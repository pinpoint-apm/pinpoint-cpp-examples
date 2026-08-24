# grpc — gRPC client/server with interceptors

Custom `ClientInterceptor` / `ServerInterceptor` inject and extract the Pinpoint trace context through gRPC metadata. All four RPC patterns (Unary, Server-Streaming, Client-Streaming, Bidi) are traced. App names: `cpp-grpc-server-demo`, `cpp-grpc-client-demo`.

**Requires:** gRPC and Protobuf — supplied by the build itself (the agent's CMake `FetchContent`, or bzlmod under Bazel). No system install needed.

Run these from the repository root. See the [top-level README](../README.md) for the shared requirements, build systems, and troubleshooting.

**Build & run:**

```bash
./grpc/run.sh                 # starts server + client, smoke-tests, leaves both up

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

**Proto:** [`testapp.proto`](testapp.proto). Service `grpcdemo.Hello` with four methods.

The gRPC server address can be overridden via `GRPC_TARGET` (default `localhost:50051`).

**Client endpoint** (`localhost:8094`):
- `GET /run_client` — runs unary → server-streaming → bidi calls against the gRPC server, returns JSON

**Test:**

```bash
curl http://localhost:8094/run_client
```

