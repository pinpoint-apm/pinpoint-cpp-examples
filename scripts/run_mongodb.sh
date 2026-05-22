#!/usr/bin/env bash
# Build and smoke-test the mongocxx example (batch mode).

SCRIPT_NAME="run_mongodb"
source "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/_lib.sh"

ensure_container pinpoint-cpp-examples-mongodb mongo:7 \
    -p 27017:27017 \
    -v pinpoint-cpp-examples-mongodb-data:/data/db
wait_for_port localhost 27017 60

ensure_pinpoint_env
ensure_configure BUILD_MONGODB_EXAMPLE
build_targets mongo_example

log "running mongo_example (insert/find/update/delete)"
"$BIN_DIR/mongo_example"
log "mongodb example PASS"
