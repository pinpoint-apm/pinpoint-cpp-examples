#!/usr/bin/env bash
# Build and smoke-test the gRPC client/server example.
#
#     ./grpc/run.sh                # default: up + smoke test
#     ./grpc/run.sh test           # re-run the requests, stack already up
#     ./grpc/run.sh down           # stop it

DEMO_NAME="grpc"
source "$(cd "$(dirname "${BASH_SOURCE[0]}")/../scripts" && pwd)/_lib.sh"

PROCS="grpc-server grpc-client"
KILL_PORTS="50051 8094"

up() {
    down
    ensure_pinpoint_env
    ensure_configure BUILD_GRPC_EXAMPLE
    build_targets grpc_server grpc_client

    start_demo grpc-server "$BIN_DIR/grpc_server"
    wait_for_port localhost 50051 30

    start_demo grpc-client "$BIN_DIR/grpc_client"
    wait_for_port localhost 8094 30

    # Both ends run their own agent, and the client's calls only show as a
    # distributed trace once the server side is registered too.
    wait_agents_ready

    test_endpoints
    load_test "http://localhost:8094/run_client"
    demo_running
}

test_endpoints() {
    curl_test "GET /run_client (unary/server-streaming/bidi)" \
        "http://localhost:8094/run_client"
}

demo_main "$@"
