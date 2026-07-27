#!/usr/bin/env bash
# Build and smoke-test the libcurl example.

SCRIPT_NAME="run_curl"
source "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/_lib.sh"

ensure_pinpoint_env
ensure_configure BUILD_CURL_EXAMPLE
build_targets curl_web_example

PIDFILE="$(mktemp)"
LOGFILE="$(mktemp)"
trap 'stop_bg "$PIDFILE"; log "server log: $LOGFILE"' EXIT

start_bg "$PIDFILE" "$LOGFILE" -- "$BIN_DIR/curl_web_example" </dev/null
wait_for_port localhost 8091 30

# The example's /run_request issues an outbound HTTP request; httpbin is a
# reliable external target. Override TARGET_URL if you need to keep the
# test offline.
TARGET_URL="${TARGET_URL:-https://httpbin.org/get}"
curl_test "GET /run_request?url=$TARGET_URL" \
    "http://localhost:8091/run_request?url=$TARGET_URL"

load_test "http://localhost:8091/run_request?url=$TARGET_URL"

log "curl example PASS"
