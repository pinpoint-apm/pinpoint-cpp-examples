#!/usr/bin/env bash
# Build and smoke-test the mongocxx example (HTTP server mode).

SCRIPT_NAME="run_mongodb"
source "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/_lib.sh"

ensure_container pinpoint-cpp-examples-mongodb mongo:7 \
    -p 27017:27017 \
    -v pinpoint-cpp-examples-mongodb-data:/data/db
wait_for_port localhost 27017 60

ensure_pinpoint_env
ensure_configure BUILD_MONGODB_EXAMPLE
build_targets mongo_example

PIDFILE="$(mktemp)"
LOGFILE="$(mktemp)"
trap 'stop_bg "$PIDFILE"; log "server log: $LOGFILE"' EXIT

start_bg "$PIDFILE" "$LOGFILE" -- "$BIN_DIR/mongo_example" </dev/null
wait_for_port localhost 8092 30

curl_test "GET /run_demo (insert/find/update/delete)" \
    "http://localhost:8092/run_demo"

load_test "http://localhost:8092/run_demo"

log "mongodb example PASS"
