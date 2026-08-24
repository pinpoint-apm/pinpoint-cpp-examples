# nginx — dynamic module in nginx's pre-fork worker model

An nginx dynamic module ([`ngx_http_pinpoint_module.c`](ngx_http_pinpoint_module.c)) using the **C API** (`pt_*`), originally from the agent repository's `example/nginx` (removed upstream since). It demonstrates the pre-fork lifecycle contract: the **master** process makes no agent API calls, each **worker** starts its own agent in `init_process` and shuts it down in `exit_process`, and cache manager/loader processes are skipped. One span per completed request is recorded from the LOG phase with the request's real start time and status, honoring incoming `Pinpoint-*` headers. App name: `cpp-nginx-demo`.

With `worker_processes 2` you get two agent instances in the Pinpoint UI, each with its own auto-generated agent id — that is the point of the example, not a misconfiguration.

**Requires:** a C toolchain only — `run.sh` fetches and builds the nginx sources itself. No Docker, and no CMake or Bazel target.

Run these from the repository root. See the [top-level README](../README.md) for the shared requirements, build systems, and troubleshooting.

**Build & run:**

```bash
./nginx/run.sh                # fetch nginx, build module + nginx, smoke test
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

