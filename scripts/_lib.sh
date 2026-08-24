# shellcheck shell=bash
# Shared plumbing for the per-example demo drivers. Source this from each
# <demo>/run.sh driver; do not execute directly.
#
# Contract for a <demo>/run.sh driver (mirrors the python agent's
# examples/<demo>/run.sh drivers):
#
#     DEMO_NAME="redis"       # set first: the lib logs with it
#     source "$(cd "$(dirname "${BASH_SOURCE[0]}")/../scripts" && pwd)/_lib.sh"
#     PROCS="redis"           # log/pid basenames under $RUN_DIR, one per process
#     KILL_PORTS="8093"       # listener ports down() force-kills
#     up() {
#         down
#         ...build / containers...
#         start_demo redis "$BIN_DIR/redis_example"
#         wait_for_port localhost 8093 30
#         wait_agents_ready   # nothing is recorded before this returns
#         test_endpoints
#         load_test <urls...>
#         demo_running
#     }
#     test_endpoints() { curl_test ...; }
#     demo_main "$@"          # last line — dispatches up|test|down
#
# down() has a var-driven default below; redefine it after sourcing if a demo
# ever needs custom teardown. Unlike the python drivers, down() leaves the
# docker containers running: they are named and volume-backed here, and
# ensure_container reuses them, so tearing them down would only make the next
# run wait for another mysql first boot.
#
# up() deliberately leaves the stack running. The spans it just produced are
# streamed to the collector by a background thread, and the endpoints stay
# hittable while you look at the Pinpoint UI; stop it with `<demo>/run.sh down`.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
PRESET="${PRESET:-debug}"
BUILD_DIR="$REPO_ROOT/build/$PRESET"
BIN_DIR="$BUILD_DIR/bin"
RUN_DIR="${PINPOINT_DEMO_RUN_DIR:-/tmp/pinpoint-cpp-demo}"

: "${DEMO_NAME:?DEMO_NAME must be set before sourcing _lib.sh}"

mkdir -p "$RUN_DIR"

log() {
    printf '\033[1;34m[%s]\033[0m %s\n' "$DEMO_NAME" "$*"
}

warn() {
    printf '\033[1;33m[%s] WARN\033[0m %s\n' "$DEMO_NAME" "$*" >&2
}

die() {
    printf '\033[1;31m[%s] ERROR\033[0m %s\n' "$DEMO_NAME" "$*" >&2
    exit 1
}

# ---------------------------------------------------------------------------
# Options — sourcing this file leaves "$@" as the driver's arguments, so
# they are parsed here once for every <demo>/run.sh uniformly.
#   up | test | down   what to do (default: up)
#   --load <seconds>   after the smoke test, keep issuing requests for that
#                      long at random 50-500ms intervals (see load_test
#                      below); 0 disables the load test
# ---------------------------------------------------------------------------
DEMO_CMD=up
RUN_LOAD_DURATION=0
__expect_duration=0
for __opt in "$@"; do
    if [ "$__expect_duration" -eq 1 ]; then
        case "$__opt" in
            ''|*[!0-9]*) die "--load requires an integer duration in seconds, got '$__opt'" ;;
        esac
        RUN_LOAD_DURATION="$__opt"
        __expect_duration=0
        continue
    fi
    case "$__opt" in
        up|test|down) DEMO_CMD="$__opt" ;;
        --load) __expect_duration=1 ;;
        *) die "unknown option '$__opt' (usage: $0 [up|test|down] [--load <seconds>])" ;;
    esac
done
[ "$__expect_duration" -eq 0 ] || die "--load requires a duration in seconds"
unset __opt __expect_duration

ensure_pinpoint_env() {
    # Examples bail out if the agent cannot resolve a collector address.
    # Point at the dev collector via env var; ports keep their built-in
    # defaults (9991/9992/9993). Caller can override either value by
    # exporting it before invoking the script.
    : "${PINPOINT_CPP_COLLECTOR_HOST:=localhost}"
    export PINPOINT_CPP_COLLECTOR_HOST
    log "PINPOINT_CPP_COLLECTOR_HOST=$PINPOINT_CPP_COLLECTOR_HOST"
}

ensure_configure() {
    # Configure (first time) or update cache options for the example we are
    # about to build. Leaves any other previously enabled examples ON.
    local args=()
    local opt
    for opt in "$@"; do args+=("-D${opt}=ON"); done
    # ${args[@]+...}: bash 3.2 (macOS) treats an empty array as unset under
    # `set -u`, and nginx/run.sh configures with no example toggle at all.
    if [ ! -f "$BUILD_DIR/CMakeCache.txt" ]; then
        log "configuring (--preset $PRESET) ${args[*]:-}"
        ( cd "$REPO_ROOT" && cmake --preset "$PRESET" ${args[@]+"${args[@]}"} )
    else
        log "updating cache: ${args[*]:-}"
        ( cd "$REPO_ROOT" && cmake -B "$BUILD_DIR" ${args[@]+"${args[@]}"} >/dev/null )
    fi
}

build_targets() {
    log "building targets: $*"
    cmake --build "$BUILD_DIR" --target "$@"
}

ensure_container() {
    # ensure_container <name> <image> [docker-run-args...]
    # Idempotent: if running, reuse; if stopped, start; otherwise create.
    local name="$1" image="$2"; shift 2
    if docker ps --format '{{.Names}}' | grep -qx "$name"; then
        log "docker: '$name' already running"
        return 0
    fi
    if docker ps -a --format '{{.Names}}' | grep -qx "$name"; then
        log "docker: starting existing '$name'"
        docker start "$name" >/dev/null
        return 0
    fi
    log "docker: creating '$name' from $image"
    docker run -d --name "$name" "$@" "$image" >/dev/null
}

agent_log_path() {
    # agent_log_path <name> — where the agent inside process <name> logs.
    # nginx/run.sh needs the same path for its generated YAML, so the naming
    # convention lives here rather than in start_demo alone.
    printf '%s/%s.agent.log\n' "$RUN_DIR" "$1"
}

start_demo() {
    # start_demo <name> <command...>
    # Runs the example in the background: its stdout/stderr go to
    # $RUN_DIR/<name>.log, the agent's own log to $RUN_DIR/<name>.agent.log,
    # and its pid to $RUN_DIR/<name>.pid.
    #
    # The separate agent log is what makes wait_agent_ready possible: the
    # agent's file sink flushes per line, while its stdout is block-buffered
    # as soon as it is redirected to a file, so registration would only
    # become visible in bursts (the python drivers get the same effect from
    # `python -u`).
    #
    # stdin is /dev/null so an example that reads a key to stop (civetweb)
    # cannot consume the terminal's input.
    local name="$1"; shift
    local logfile="$RUN_DIR/$name.log"
    local agentlog
    agentlog="$(agent_log_path "$name")"
    # The agent's file sink opens in append mode, so a stale log would make
    # wait_agent_ready match a previous run's registration immediately.
    rm -f "$logfile" "$agentlog"
    log "starting $name: $* (log: $logfile, agent log: $agentlog)"
    (
        export PINPOINT_CPP_LOG_FILE_PATH="$agentlog"
        exec "$@"
    ) >"$logfile" 2>&1 </dev/null &
    echo $! >"$RUN_DIR/$name.pid"
}

stop_bg() {
    # stop_bg <pidfile>
    local pidfile="$1" pid
    [ -f "$pidfile" ] || return 0
    pid="$(cat "$pidfile")"
    rm -f "$pidfile"
    if [ -n "$pid" ] && kill -0 "$pid" 2>/dev/null; then
        log "stopping pid $pid"
        kill "$pid" 2>/dev/null || true
        wait "$pid" 2>/dev/null || true
    fi
}

kill_port() {
    # kill_port <port> — for a process that outlived its pid file.
    local port="$1" pids
    pids="$(lsof -ti :"$port" 2>/dev/null || true)"
    [ -n "$pids" ] && kill $pids 2>/dev/null || true
    return 0
}

assert_alive() {
    # assert_alive <name> — the process must still be up. Used after a request
    # whose failure is tolerated, where a crash would otherwise pass silently.
    # (bash 3.2 declares every name in one `local` before assigning, so
    # pidfile cannot reference name in the same statement)
    local name="$1"
    local pidfile="$RUN_DIR/$name.pid"
    kill -0 "$(cat "$pidfile" 2>/dev/null)" 2>/dev/null \
        || die "$name died (see $RUN_DIR/$name.log)"
}

# Default teardown, driven by $DEMO_NAME / $PROCS / $KILL_PORTS.
down() {
    log "stopping $DEMO_NAME"
    local name port
    for name in $PROCS; do
        stop_bg "$RUN_DIR/$name.pid"
    done
    for port in ${KILL_PORTS:-}; do
        kill_port "$port"
    done
}

wait_for_port() {
    # wait_for_port <host> <port> [timeout-seconds]
    local host="$1" port="$2" timeout="${3:-60}"
    log "waiting for $host:$port (up to ${timeout}s)"
    local start now
    start="$(date +%s)"
    while true; do
        if (exec 3<>"/dev/tcp/$host/$port") 2>/dev/null; then
            exec 3<&- 3>&-
            return 0
        fi
        now="$(date +%s)"
        if [ $((now - start)) -ge "$timeout" ]; then
            die "timed out waiting for $host:$port"
        fi
        sleep 1
    done
}

wait_for_http() {
    # wait_for_http <url> [timeout-seconds]
    local url="$1" timeout="${2:-60}"
    log "waiting for HTTP $url (up to ${timeout}s)"
    local start now
    start="$(date +%s)"
    while ! curl -fsS -o /dev/null --max-time 2 "$url" 2>/dev/null; do
        now="$(date +%s)"
        if [ $((now - start)) -ge "$timeout" ]; then
            die "timed out waiting for $url"
        fi
        sleep 1
    done
}

wait_agent_ready() {
    # wait_agent_ready <name> [count] [timeout-seconds]
    # The agent's grpc handshake with the collector runs on a background
    # thread and takes a few seconds. Spans recorded before it lands never
    # reach the collector, so a smoke test that starts earlier leaves nothing
    # to look at in the web UI — poll the agent log instead of racing it.
    #
    # COUNT is for processes that fork (nginx workers): each child registers
    # its own agent instance into the same log file.
    local name="$1" count="${2:-1}" timeout="${3:-60}"
    local agentlog
    agentlog="$(agent_log_path "$name")"
    log "waiting for $name agent registration (up to ${timeout}s)"
    local start now found
    start="$(date +%s)"
    while true; do
        found="$(grep -c "success to register the agent" "$agentlog" 2>/dev/null || true)"
        if [ "${found:-0}" -ge "$count" ]; then
            log "  $name agent registered"
            return 0
        fi
        now="$(date +%s)"
        if [ $((now - start)) -ge "$timeout" ]; then
            # Not fatal: the examples still serve traffic without a collector,
            # and that is a legitimate way to run them offline.
            warn "$name agent did not register within ${timeout}s — continuing, but the spans will not show up in the UI (see $agentlog)"
            return 0
        fi
        sleep 1
    done
}

wait_agents_ready() {
    # Every process in $PROCS runs its own agent, so all of them have to be
    # registered before the smoke test starts.
    local name
    for name in $PROCS; do
        wait_agent_ready "$name"
    done
}

curl_test() {
    # curl_test <label> <url> [curl-args...]
    local label="$1" url="$2"; shift 2
    log "test: $label"
    log "  curl $url"
    local out
    if ! out="$(curl -fsS --max-time 10 "$@" "$url")"; then
        die "curl failed for $url"
    fi
    if [ -n "$out" ]; then
        printf '  %s\n' "$out"
    fi
}

load_test() {
    # load_test <url...>
    # No-op unless the script was invoked with --load <seconds> and the
    # duration is > 0. Keeps issuing requests for that long, each time picking
    # one of the given URLs at random and sleeping a random 50-500ms before
    # the next one. Failures are counted, not fatal.
    #
    # Ctrl-C (SIGINT) stops the loop early: partial counts are reported and
    # the script continues normally, leaving the stack up like any other run.
    [ "$RUN_LOAD_DURATION" -gt 0 ] || return 0

    local duration_s="$RUN_LOAD_DURATION" min_ms=50 max_ms=500
    local urls=("$@")
    local url gap_ms ok=0 fail=0 interrupted=0
    trap 'interrupted=1' INT
    log "load test: ${duration_s}s of requests at random ${min_ms}-${max_ms}ms intervals (Ctrl-C to stop early)"
    local start_s=$SECONDS
    while [ $((SECONDS - start_s)) -lt "$duration_s" ] && [ "$interrupted" -eq 0 ]; do
        url="${urls[RANDOM % ${#urls[@]}]}"
        if curl -fsS -o /dev/null --max-time 10 "$url"; then
            ok=$((ok + 1))
        elif [ "$interrupted" -eq 0 ]; then
            # (a curl killed by Ctrl-C is not a real request failure)
            fail=$((fail + 1))
            log "  request failed: $url"
        fi
        gap_ms=$((RANDOM % (max_ms - min_ms + 1) + min_ms))
        # `|| true`: an interrupted sleep exits 130, which must not trip -e.
        sleep "$(printf '%d.%03d' $((gap_ms / 1000)) $((gap_ms % 1000)))" || true
    done
    trap - INT
    if [ "$interrupted" -eq 1 ]; then
        log "load test interrupted: $ok ok, $fail failed after $((SECONDS - start_s))s"
    else
        log "load test done: $ok ok, $fail failed over ${duration_s}s"
    fi
}

demo_running() {
    # Closing hint after a successful up: the stack stays up on purpose.
    local name logs=""
    for name in $PROCS; do logs+="$RUN_DIR/$name.log "; done
    log "$DEMO_NAME demo PASS"
    log "stack running — logs: ${logs% }"
    log "re-run the requests with: $0 test"
    log "stop it with: $0 down"
}

demo_main() {
    case "$DEMO_CMD" in
        up)   up ;;
        test) test_endpoints ;;
        down) down ;;
    esac
}
