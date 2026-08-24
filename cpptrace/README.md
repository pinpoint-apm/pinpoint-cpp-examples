# cpptrace — call-stack capture + cpp-httplib

A cpp-httplib server that attaches a cpptrace-captured call stack to a Pinpoint span via `SetError`. Outgoing HTTP requests carry the trace context injected into the request headers. App name: `cpp-cpptrace-demo`.

**Requires:** nothing beyond the toolchain — cpptrace and cpp-httplib are fetched at build time.

**Bazel does not build this example on macOS:** cpptrace BCR 1.0.4 forces libunwind, which doesn't compile there, so the target is excluded via `target_compatible_with`. Use the CMake build.

Run these from the repository root. See the [top-level README](../README.md) for the shared requirements, build systems, and troubleshooting.

**Build & run:**

```bash
./cpptrace/run.sh             # build + smoke test (stack stays up; `down` stops it)

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

