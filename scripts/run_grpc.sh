#!/usr/bin/env bash
# Build and smoke-test the gRPC client/server example.

SCRIPT_NAME="run_grpc"
source "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/_lib.sh"

ensure_pinpoint_env
ensure_configure BUILD_GRPC_EXAMPLE
build_targets grpc_server grpc_client

SERVER_PIDFILE="$(mktemp)"
SERVER_LOGFILE="$(mktemp)"
CLIENT_PIDFILE="$(mktemp)"
CLIENT_LOGFILE="$(mktemp)"
trap 'stop_bg "$CLIENT_PIDFILE"; stop_bg "$SERVER_PIDFILE"; log "server log: $SERVER_LOGFILE"; log "client log: $CLIENT_LOGFILE"' EXIT

start_bg "$SERVER_PIDFILE" "$SERVER_LOGFILE" -- "$BIN_DIR/grpc_server" </dev/null
wait_for_port localhost 50051 30

start_bg "$CLIENT_PIDFILE" "$CLIENT_LOGFILE" -- "$BIN_DIR/grpc_client" </dev/null
wait_for_port localhost 8094 30

curl_test "GET /run_client (unary/server-streaming/bidi)" \
    "http://localhost:8094/run_client"

log "grpc example PASS"
