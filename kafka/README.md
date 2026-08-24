# kafka — librdkafka producer/consumer + header-based trace propagation

The producer publishes a message with the trace context injected into Kafka message headers; the consumer extracts it and starts a new span as a child of the producer's trace. App names: `cpp-kafka-producer-demo`, `cpp-kafka-consumer-demo`.

**Requires:** librdkafka (`brew install librdkafka`, or `librdkafka-dev` on Linux) and Docker for the broker at `localhost:9092` (topic `test-topic`). The CMake build fetches librdkafka if it isn't installed; the Bazel build links the Homebrew one.

Run these from the repository root. See the [top-level README](../README.md) for the shared requirements, build systems, and troubleshooting.

**Build & run:**

```bash
./kafka/run.sh                # brings up kafka, runs both binaries, verifies the message

# Manual
docker run -d --name pinpoint-cpp-examples-kafka -p 9092:9092 \
    -e KAFKA_NODE_ID=1 -e KAFKA_PROCESS_ROLES=broker,controller \
    -e KAFKA_ADVERTISED_LISTENERS=PLAINTEXT://localhost:9092 \
    apache/kafka:3.8.0                # see kafka/run.sh for the full flag set

cmake -B build/debug -DBUILD_KAFKA_EXAMPLE=ON
cmake --build build/debug --target kafka_web_producer kafka_consumer

# Terminal 1: start the consumer first (subscribes to test-topic)
./build/debug/bin/kafka_consumer
# Terminal 2: producer (HTTP server on port 8090)
./build/debug/bin/kafka_web_producer

# Bazel
bazel run //kafka:kafka_consumer
bazel run //kafka:kafka_web_producer
```

The broker address can be overridden via `KAFKA_BROKERS` (default `localhost:9092`).

**Producer endpoint** (`localhost:8090`):
- `GET /run_producer[?message=...]` — produces the message to `test-topic`

**Test:**

```bash
curl 'http://localhost:8090/run_producer?message=hello-pinpoint'
# Watch for "Consumed message: hello-pinpoint" in the kafka_consumer terminal
```

