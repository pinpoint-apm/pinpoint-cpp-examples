#!/usr/bin/env bash
# Build and smoke-test the libcurl example.
#
#     ./scripts/run_curl.sh                # default: up + smoke test
#     ./scripts/run_curl.sh test           # re-run the requests, stack already up
#     ./scripts/run_curl.sh down           # stop it

SCRIPT_NAME="run_curl"
source "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/_lib.sh"

DEMO_NAME="curl"
PROCS="curl"
KILL_PORTS="8091"

# The example's /run_request issues an outbound HTTP request; httpbin is a
# reliable external target. Override TARGET_URL if you need to keep the
# test offline.
TARGET_URL="${TARGET_URL:-https://httpbin.org/get}"

up() {
    down
    ensure_pinpoint_env
    ensure_configure BUILD_CURL_EXAMPLE
    build_targets curl_web_example

    start_demo curl "$BIN_DIR/curl_web_example"
    wait_for_port localhost 8091 30
    wait_agents_ready

    test_endpoints
    load_test "http://localhost:8091/run_request?url=$TARGET_URL"
    demo_running
}

test_endpoints() {
    curl_test "GET /run_request?url=$TARGET_URL" \
        "http://localhost:8091/run_request?url=$TARGET_URL"
}

demo_main "$@"
