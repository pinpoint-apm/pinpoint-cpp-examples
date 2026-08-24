# curl — libcurl client + trace context propagation

Issues outbound HTTP requests with libcurl while injecting the Pinpoint trace context into the request headers via `curl_slist_append`. App name: `cpp-curl-demo`.

**Requires:** libcurl — preinstalled on macOS (it ships with the SDK); on Linux install `libcurl4-openssl-dev` or your distro's equivalent. The CMake build fetches it if it isn't found.

Run these from the repository root. See the [top-level README](../README.md) for the shared requirements, build systems, and troubleshooting.

**Build & run:**

```bash
./curl/run.sh                 # build + smoke test (stack stays up; `down` stops it)

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

