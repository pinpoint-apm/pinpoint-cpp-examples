# pinpoint-cpp-agent C/C++ Examples

Each subdirectory contains a C/C++ application that uses the library named after the directory (CivetWeb, cpp-httplib + cpptrace, libcurl, gRPC, librdkafka, mongocxx, MySQL X DevAPI, hiredis) and traces it with [pinpoint-cpp-agent](https://github.com/pinpoint-apm/pinpoint-cpp-agent). [nginx/](nginx/) is the odd one out: an nginx dynamic module, built by nginx's own build system rather than by CMake or Bazel.

## Requirements

| Requirement | Version |
|---|---|
| Pinpoint Collector | 3.1.0+ |
| C++ Compiler | C++17 (GCC 8+, Clang 6+) |
| Build System | CMake 3.21+ or Bazel 8.0+ |
| OS | Linux, macOS |

Same as [pinpoint-cpp-agent's requirements](https://github.com/pinpoint-apm/pinpoint-cpp-agent#requirements), except for Bazel: the agent builds with 7.0+, while these examples pin 8.0.0 in `.bazelversion`.

Two build systems are supported:

- **CMake** — builds pinpoint-cpp-agent via `FetchContent`. All examples build.
- **Bazel** — uses pinpoint-cpp-agent's own bzlmod build directly.

Each example's own library dependency and backing service — and whether Docker is needed for it — are listed in that example's README.

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

Per-example documentation — requirements, endpoints, build/run commands, env overrides — lives in each directory's `README.md`.

## Pinpoint Collector

Each example sets its own `ApplicationName` in source via `setenv(...)`. The collector host is read from the `PINPOINT_CPP_COLLECTOR_HOST` env var (ports default to `9991`/`9992`/`9993`). The examples still run if no collector is reachable — the trace exports just fail silently — but the agent does require a host to resolve at startup, so set the env var before launching the binary:

```bash
export PINPOINT_CPP_COLLECTOR_HOST=collector.your.network    # or `localhost` for a local collector
./build/debug/bin/redis_example
```

`scripts/_lib.sh` sets `PINPOINT_CPP_COLLECTOR_HOST=localhost` by default for the `<demo>/run.sh` drivers — export your own value before invoking them if you have a real collector elsewhere.

## Build

### Per-example scripts (recommended)

Each example has a `<demo>/run.sh` that handles configure, build, optional `docker run` for its backing service, and a curl-driven smoke test. Every script takes an `up` (default) / `test` / `down` subcommand, the same way the python agent's `examples/<demo>/run.sh` drivers do:

```bash
./redis/run.sh              # up: build, start, wait for the agent, smoke-test
./redis/run.sh test         # re-run the requests against the running stack
./redis/run.sh down         # stop the example processes
```

`up` **leaves the stack running** on purpose. The agent's registration handshake with the collector takes about five seconds, and nothing recorded before it lands ever reaches the collector — so `up` waits for `success to register the agent` in the agent log before sending the first request, and then keeps the processes alive so the spans finish streaming and the endpoints stay hittable while you look at them in the Pinpoint UI. Without a reachable collector the wait times out after 60s with a warning and the smoke test runs anyway.

Each process writes two logs under `$RUN_DIR` (`/tmp/pinpoint-cpp-demo`, override with `PINPOINT_DEMO_RUN_DIR`): `<name>.log` for the example's own output and `<name>.agent.log` for the agent's. The agent gets its own file because its file sink flushes per line, while its stdout is block-buffered as soon as it is redirected — which is what makes the registration poll reliable.

`down` stops the example processes but leaves the containers running (idempotent re-use on the next run). Stop those with `docker rm -f <name>` when you are done. Override the collector or preset via env:

```bash
PINPOINT_CPP_COLLECTOR_HOST=10.0.0.5 PRESET=release ./grpc/run.sh
```

**Load mode:** pass `--load <seconds>` to keep hitting the example's endpoints for that long after the smoke test — useful for producing a steady stream of traces to watch in the Pinpoint UI. Requests fire with a random 50–500ms gap; failures are counted and reported but don't abort the run. `--load 0` disables it, and Ctrl-C stops the load early (partial counts are reported and the stack stays up, same as any other run):

```bash
./redis/run.sh --load 60
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

Binaries land at `bazel-bin/<example>/<binary>`.

### Backing services

The per-example scripts create the necessary container on demand via `docker run` (idempotent: reused if already running). You only need to start a container manually when running a binary outside of the scripts — each example's README carries its own `docker run` line and connection details.

`docker-compose.yml` is retained as an alternative for spinning up all four services (kafka, mongodb, mysql, redis) at once:

```bash
docker compose up -d                  # start all four
docker compose up -d redis            # start one
docker compose down -v                # stop and wipe volumes
```

---

## Platform notes

### macOS

```bash
brew install cmake ninja bazelisk pkg-config
```

That is the whole toolchain. Per-example system libraries are listed in each example's README; CivetWeb, cpptrace, and cpp-httplib are fetched automatically at build time.

**Bazel only:** The Bazel C++ rules in Bazel 8 require a full **Xcode.app** installation (Command Line Tools alone are not enough — Bazel's `xcode-locator` looks for an Xcode bundle and Bazel crashes with `DottedVersion ... got 'None'` otherwise). Install Xcode from the App Store and run:

```bash
sudo xcode-select -s /Applications/Xcode.app/Contents/Developer
bazel clean --expunge       # required if Bazel ran previously under CLT
```

The expunge is necessary because Bazel caches the auto-detected `@local_config_cc` / `@local_config_xcode` repos. Re-using the CLT-era cache against the new Xcode SDK paths produces `absolute path inclusion(s) found in rule ...` errors on every C++ compile.

The CMake path works with Command Line Tools alone; use it if you cannot install Xcode.

The Bazel build links against Homebrew-installed system libraries (`third_party/BUILD.bazel` wraps the Homebrew header repos with `-l<name>` flags, and `.bazelrc` supplies `-L/opt/homebrew/lib`). The CMake build fetches most of them from source instead when they are missing.

### Linux

Install the equivalent development packages with your package manager — each example's README names the one it needs.

---

## Troubleshooting

- **The first CMake build is very slow** — pinpoint-cpp-agent uses `FetchContent` to pull and compile gRPC + protobuf + abseil + BoringSSL. It's cached after the first run.
- **`cmake --build build/debug` produces no example binaries** — all `BUILD_*_EXAMPLE` options default to OFF. Use `./<demo>/run.sh` or `cmake -B build/debug -DBUILD_<NAME>_EXAMPLE=ON` first.
- **Backing service connection refused** — verify the container is running (`docker ps` should show `pinpoint-cpp-examples-<service>`).
- **Pinpoint traces don't show up in the collector** — confirm the collector is reachable from this host and that `PINPOINT_CPP_COLLECTOR_HOST` points to it. `StartAgent()` brings the agent up asynchronously, and `Enable()` only returns true once the collector has accepted the agent's registration; an unreachable collector leaves the agent disabled (it keeps retrying in the background) and spans are dropped rather than delivered. Grep the run script's agent log for the handshake: `grep 'success to register the agent' /tmp/pinpoint-cpp-demo/<name>.agent.log`.
- **`bazel: command not found`** — `brew install bazelisk`. The wrapper reads `.bazelversion` and downloads Bazel 8 automatically.
- **`bazel build` crashes on macOS with `DottedVersion ... got 'None'`** — Bazel 8 needs a full Xcode install. Command Line Tools alone make `xcodebuild` unavailable, Bazel's auto-detected `xcode_config` records `"None"` as the SDK version, and any C++ action then crashes. Install Xcode.app and `sudo xcode-select -s /Applications/Xcode.app/Contents/Developer`, or use the CMake path instead.
- **`absolute path inclusion(s) found in rule '@@abseil-cpp+//...'` after installing Xcode** — Bazel is still using the CLT-era `@local_config_cc` / `@local_config_xcode` caches. Run `bazel clean --expunge` once after the Xcode switch.
- **`fatal error: 'hiredis.h' / 'bsoncxx/...' file not found` under Bazel** — the Bazel build links against Homebrew-installed system libraries. Verify `brew list hiredis librdkafka mongo-cxx-driver mysql-connector-c++` lists the ones your examples need.
