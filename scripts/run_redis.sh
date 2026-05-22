#!/usr/bin/env bash
# Build and smoke-test the hiredis example (batch mode).

SCRIPT_NAME="run_redis"
source "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/_lib.sh"

ensure_container pinpoint-cpp-examples-redis redis:7 \
    -p 6379:6379 \
    -v pinpoint-cpp-examples-redis-data:/data
wait_for_port localhost 6379 30

ensure_pinpoint_env
ensure_configure BUILD_REDIS_EXAMPLE
build_targets redis_example

log "running redis_example (PING/SET/GET/INCR/LPUSH/LRANGE)"
"$BIN_DIR/redis_example"
log "redis example PASS"
