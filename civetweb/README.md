# civetweb — HTTP server (C wrapper API)

A CivetWeb HTTP server traced with pinpoint-cpp-agent's **C API** (`pt_*`). Manually constructs the header carriers and calls `pt_agent_new_span_with_reader` / `pt_trace_http_server_request` / `pt_span_new_event` / `pt_trace_http_server_response`. App name: `cpp-civetweb-demo`.

**Requires:** nothing beyond the toolchain — CivetWeb is built from source by both build systems (CMake `FetchContent`, Bazel `rules_foreign_cc`).

Run these from the repository root. See the [top-level README](../README.md) for the shared requirements, build systems, and troubleshooting.

**Build & run:**

```bash
./civetweb/run.sh             # build + smoke test (stack stays up; `down` stops it)

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

