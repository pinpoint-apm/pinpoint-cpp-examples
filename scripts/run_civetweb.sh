#!/usr/bin/env bash
# Build and smoke-test the civetweb example.
#
#     ./scripts/run_civetweb.sh            # default: up + smoke test
#     ./scripts/run_civetweb.sh test       # re-run the requests, stack already up
#     ./scripts/run_civetweb.sh down       # stop it

SCRIPT_NAME="run_civetweb"
source "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/_lib.sh"

DEMO_NAME="civetweb"
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
