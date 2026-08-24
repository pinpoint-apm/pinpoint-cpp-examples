# redis — hiredis command tracing

Each Redis command is wrapped in a SpanEvent with `SERVICE_TYPE_REDIS`. App name: `cpp-redis-demo`.

**Requires:** hiredis (`brew install hiredis`, or `libhiredis-dev` on Linux) and Docker for Redis at `localhost:6379`. The CMake build fetches hiredis if it isn't installed; the Bazel build links the Homebrew one.

Run these from the repository root. See the [top-level README](../README.md) for the shared requirements, build systems, and troubleshooting.

**Build & run:**

```bash
./redis/run.sh                # brings up redis, runs the example + smoke test

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

