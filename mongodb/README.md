# mongodb — mongocxx with traced CRUD

Each MongoDB operation is wrapped in a SpanEvent that records the collection name and a query description as annotations. App name: `cpp-mongodb-demo`.

**Requires:** mongo-cxx-driver (`brew install mongo-cxx-driver`, or `libmongocxx-dev` on Linux) and Docker for MongoDB at `localhost:27017`. The CMake build fetches the driver if it isn't installed; the Bazel build links the Homebrew one.

Run these from the repository root. See the [top-level README](../README.md) for the shared requirements, build systems, and troubleshooting.

**Build & run:**

```bash
./mongodb/run.sh              # brings up mongo, runs the example + smoke test

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

