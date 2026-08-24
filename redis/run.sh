#!/usr/bin/env bash
# Build and smoke-test the hiredis example (HTTP server mode).
#
#     ./redis/run.sh               # default: up + smoke test
#     ./redis/run.sh test          # re-run the requests, stack already up
#     ./redis/run.sh down          # stop it (the redis container stays up)

DEMO_NAME="redis"
source "$(cd "$(dirname "${BASH_SOURCE[0]}")/../scripts" && pwd)/_lib.sh"

PROCS="redis"
KILL_PORTS="8093"

up() {
    down
    ensure_container pinpoint-cpp-examples-redis redis:7 \
        -p 6379:6379 \
        -v pinpoint-cpp-examples-redis-data:/data
    wait_for_port localhost 6379 30

    ensure_pinpoint_env
    ensure_configure BUILD_REDIS_EXAMPLE
    build_targets redis_example

    start_demo redis "$BIN_DIR/redis_example"
    wait_for_port localhost 8093 30
    wait_agents_ready

    test_endpoints
    load_test "http://localhost:8093/run_demo"
    demo_running
}

test_endpoints() {
    curl_test "GET /run_demo (PING/SET/GET/INCR/LPUSH/LRANGE)" \
        "http://localhost:8093/run_demo"
}

demo_main "$@"
