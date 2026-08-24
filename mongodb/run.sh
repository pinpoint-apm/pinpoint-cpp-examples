#!/usr/bin/env bash
# Build and smoke-test the mongocxx example (HTTP server mode).
#
#     ./mongodb/run.sh             # default: up + smoke test
#     ./mongodb/run.sh test        # re-run the requests, stack already up
#     ./mongodb/run.sh down        # stop it (the mongo container stays up)

DEMO_NAME="mongodb"
source "$(cd "$(dirname "${BASH_SOURCE[0]}")/../scripts" && pwd)/_lib.sh"

PROCS="mongodb"
KILL_PORTS="8092"

up() {
    down
    ensure_container pinpoint-cpp-examples-mongodb mongo:7 \
        -p 27017:27017 \
        -v pinpoint-cpp-examples-mongodb-data:/data/db
    wait_for_port localhost 27017 60

    ensure_pinpoint_env
    ensure_configure BUILD_MONGODB_EXAMPLE
    build_targets mongo_example

    start_demo mongodb "$BIN_DIR/mongo_example"
    wait_for_port localhost 8092 30
    wait_agents_ready

    test_endpoints
    load_test "http://localhost:8092/run_demo"
    demo_running
}

test_endpoints() {
    curl_test "GET /run_demo (insert/find/update/delete)" \
        "http://localhost:8092/run_demo"
}

demo_main "$@"
