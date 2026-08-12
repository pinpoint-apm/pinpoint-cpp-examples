#!/usr/bin/env bash
# Build and smoke-test the nginx dynamic module example.
#
# Unlike the other examples this one is not a CMake/Bazel target: an nginx
# module builds with nginx's own build system. This script fetches the nginx
# sources, builds them together with the module against the agent library from
# the CMake build tree, then runs that nginx and traces a few requests.

SCRIPT_NAME="run_nginx"
source "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/_lib.sh"

NGINX_VERSION="${NGINX_VERSION:-1.28.0}"
NGINX_DIR="$BUILD_DIR/nginx"
NGINX_SRC="$NGINX_DIR/nginx-$NGINX_VERSION"
NGINX_PREFIX="$NGINX_DIR/prefix"
NGINX_PORT="${NGINX_PORT:-8095}"

ensure_pinpoint_env
# No example toggle: the module is built by nginx, we only need the agent
# library and headers that every CMake configure produces.
ensure_configure
build_targets pinpoint_cpp

AGENT_INC="$BUILD_DIR/_deps/pinpoint_cpp-src/include"
AGENT_LIB="$BUILD_DIR/_deps/pinpoint_cpp-build"
[ -d "$AGENT_INC" ] || die "agent headers not found at $AGENT_INC"

if [ ! -d "$NGINX_SRC" ]; then
    log "fetching nginx-$NGINX_VERSION sources"
    mkdir -p "$NGINX_DIR"
    curl -fsSL "https://nginx.org/download/nginx-$NGINX_VERSION.tar.gz" \
        | tar xz -C "$NGINX_DIR" || die "failed to fetch nginx-$NGINX_VERSION"
fi

# Rebuild whenever the module source is newer than the installed binary, so
# edits to ngx_http_pinpoint_module.c are picked up without a manual clean.
if [ ! -x "$NGINX_PREFIX/sbin/nginx" ] \
   || [ "$REPO_ROOT/nginx/ngx_http_pinpoint_module.c" -nt "$NGINX_PREFIX/sbin/nginx" ]; then
    log "building nginx-$NGINX_VERSION with ngx_http_pinpoint_module"
    # rpath: the module links against the agent in the CMake build tree, which
    # is not on the default loader path.
    # --without-http_{rewrite,gzip}_module: drops the PCRE2/zlib build deps,
    # neither of which this example needs.
    ( cd "$NGINX_SRC" \
      && ./configure \
            --prefix="$NGINX_PREFIX" \
            --add-dynamic-module="$REPO_ROOT/nginx" \
            --with-cc-opt="-I$AGENT_INC" \
            --with-ld-opt="-L$AGENT_LIB -lpinpoint_cpp -Wl,-rpath,$AGENT_LIB" \
            --without-http_rewrite_module \
            --without-http_gzip_module \
      && make -j"$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 4)" \
      && make install ) >"$NGINX_DIR/build.log" 2>&1 \
        || die "nginx build failed, see $NGINX_DIR/build.log"
fi

# nginx wipes the environment of its workers (except TZ), so the agent is
# configured through a YAML file instead of PINPOINT_CPP_* variables.
cat >"$NGINX_PREFIX/conf/pinpoint.yaml" <<EOF
ApplicationName: "cpp-nginx-demo"
AgentName: "cpp-nginx-demo"
Collector:
  Host: "$PINPOINT_CPP_COLLECTOR_HOST"
EOF

echo '{"message":"hello from nginx"}' >"$NGINX_PREFIX/html/hello.json"

# worker_processes 2: each worker starts its own agent and shows up as its own
# agent instance in the Pinpoint UI — the point of the pre-fork example.
cat >"$NGINX_PREFIX/conf/nginx.conf" <<EOF
load_module modules/ngx_http_pinpoint_module.so;

worker_processes 2;
daemon off;
error_log logs/error.log info;
events { worker_connections 128; }

http {
    pinpoint_enable      on;
    pinpoint_config_file $NGINX_PREFIX/conf/pinpoint.yaml;

    default_type application/json;

    server {
        listen $NGINX_PORT;

        # \`return\` belongs to the rewrite module, which this build leaves out
        # (it would pull in PCRE2), so the 200 comes from a static file.
        location = /api/hello {
            alias $NGINX_PREFIX/html/hello.json;
        }

        # Traced as well — the span carries the 403.
        location = /api/forbidden {
            deny all;
        }
    }
}
EOF

PIDFILE="$(mktemp)"
LOGFILE="$(mktemp)"
trap 'stop_bg "$PIDFILE"; log "nginx log: $LOGFILE, agent log: $NGINX_PREFIX/logs/error.log"' EXIT

start_bg "$PIDFILE" "$LOGFILE" -- "$NGINX_PREFIX/sbin/nginx" -p "$NGINX_PREFIX"
wait_for_http "http://localhost:$NGINX_PORT/api/hello" 30

curl_test "GET /api/hello" "http://localhost:$NGINX_PORT/api/hello"
# Traced as well, with status 403 on the span.
log "test: GET /api/forbidden (expects 403)"
curl -sS --max-time 10 -o /dev/null -w "  http=%{http_code}\n" \
    "http://localhost:$NGINX_PORT/api/forbidden"

load_test "http://localhost:$NGINX_PORT/api/hello" "http://localhost:$NGINX_PORT/api/forbidden"

log "nginx example PASS"
