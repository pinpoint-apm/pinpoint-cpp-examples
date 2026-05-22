# shellcheck shell=bash
# Shared helpers for the per-example build+test scripts. Source this from
# each run_*.sh script; do not execute directly.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
PRESET="${PRESET:-debug}"
BUILD_DIR="$REPO_ROOT/build/$PRESET"
BIN_DIR="$BUILD_DIR/bin"

: "${SCRIPT_NAME:=$(basename "${BASH_SOURCE[1]:-script}")}"

log() {
    printf '\033[1;34m[%s]\033[0m %s\n' "$SCRIPT_NAME" "$*"
}

die() {
    printf '\033[1;31m[%s] ERROR\033[0m %s\n' "$SCRIPT_NAME" "$*" >&2
    exit 1
}

ensure_pinpoint_env() {
    # Examples bail out if the agent cannot resolve a collector address.
    # Point at the dev collector via env var; ports keep their built-in
    # defaults (9991/9992/9993). Caller can override either value by
    # exporting it before invoking the script.
    : "${PINPOINT_CPP_GRPC_HOST:=localhost}"
    export PINPOINT_CPP_GRPC_HOST
    log "PINPOINT_CPP_GRPC_HOST=$PINPOINT_CPP_GRPC_HOST"
}

ensure_configure() {
    # Configure (first time) or update cache options for the example we are
    # about to build. Leaves any other previously enabled examples ON.
    local args=()
    local opt
    for opt in "$@"; do args+=("-D${opt}=ON"); done
    if [ ! -f "$BUILD_DIR/CMakeCache.txt" ]; then
        log "configuring (--preset $PRESET) ${args[*]}"
        ( cd "$REPO_ROOT" && cmake --preset "$PRESET" "${args[@]}" )
    else
        log "updating cache: ${args[*]}"
        ( cd "$REPO_ROOT" && cmake -B "$BUILD_DIR" "${args[@]}" >/dev/null )
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

start_bg() {
    # start_bg <pidfile> <logfile> -- <command...>
    local pidfile="$1" logfile="$2"; shift 2
    [ "$1" = "--" ] && shift
    log "starting: $* (log: $logfile)"
    "$@" >"$logfile" 2>&1 &
    echo $! >"$pidfile"
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
