#!/usr/bin/env bash
# Build and smoke-test the MySQL X DevAPI example.
#
#     ./mysql/run.sh               # default: up + smoke test
#     ./mysql/run.sh test          # re-run the requests, stack already up
#     ./mysql/run.sh down          # stop it (the mysql container stays up)

DEMO_NAME="mysql"
source "$(cd "$(dirname "${BASH_SOURCE[0]}")/../scripts" && pwd)/_lib.sh"

PROCS="mysql"
KILL_PORTS="8089"

up() {
    down
    ensure_container pinpoint-cpp-examples-mysql mysql:8.0 \
        -p 3306:3306 -p 33060:33060 \
        -e MYSQL_ROOT_PASSWORD=pinpoint123 \
        -e MYSQL_ROOT_HOST=% \
        -e MYSQL_DATABASE=test \
        -e MYSQL_USER=pinpoint \
        -e MYSQL_PASSWORD=pinpoint123 \
        -v pinpoint-cpp-examples-mysql-data:/var/lib/mysql \
        -v "$REPO_ROOT/mysql/init.sql:/docker-entrypoint-initdb.d/init.sql:ro"
    # mysql 8 takes a while to be ready on first boot; poll the X Protocol port.
    wait_for_port localhost 33060 120

    ensure_pinpoint_env
    ensure_configure BUILD_MYSQL_EXAMPLE
    build_targets mysql_example

    start_demo mysql "$BIN_DIR/mysql_example"
    wait_for_http "http://localhost:8089/status" 60
    wait_agents_ready

    test_endpoints
    load_test "http://localhost:8089/db-demo" "http://localhost:8089/status"
    demo_running
}

test_endpoints() {
    curl_test "GET /status"  "http://localhost:8089/status"
    curl_test "GET /db-demo" "http://localhost:8089/db-demo"
}

demo_main "$@"
