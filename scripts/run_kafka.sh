#!/usr/bin/env bash
# Build and smoke-test the librdkafka producer/consumer example.
# Brings up a single-broker Kafka container (KRaft mode).
#
#     ./scripts/run_kafka.sh               # default: up + smoke test
#     ./scripts/run_kafka.sh test          # re-run the requests, stack already up
#     ./scripts/run_kafka.sh down          # stop it (the kafka container stays up)

SCRIPT_NAME="run_kafka"
source "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/_lib.sh"

DEMO_NAME="kafka"
PROCS="kafka-consumer kafka-producer"
KILL_PORTS="8090"

up() {
    down
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

    start_demo kafka-consumer "$BIN_DIR/kafka_consumer"
    start_demo kafka-producer "$BIN_DIR/kafka_web_producer"
    wait_for_port localhost 8090 30

    # Producer and consumer each run their own agent; the consumer side of the
    # message trace only lands once its own registration is through.
    wait_agents_ready

    # Give the consumer a moment to subscribe before producing.
    sleep 3

    test_endpoints
    load_test "http://localhost:8090/run_producer?message=load-test-$$"
    demo_running
}

test_endpoints() {
    local message="hello-from-script-$$-$(date +%s)"
    local consumer_log="$RUN_DIR/kafka-consumer.log"

    curl_test "produce '$message'" \
        "http://localhost:8090/run_producer?message=$message"

    # Wait up to 20s for the consumer to log the message we just produced.
    local _
    for _ in $(seq 1 20); do
        if grep -q "$message" "$consumer_log"; then
            log "consumer received '$message'"
            return 0
        fi
        sleep 1
    done
    die "consumer never received '$message' (check $consumer_log)"
}

demo_main "$@"
