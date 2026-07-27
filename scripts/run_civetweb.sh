#!/usr/bin/env bash
# Build and smoke-test the civetweb example.

SCRIPT_NAME="run_civetweb"
source "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/_lib.sh"

ensure_pinpoint_env
ensure_configure BUILD_CIVETWEB_EXAMPLE
build_targets civetweb_example

PIDFILE="$(mktemp)"
LOGFILE="$(mktemp)"
trap 'stop_bg "$PIDFILE"; log "server log: $LOGFILE"' EXIT

# civetweb_example reads Enter to stop; feed an empty stdin so it cannot
# accidentally consume terminal input from this script.
start_bg "$PIDFILE" "$LOGFILE" -- "$BIN_DIR/civetweb_example" </dev/null
wait_for_http "http://localhost:8080/api/health" 30

curl_test "GET /api/users"  "http://localhost:8080/api/users"
curl_test "GET /api/health" "http://localhost:8080/api/health"

load_test "http://localhost:8080/api/users" "http://localhost:8080/api/health"

log "civetweb example PASS"
