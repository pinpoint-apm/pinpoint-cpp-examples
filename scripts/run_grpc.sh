#!/usr/bin/env bash
# Build and smoke-test the gRPC client/server example.

SCRIPT_NAME="run_grpc"
source "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/_lib.sh"

ensure_pinpoint_env
ensure_configure BUILD_GRPC_EXAMPLE
build_targets grpc_server grpc_client

PIDFILE="$(mktemp)"
LOGFILE="$(mktemp)"
trap 'stop_bg "$PIDFILE"; log "server log: $LOGFILE"' EXIT

start_bg "$PIDFILE" "$LOGFILE" -- "$BIN_DIR/grpc_server" </dev/null
wait_for_port localhost 50051 30

log "running grpc_client (foreground)"
"$BIN_DIR/grpc_client"

log "grpc example PASS"
