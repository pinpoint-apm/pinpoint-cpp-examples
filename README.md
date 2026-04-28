# pinpoint-cpp-agent C/C++ Examples

Each subdirectory contains a C/C++ application that uses the library named after the directory (CivetWeb, cpp-httplib + cpptrace, libcurl, gRPC, librdkafka, mongocxx, MySQL X DevAPI, hiredis) and traces it with [pinpoint-cpp-agent](https://github.com/pinpoint-apm/pinpoint-cpp-agent).

Two build systems are supported:

- **CMake** — builds pinpoint-cpp-agent via `FetchContent`. All examples build (macOS / Linux).
- **Bazel** — uses pinpoint-cpp-agent's own bzlmod build directly. Requires Bazel 8. On macOS, `cpptrace_example` is excluded (libunwind compatibility).

## Layout

| Directory | Library | Binaries |
|-----------|---------|----------|
| [civetweb/](civetweb/)   | [CivetWeb](https://github.com/civetweb/civetweb) (HTTP server, C)        | `civetweb_example`                  |
| [cpptrace/](cpptrace/)   | [cpptrace](https://github.com/jeremy-rifkin/cpptrace) + cpp-httplib      | `cpptrace_example`                  |
| [curl/](curl/)           | [libcurl](https://curl.se/libcurl/) + cpp-httplib                        | `curl_web_example`                  |
| [grpc/](grpc/)           | [gRPC](https://grpc.io/)                                                  | `grpc_client`, `grpc_server`        |
| [kafka/](kafka/)         | [librdkafka](https://github.com/confluentinc/librdkafka) + cpp-httplib   | `kafka_web_producer`, `kafka_consumer` |
| [mongodb/](mongodb/)     | [mongo-cxx-driver](https://github.com/mongodb/mongo-cxx-driver)          | `mongo_example`                     |
| [mysql/](mysql/)         | [MySQL Connector/C++ X DevAPI](https://dev.mysql.com/doc/connector-cpp/) + cpp-httplib | `mysql_example` |
| [redis/](redis/)         | [hiredis](https://github.com/redis/hiredis)                              | `redis_example`                     |

## Prerequisites

### System libraries (macOS / Homebrew)

```bash
brew install cmake ninja bazelisk pkg-config
brew install hiredis librdkafka mongo-cxx-driver mysql-connector-c++
# civetweb, cpptrace, and cpp-httplib are fetched automatically at build time.
```

On a Linux distro, install the equivalent packages with your package manager (`libcurl4-openssl-dev`, `libhiredis-dev`, `librdkafka-dev`, `libmongocxx-dev`, `libmysqlcppconn-dev`, etc.).

### Pinpoint Collector

Each example sends trace data to the collector at `127.0.0.1:9991/9992/9993` by default, or to whatever the YAML file pointed to by `PINPOINT_CPP_CONFIG_FILE` specifies. The examples still run if no collector is reachable — the trace exports just fail silently.

Sample YAML (`/tmp/pinpoint-config.yaml`):

```yaml
ApplicationName: my-app
AgentId: my-agent
Collector:
  Ip: 127.0.0.1
  GrpcPort: 9991
  StatPort: 9992
  SpanPort: 9993
```

### Backing services (kafka / mongodb / mysql / redis)

`docker-compose.yml` defines the four services:

```bash
docker compose up -d                  # start all four
docker compose up -d redis            # start one
docker compose down -v                # stop and wipe volumes
```

Endpoints:
- kafka: `localhost:9092` (topic: `test-topic`)
- mongodb: `localhost:27017`
- mysql: `localhost:3306` (classic), `localhost:33060` (X Protocol). user `root` / password `pinpoint123`, db `test`
- redis: `localhost:6379`

## Build

### CMake

```bash
cmake --preset debug                  # or --preset release
cmake --build build/debug
```

Binaries land in `build/debug/bin/`. The first build takes ~5–10 minutes because pinpoint-cpp-agent fetches and compiles gRPC; subsequent incremental builds are fast.

Build a single example:

```bash
cmake --build build/debug --target redis_example
```

### Bazel

```bash
bazel build //...                     # everything
bazel build //redis:redis_example     # one target
```

Binaries land at `bazel-bin/<example>/<binary>`. On macOS, `cpptrace_example` is excluded via `target_compatible_with` — use the CMake build instead.

---

## civetweb — HTTP server (C wrapper API)

A CivetWeb HTTP server traced with pinpoint-cpp-agent's **C API** (`pt_*`). Manually constructs the header carriers and calls `pt_agent_new_span_with_reader` / `pt_trace_http_server_request` / `pt_trace_http_server_response`.

**Build & run:**

```bash
# CMake
cmake --build build/debug --target civetweb_example
./build/debug/bin/civetweb_example [pinpoint-config.yaml]

# Bazel
bazel run //civetweb:civetweb_example
```

**Endpoints** (`localhost:8080`):
- `GET /` — landing HTML
- `GET /api/users` — dummy users JSON; response headers are recorded on the span
- `GET /api/health` — health check, also traced

**Test:**

```bash
curl http://localhost:8080/api/users
curl http://localhost:8080/api/health
```

Press Enter on the server console to stop the server.

---

## cpptrace — call-stack capture + cpp-httplib

A cpp-httplib server that attaches a cpptrace-captured call stack to a Pinpoint span via `SetError`. Outgoing HTTP requests carry the trace context injected into the request headers.

**Build & run:**

```bash
# CMake (Bazel disabled on macOS — libunwind incompatibility)
cmake --build build/debug --target cpptrace_example
./build/debug/bin/cpptrace_example
```

Configuration is provided via env vars (defaults to `/tmp/pinpoint-config.yaml`):

```bash
PINPOINT_CPP_CONFIG_FILE=/path/to/config.yaml \
PINPOINT_CPP_APPLICATION_NAME=my-cpptrace-demo \
./build/debug/bin/cpptrace_example
```

**Endpoints** (`localhost:8088`):
- `GET /users/{id}?name=...` — returns the path `id` and the `name` query param as JSON
- `GET /outgoing?host=...&path=...` — issues a cpp-httplib client call to that URL and returns the response. On failure, attaches the cpptrace stack to the span via `SetError`

**Test:**

```bash
curl 'http://localhost:8088/users/123?name=foo'
curl 'http://localhost:8088/outgoing?host=localhost:9000&path=/bar'   # fails -> traced call stack
```

---

## curl — libcurl client + trace context propagation

Issues outbound HTTP requests with libcurl while injecting the Pinpoint trace context into the request headers via `curl_slist_append`.

**Build & run:**

```bash
# CMake
cmake --build build/debug --target curl_web_example
./build/debug/bin/curl_web_example

# Bazel
bazel run //curl:curl_web_example
```

The default target URL is `http://google.com`. Override with an env var:

```bash
TARGET_URL=https://httpbin.org/get ./build/debug/bin/curl_web_example
```

**Endpoint** (`localhost:8091`):
- `GET /run_request[?url=...]` — calls the URL from the `url` query param (or `TARGET_URL`) via cURL

**Test:**

```bash
curl 'http://localhost:8091/run_request?url=https://httpbin.org/get'
```

---

## grpc — gRPC client/server with interceptors

Custom `ClientInterceptor` / `ServerInterceptor` inject and extract the Pinpoint trace context through gRPC metadata. All four RPC patterns (Unary, Server-Streaming, Client-Streaming, Bidi) are traced.

**Build & run:**

```bash
# CMake
cmake --build build/debug --target grpc_server grpc_client

# Terminal 1
./build/debug/bin/grpc_server                       # listens on 0.0.0.0:50051
# Terminal 2
./build/debug/bin/grpc_client

# Bazel (same pattern)
bazel run //grpc:grpc_server
bazel run //grpc:grpc_client
```

**Proto:** [`testapp.proto`](grpc/testapp.proto). Service `grpcdemo.Hello` with four methods.

`grpc_client` runs unary → server-streaming → bidi calls in sequence on startup, then exits.

---

## kafka — librdkafka producer/consumer + header-based trace propagation

The producer publishes a message with the trace context injected into Kafka message headers; the consumer extracts it and starts a new span as a child of the producer's trace.

**Build & run:**

```bash
docker compose up -d kafka

# CMake
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

---

## mongodb — mongocxx with traced CRUD

Each MongoDB operation is wrapped in a SpanEvent that records the collection name and a query description as annotations.

**Build & run:**

```bash
docker compose up -d mongodb

# CMake
cmake --build build/debug --target mongo_example
./build/debug/bin/mongo_example

# Bazel
bazel run //mongodb:mongo_example
```

Override the URI via `MONGO_URI` (default `mongodb://localhost:27017`).

This example runs in batch mode (it is not a server): in db `testdb` / collection `testcollection` it performs Insert → Find → Update → Delete, then exits.

---

## mysql — MySQL Connector/C++ X DevAPI + cpp-httplib

Uses X Protocol (`mysqlx://`, port 33060) for SQL execution; each query is wrapped in a span event. Hitting `/db-demo` runs a SELECT/INSERT/UPDATE/DELETE/transaction sequence.

**Build & run:**

```bash
docker compose up -d mysql

# CMake
cmake --build build/debug --target mysql_example
./build/debug/bin/mysql_example

# Bazel
bazel run //mysql:mysql_example
```

Connection settings can be overridden via env vars (`MYSQL_HOST`, `MYSQL_PORT`, `MYSQL_DATABASE`, `MYSQL_USER`, `MYSQL_PASSWORD`).

**Endpoints** (`localhost:8089`):
- `GET /db-demo` — runs the demo scenario, returns JSON
- `GET /status` — service metadata

**Test:**

```bash
curl http://localhost:8089/status
curl http://localhost:8089/db-demo
```

The initial schema and seed data come from [`mysql/init.sql`](mysql/init.sql), applied on the container's first startup (`demo_users` table).

---

## redis — hiredis command tracing

Each Redis command is wrapped in a SpanEvent with `SERVICE_TYPE_REDIS`.

**Build & run:**

```bash
docker compose up -d redis

# CMake
cmake --build build/debug --target redis_example
./build/debug/bin/redis_example

# Bazel
bazel run //redis:redis_example
```

Host / port overrides: `REDIS_HOST`, `REDIS_PORT`.

This example is also batch-mode: PING → SET → GET → INCR → LPUSH (loop) → LRANGE, then exits.

---

## Troubleshooting

- **The first CMake build is very slow** — pinpoint-cpp-agent uses `FetchContent` to pull and compile gRPC + protobuf + abseil + BoringSSL. It's cached after the first run.
- **`bazel: command not found`** — `brew install bazelisk`. The wrapper reads `.bazelversion` and downloads Bazel 8 automatically.
- **`cpptrace_example` won't build under Bazel on macOS** — cpptrace BCR 1.0.4 forces libunwind, which doesn't compile on macOS. Use the CMake build for this example.
- **MongoDB / MySQL / Kafka connection refused** — verify the service is running (`docker compose ps` shows `Up (healthy)`).
- **Pinpoint traces don't show up in the collector** — confirm the collector is running, and that the IP/port in `PINPOINT_CPP_CONFIG_FILE` (or `pinpoint::SetConfigString`) match.
