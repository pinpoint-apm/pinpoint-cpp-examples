#!/usr/bin/env bash
# Build and smoke-test the civetweb example.
#
#     ./civetweb/run.sh            # default: up + smoke test
#     ./civetweb/run.sh test       # re-run the requests, stack already up
#     ./civetweb/run.sh down       # stop it

DEMO_NAME="civetweb"
source "$(cd "$(dirname "${BASH_SOURCE[0]}")/../scripts" && pwd)/_lib.sh"

PROCS="civetweb"
KILL_PORTS="8080"

up() {
    down
    ensure_pinpoint_env
    ensure_configure BUILD_CIVETWEB_EXAMPLE
    build_targets civetweb_example

    start_demo civetweb "$BIN_DIR/civetweb_example"
    wait_for_http "http://localhost:8080/api/health" 30
    wait_agents_ready

    test_endpoints
    load_test "http://localhost:8080/api/users" "http://localhost:8080/api/health"
    demo_running
}

test_endpoints() {
    curl_test "GET /api/users"  "http://localhost:8080/api/users"
    curl_test "GET /api/health" "http://localhost:8080/api/health"
}

demo_main "$@"
