#!/usr/bin/env bash
# Build and smoke-test the librdkafka producer/consumer example.
# Brings up a single-broker Kafka container (KRaft mode).

SCRIPT_NAME="run_kafka"
source "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/_lib.sh"

ensure_container pinpoint-cpp-examples-kafka apache/kafka:3.8.0 \
    -p 9092:9092 \
    -e KAFKA_NODE_ID=1 \
    -e KAFKA_PROCESS_ROLES=broker,controller \
    -e KAFKA_LISTENERS=PLAINTEXT://0.0.0.0:9092,CONTROLLER://0.0.0.0:9093 \
    -e KAFKA_ADVERTISED_LISTENERS=PLAINTEXT://localhost:9092 \
    -e KAFKA_CONTROLLER_LISTENER_NAMES=CONTROLLER \
    -e KAFKA_LISTENER_SECURITY_PROTOCOL_MAP=CONTROLLER:PLAINTEXT,PLAINTEXT:PLAINTEXT \
    -e KAFKA_INTER_BROKER_LISTENER_NAME=PLAINTEXT \
    -e KAFKA_CONTROLLER_QUORUM_VOTERS=1@localhost:9093 \
    -e KAFKA_OFFSETS_TOPIC_REPLICATION_FACTOR=1 \
    -e KAFKA_TRANSACTION_STATE_LOG_REPLICATION_FACTOR=1 \
    -e KAFKA_TRANSACTION_STATE_LOG_MIN_ISR=1 \
    -e KAFKA_AUTO_CREATE_TOPICS_ENABLE=true \
    -e KAFKA_NUM_PARTITIONS=3 \
    -e KAFKA_GROUP_INITIAL_REBALANCE_DELAY_MS=0
wait_for_port localhost 9092 90

ensure_pinpoint_env
ensure_configure BUILD_KAFKA_EXAMPLE
build_targets kafka_consumer kafka_web_producer

CONSUMER_PID="$(mktemp)"
PRODUCER_PID="$(mktemp)"
CONSUMER_LOG="$(mktemp)"
PRODUCER_LOG="$(mktemp)"
trap '
    stop_bg "$CONSUMER_PID"
    stop_bg "$PRODUCER_PID"
    log "consumer log: $CONSUMER_LOG"
    log "producer log: $PRODUCER_LOG"
' EXIT

start_bg "$CONSUMER_PID" "$CONSUMER_LOG" -- "$BIN_DIR/kafka_consumer" </dev/null
start_bg "$PRODUCER_PID" "$PRODUCER_LOG" -- "$BIN_DIR/kafka_web_producer" </dev/null
wait_for_port localhost 8090 30

# Give the consumer a moment to subscribe before producing.
sleep 3

MESSAGE="hello-from-script-$$-$(date +%s)"
curl_test "produce '$MESSAGE'" \
    "http://localhost:8090/run_producer?message=$MESSAGE"

# Wait up to 20s for the consumer to log the message we just produced.
for _ in $(seq 1 20); do
    if grep -q "$MESSAGE" "$CONSUMER_LOG"; then
        log "consumer received '$MESSAGE'"
        load_test "http://localhost:8090/run_producer?message=load-test-$$"
        log "kafka example PASS"
        exit 0
    fi
    sleep 1
done
die "consumer never received '$MESSAGE' (check $CONSUMER_LOG)"
