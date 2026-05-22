#!/usr/bin/env bash
# Build and smoke-test the cpptrace example.

SCRIPT_NAME="run_cpptrace"
source "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/_lib.sh"

ensure_pinpoint_env
ensure_configure BUILD_CPPTRACE_EXAMPLE
build_targets cpptrace_example

PIDFILE="$(mktemp)"
LOGFILE="$(mktemp)"
trap 'stop_bg "$PIDFILE"; log "server log: $LOGFILE"' EXIT

start_bg "$PIDFILE" "$LOGFILE" -- "$BIN_DIR/cpptrace_example" </dev/null
wait_for_port localhost 8088 30

curl_test "GET /users/123" "http://localhost:8088/users/123?name=foo"
# /outgoing intentionally fails (localhost:9000 is not running) so the
# server attaches the cpptrace call stack to the Pinpoint span. We accept
# any HTTP response, just want to exercise the code path.
log "test: GET /outgoing (expected to surface a traced failure)"
curl -sS --max-time 10 -o /dev/null -w "  http=%{http_code}\n" \
    "http://localhost:8088/outgoing?host=localhost:9000&path=/bar" || true

log "cpptrace example PASS"
