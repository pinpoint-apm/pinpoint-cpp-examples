#!/usr/bin/env bash
# Build and smoke-test the hiredis example (HTTP server mode).

SCRIPT_NAME="run_redis"
source "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/_lib.sh"

ensure_container pinpoint-cpp-examples-redis redis:7 \
    -p 6379:6379 \
    -v pinpoint-cpp-examples-redis-data:/data
wait_for_port localhost 6379 30

ensure_pinpoint_env
ensure_configure BUILD_REDIS_EXAMPLE
build_targets redis_example

PIDFILE="$(mktemp)"
LOGFILE="$(mktemp)"
trap 'stop_bg "$PIDFILE"; log "server log: $LOGFILE"' EXIT

start_bg "$PIDFILE" "$LOGFILE" -- "$BIN_DIR/redis_example" </dev/null
wait_for_port localhost 8093 30

curl_test "GET /run_demo (PING/SET/GET/INCR/LPUSH/LRANGE)" \
    "http://localhost:8093/run_demo"

load_test "http://localhost:8093/run_demo"

log "redis example PASS"
