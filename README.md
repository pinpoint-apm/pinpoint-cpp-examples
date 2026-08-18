# pinpoint-cpp-agent C/C++ Examples

Each subdirectory contains a C/C++ application that uses the library named after the directory (CivetWeb, cpp-httplib + cpptrace, libcurl, gRPC, librdkafka, mongocxx, MySQL X DevAPI, hiredis) and traces it with [pinpoint-cpp-agent](https://github.com/pinpoint-apm/pinpoint-cpp-agent). [nginx/](nginx/) is the odd one out: an nginx dynamic module, built by nginx's own build system rather than by CMake or Bazel.

Two build systems are supported:

- **CMake** — builds pinpoint-cpp-agent via `FetchContent`. All examples build (macOS / Linux).
- **Bazel** — uses pinpoint-cpp-agent's own bzlmod build directly. Requires Bazel 8. On macOS, `cpptrace_example` is excluded (libunwind compatibility).

## Layout

| Directory | Library | Binaries |
|-----------|---------|----------|
| [civetweb/](civetweb/)   | [CivetWeb](https://github.com/civetweb/civetweb) (HTTP server, C)        | `civetweb_example`                  |
| [cpptrace/](cpptrace/)   | [cpptrace](https://github.com/jeremy-rifkin/cpptrace) + cpp-httplib      | `cpptrace_example`                  |
| [curl/](curl/)           | [libcurl](https://curl.se/libcurl/) + cpp-httplib                        | `curl_web_example`                  |
| [grpc/](grpc/)           | [gRPC](https://grpc.io/) + cpp-httplib                                    | `grpc_client`, `grpc_server`        |
| [kafka/](kafka/)         | [librdkafka](https://github.com/confluentinc/librdkafka) + cpp-httplib   | `kafka_web_producer`, `kafka_consumer` |
| [mongodb/](mongodb/)     | [mongo-cxx-driver](https://github.com/mongodb/mongo-cxx-driver) + cpp-httplib | `mongo_example`                |
| [mysql/](mysql/)         | [MySQL Connector/C++ X DevAPI](https://dev.mysql.com/doc/connector-cpp/) + cpp-httplib | `mysql_example` |
| [nginx/](nginx/)         | [nginx](https://nginx.org/) dynamic module (C)                           | `ngx_http_pinpoint_module.so`       |
| [redis/](redis/)         | [hiredis](https://github.com/redis/hiredis) + cpp-httplib                | `redis_example`                     |

## Prerequisites

### System libraries (macOS / Homebrew)

```bash
brew install cmake ninja bazelisk pkg-config
brew install hiredis librdkafka mongo-cxx-driver mysql-connector-c++
# civetweb, cpptrace, and cpp-httplib are fetched automatically at build time.
```

On a Linux distro, install the equivalent packages with your package manager (`libcurl4-openssl-dev`, `libhiredis-dev`, `librdkafka-dev`, `libmongocxx-dev`, `libmysqlcppconn-dev`, etc.).

**macOS — Bazel only:** The Bazel C++ rules in Bazel 8 require a full **Xcode.app** installation (Command Line Tools alone are not enough — Bazel's `xcode-locator` looks for an Xcode bundle and Bazel crashes with `DottedVersion ... got 'None'` otherwise). Install Xcode from the App Store and run:

```bash
sudo xcode-select -s /Applications/Xcode.app/Contents/Developer
bazel clean --expunge       # required if Bazel ran previously under CLT
```

The expunge is necessary because Bazel caches the auto-detected `@local_config_cc` / `@local_config_xcode` repos. Re-using the CLT-era cache against the new Xcode SDK paths produces `absolute path inclusion(s) found in rule ...` errors on every C++ compile.

The CMake path works with Command Line Tools alone; use it if you cannot install Xcode.

### Pinpoint Collector

Each example sets its own `ApplicationName` in source via `setenv(...)`. The collector host is read from the `PINPOINT_CPP_COLLECTOR_HOST` env var (ports default to `9991`/`9992`/`9993`). The examples still run if no collector is reachable — the trace exports just fail silently — but the agent does require a host to resolve at startup, so set the env var before launching the binary:

```bash
export PINPOINT_CPP_COLLECTOR_HOST=collector.your.network    # or `localhost` for a local collector
./build/debug/bin/redis_example
```

`scripts/_lib.sh` sets `PINPOINT_CPP_COLLECTOR_HOST=localhost` by default for the `scripts/run_*.sh` helpers — export your own value before invoking them if you have a real collector elsewhere.

### Backing services (kafka / mongodb / mysql / redis)

The per-example scripts under `scripts/` create the necessary container on demand via `docker run` (idempotent: reused if already running). You only need to start a container manually when running a binary outside of the scripts.

`docker-compose.yml` is retained as an alternative for spinning up all four services at once:

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

### Per-example scripts (recommended)

Each example has a `scripts/run_<name>.sh` that handles configure, build, optional `docker run` for its backing service, and a curl-driven smoke test:

```bash
./scripts/run_civetweb.sh
./scripts/run_cpptrace.sh
./scripts/run_curl.sh
./scripts/run_grpc.sh
./scripts/run_kafka.sh         # also brings up the kafka container
./scripts/run_mongodb.sh       # also brings up the mongodb container
./scripts/run_mysql.sh         # also brings up the mysql container
./scripts/run_nginx.sh         # also fetches and builds the nginx sources
./scripts/run_redis.sh         # also brings up the redis container
```

Containers are left running after the script exits (idempotent re-use on the next run). Stop them with `docker rm -f <name>` when you are done. Override the collector or preset via env:

```bash
PINPOINT_CPP_COLLECTOR_HOST=10.0.0.5 PRESET=release ./scripts/run_grpc.sh
```

**Load mode:** pass `--load <seconds>` to keep hitting the example's endpoints for that long after the smoke test — useful for producing a steady stream of traces to watch in the Pinpoint UI. Requests fire with a random 50–500ms gap; failures are counted and reported but don't abort the run. `--load 0` disables it, and Ctrl-C stops the load early (partial counts are reported and the servers are still torn down cleanly):

```bash
./scripts/run_redis.sh --load 60
```

### CMake

```bash
cmake --preset debug                  # or --preset release
cmake --build build/debug
```

All `BUILD_*_EXAMPLE` options default to **OFF**, so the bare configure+build produces no example binaries. Enable the ones you want and rebuild:

```bash
cmake -B build/debug -DBUILD_REDIS_EXAMPLE=ON
cmake --build build/debug --target redis_example
```

Binaries land in `build/debug/bin/`. The first configure takes ~5–10 minutes because pinpoint-cpp-agent fetches and compiles gRPC; subsequent incremental builds are fast.

### Bazel

```bash
bazel build //...                     # everything
bazel build //redis:redis_example     # one target
```

Binaries land at `bazel-bin/<example>/<binary>`. On macOS, `cpptrace_example` is excluded via `target_compatible_with` — use the CMake build instead.

---

## civetweb — HTTP server (C wrapper API)

A CivetWeb HTTP server traced with pinpoint-cpp-agent's **C API** (`pt_*`). Manually constructs the header carriers and calls `pt_agent_new_span_with_reader` / `pt_trace_http_server_request` / `pt_trace_http_server_response`. App name: `cpp-civetweb-demo`.

**Build & run:**

```bash
./scripts/run_civetweb.sh             # build + smoke test

# Manual CMake
cmake -B build/debug -DBUILD_CIVETWEB_EXAMPLE=ON
cmake --build build/debug --target civetweb_example
./build/debug/bin/civetweb_example

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

A cpp-httplib server that attaches a cpptrace-captured call stack to a Pinpoint span via `SetError`. Outgoing HTTP requests carry the trace context injected into the request headers. App name: `cpp-cpptrace-demo`.

**Build & run:**

```bash
./scripts/run_cpptrace.sh             # build + smoke test

# Manual CMake (Bazel disabled on macOS — libunwind incompatibility)
cmake -B build/debug -DBUILD_CPPTRACE_EXAMPLE=ON
cmake --build build/debug --target cpptrace_example
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

Issues outbound HTTP requests with libcurl while injecting the Pinpoint trace context into the request headers via `curl_slist_append`. App name: `cpp-curl-demo`.

**Build & run:**

```bash
./scripts/run_curl.sh                 # build + smoke test

# Manual CMake
cmake -B build/debug -DBUILD_CURL_EXAMPLE=ON
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

Custom `ClientInterceptor` / `ServerInterceptor` inject and extract the Pinpoint trace context through gRPC metadata. All four RPC patterns (Unary, Server-Streaming, Client-Streaming, Bidi) are traced. App names: `grpc-server-demo`, `grpc-client-demo`.

**Build & run:**

```bash
./scripts/run_grpc.sh                 # starts server + client, smoke-tests, tears down

# Manual CMake
cmake -B build/debug -DBUILD_GRPC_EXAMPLE=ON
cmake --build build/debug --target grpc_server grpc_client

# Terminal 1
./build/debug/bin/grpc_server                       # listens on 0.0.0.0:50051
# Terminal 2: client (HTTP server on port 8094)
./build/debug/bin/grpc_client

# Bazel (same pattern)
bazel run //grpc:grpc_server
bazel run //grpc:grpc_client
```

**Proto:** [`testapp.proto`](grpc/testapp.proto). Service `grpcdemo.Hello` with four methods.

The gRPC server address can be overridden via `GRPC_TARGET` (default `localhost:50051`).

**Client endpoint** (`localhost:8094`):
- `GET /run_client` — runs unary → server-streaming → bidi calls against the gRPC server, returns JSON

**Test:**

```bash
curl http://localhost:8094/run_client
```

---

## kafka — librdkafka producer/consumer + header-based trace propagation

The producer publishes a message with the trace context injected into Kafka message headers; the consumer extracts it and starts a new span as a child of the producer's trace. App names: `cpp-kafka-producer-demo`, `cpp-kafka-consumer-demo`.

**Build & run:**

```bash
./scripts/run_kafka.sh                # brings up kafka, runs both binaries, verifies the message

# Manual
docker run -d --name pinpoint-cpp-examples-kafka -p 9092:9092 \
    -e KAFKA_NODE_ID=1 -e KAFKA_PROCESS_ROLES=broker,controller \
    -e KAFKA_ADVERTISED_LISTENERS=PLAINTEXT://localhost:9092 \
    apache/kafka:3.8.0                # see scripts/run_kafka.sh for the full flag set

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

---

## mongodb — mongocxx with traced CRUD

Each MongoDB operation is wrapped in a SpanEvent that records the collection name and a query description as annotations. App name: `cpp-mongodb-demo`.

**Build & run:**

```bash
./scripts/run_mongodb.sh              # brings up mongo, runs the example + smoke test

# Manual
docker run -d --name pinpoint-cpp-examples-mongodb -p 27017:27017 mongo:7

cmake -B build/debug -DBUILD_MONGODB_EXAMPLE=ON
cmake --build build/debug --target mongo_example
./build/debug/bin/mongo_example

# Bazel
bazel run //mongodb:mongo_example
```

Override the URI via `MONGO_URI` (default `mongodb://localhost:27017`).

**Endpoint** (`localhost:8092`):
- `GET /run_demo` — in db `testdb` / collection `testcollection`, performs Insert → Find → Update → Delete, returns JSON

**Test:**

```bash
curl http://localhost:8092/run_demo
```

---

## mysql — MySQL Connector/C++ X DevAPI + cpp-httplib

Uses X Protocol (`mysqlx://`, port 33060) for SQL execution; each query is wrapped in a span event. Hitting `/db-demo` runs a SELECT/INSERT/UPDATE/DELETE/transaction sequence. App name: `cpp-db-demo`.

**Build & run:**

```bash
./scripts/run_mysql.sh                # brings up mysql, runs the example + smoke test

# Manual
docker run -d --name pinpoint-cpp-examples-mysql -p 3306:3306 -p 33060:33060 \
    -e MYSQL_ROOT_PASSWORD=pinpoint123 -e MYSQL_DATABASE=test \
    -v "$PWD/mysql/init.sql:/docker-entrypoint-initdb.d/init.sql:ro" \
    mysql:8.0                         # see scripts/run_mysql.sh for the full flag set

cmake -B build/debug -DBUILD_MYSQL_EXAMPLE=ON
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

## nginx — dynamic module in nginx's pre-fork worker model

An nginx dynamic module ([`nginx/ngx_http_pinpoint_module.c`](nginx/ngx_http_pinpoint_module.c)) using the **C API** (`pt_*`), originally from the agent repository's `example/nginx` (removed upstream since). It demonstrates the pre-fork lifecycle contract: the **master** process makes no agent API calls, each **worker** starts its own agent in `init_process` and shuts it down in `exit_process`, and cache manager/loader processes are skipped. One span per completed request is recorded from the LOG phase with the request's real start time and status, honoring incoming `Pinpoint-*` headers. App name: `cpp-nginx-demo`.

With `worker_processes 2` you get two agent instances in the Pinpoint UI, each with its own auto-generated agent id — that is the point of the example, not a misconfiguration.

**Build & run:**

```bash
./scripts/run_nginx.sh                # fetch nginx, build module + nginx, smoke test
```

The script builds the agent library through the normal CMake configure, then hands nginx's `configure` the module directory. There is no CMake or Bazel target — nginx modules are built by nginx. Everything lands under `build/debug/nginx/` (sources, prefix, `build.log`) and is reused on the next run; the module is relinked whenever `ngx_http_pinpoint_module.c` changes. Override the version or port with `NGINX_VERSION` / `NGINX_PORT`.

Manual build against your own nginx sources:

```bash
cd /path/to/nginx-src
./configure --add-dynamic-module=/path/to/pinpoint-cpp-examples/nginx \
            --with-cc-opt="-I/path/to/agent/include" \
            --with-ld-opt="-L/path/to/agent/lib -lpinpoint_cpp"
make modules
```

```nginx
load_module modules/ngx_http_pinpoint_module.so;

http {
    pinpoint_enable      on;
    pinpoint_config_file /etc/pinpoint/nginx-agent.yaml;
}
```

nginx wipes the environment of its workers (everything but `TZ`), so `PINPOINT_CPP_*` variables do not reach the agent — configure it through `pinpoint_config_file`, or declare each variable with nginx's `env` directive. Give each worker its own log file (`Log.FilePath: ".../nginx-agent-%pid%.log"`); the built-in rotation is not multi-process safe.

**Endpoints** (`localhost:8095`, generated config):
- `GET /api/hello` — static JSON, traced with status 200
- `GET /api/forbidden` — `deny all`, traced with status 403

**Test:**

```bash
curl http://localhost:8095/api/hello
curl -i http://localhost:8095/api/forbidden
```

The script's nginx is built `--without-http_rewrite_module --without-http_gzip_module` to avoid the PCRE2/zlib build dependencies, so the demo config serves a static file instead of using `return 200`.

**Deliberately out of scope** (see the module's header comment): injecting `Pinpoint-*` headers into proxied upstream requests, copying header values out of nginx storage for HTTP/2/3, span events for upstream calls, and per-location URL templates.

---

## redis — hiredis command tracing

Each Redis command is wrapped in a SpanEvent with `SERVICE_TYPE_REDIS`. App name: `cpp-redis-demo`.

**Build & run:**

```bash
./scripts/run_redis.sh                # brings up redis, runs the example + smoke test

# Manual
docker run -d --name pinpoint-cpp-examples-redis -p 6379:6379 redis:7

cmake -B build/debug -DBUILD_REDIS_EXAMPLE=ON
cmake --build build/debug --target redis_example
./build/debug/bin/redis_example

# Bazel
bazel run //redis:redis_example
```

Host / port overrides: `REDIS_HOST`, `REDIS_PORT`.

**Endpoint** (`localhost:8093`):
- `GET /run_demo` — runs PING → SET → GET → INCR → LPUSH (loop) → LRANGE, returns JSON

**Test:**

```bash
curl http://localhost:8093/run_demo
```

---

## Troubleshooting

- **The first CMake build is very slow** — pinpoint-cpp-agent uses `FetchContent` to pull and compile gRPC + protobuf + abseil + BoringSSL. It's cached after the first run.
- **`bazel: command not found`** — `brew install bazelisk`. The wrapper reads `.bazelversion` and downloads Bazel 8 automatically.
- **`bazel build` crashes on macOS with `DottedVersion ... got 'None'`** — Bazel 8 needs a full Xcode install. Command Line Tools alone make `xcodebuild` unavailable, Bazel's auto-detected `xcode_config` records `"None"` as the SDK version, and any C++ action then crashes. Install Xcode.app and `sudo xcode-select -s /Applications/Xcode.app/Contents/Developer`, or use the CMake path instead.
- **`absolute path inclusion(s) found in rule '@@abseil-cpp+//...'` after installing Xcode** — Bazel is still using the CLT-era `@local_config_cc` / `@local_config_xcode` caches. Run `bazel clean --expunge` once after the Xcode switch.
- **`fatal error: 'hiredis.h' / 'bsoncxx/...' file not found` under Bazel** — the Bazel build links against Homebrew-installed system libraries. Verify `brew list hiredis librdkafka mongo-cxx-driver mysql-connector-c++` lists all four (re-run the `brew install` line in Prerequisites if any are missing).
- **`cpptrace_example` won't build under Bazel on macOS** — cpptrace BCR 1.0.4 forces libunwind, which doesn't compile on macOS. Use the CMake build for this example.
- **MongoDB / MySQL / Kafka connection refused** — verify the container is running (`docker ps` should show `pinpoint-cpp-examples-<service>`).
- **Pinpoint traces don't show up in the collector** — confirm the collector is reachable from this host and that `PINPOINT_CPP_COLLECTOR_HOST` points to it. `StartAgent()` brings the agent up asynchronously, and `Enable()` only returns true once the collector has accepted the agent's registration; an unreachable collector leaves the agent disabled (it keeps retrying in the background) and spans are dropped rather than delivered.
- **`cmake --build build/debug` produces no example binaries** — all `BUILD_*_EXAMPLE` options default to OFF. Use `./scripts/run_<name>.sh` or `cmake -B build/debug -DBUILD_<NAME>_EXAMPLE=ON` first.
