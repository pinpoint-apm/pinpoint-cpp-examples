#!/usr/bin/env bash
# Build and smoke-test the cpptrace example.
#
#     ./cpptrace/run.sh            # default: up + smoke test
#     ./cpptrace/run.sh test       # re-run the requests, stack already up
#     ./cpptrace/run.sh down       # stop it

DEMO_NAME="cpptrace"
source "$(cd "$(dirname "${BASH_SOURCE[0]}")/../scripts" && pwd)/_lib.sh"

PROCS="cpptrace"
KILL_PORTS="8088"

up() {
    down
    ensure_pinpoint_env
    ensure_configure BUILD_CPPTRACE_EXAMPLE
    build_targets cpptrace_example

    start_demo cpptrace "$BIN_DIR/cpptrace_example"
    wait_for_port localhost 8088 30
    wait_agents_ready

    test_endpoints
    load_test "http://localhost:8088/users/123?name=foo"
    demo_running
}

test_endpoints() {
    curl_test "GET /users/123" "http://localhost:8088/users/123?name=foo"
    # /outgoing intentionally fails (localhost:9000 is not running) so the
    # server attaches the cpptrace call stack to the Pinpoint span. We accept
    # any HTTP response, just want to exercise the code path.
    log "test: GET /outgoing (expected to surface a traced failure)"
    curl -sS --max-time 10 -o /dev/null -w "  http=%{http_code}\n" \
        "http://localhost:8088/outgoing?host=localhost:9000&path=/bar" || true
    # The failed call must be *traced*, not fatal — the server has to survive it.
    assert_alive cpptrace
}

demo_main "$@"
