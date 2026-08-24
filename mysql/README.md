# mysql — MySQL Connector/C++ X DevAPI + cpp-httplib

Uses X Protocol (`mysqlx://`, port 33060) for SQL execution; each query is wrapped in a span event. Hitting `/db-demo` runs a SELECT/INSERT/UPDATE/DELETE/transaction sequence. App name: `cpp-mysql-demo`.

**Requires:** MySQL Connector/C++ X DevAPI (`brew install mysql-connector-c++`, or `libmysqlcppconn-dev` on Linux) and Docker for MySQL at `localhost:3306` (classic) / `localhost:33060` (X Protocol), user `root` / password `pinpoint123`, db `test`. This is the one example with no fetch fallback — CMake skips it with a warning if the connector isn't on the system.

Run these from the repository root. See the [top-level README](../README.md) for the shared requirements, build systems, and troubleshooting.

**Build & run:**

```bash
./mysql/run.sh                # brings up mysql, runs the example + smoke test

# Manual
docker run -d --name pinpoint-cpp-examples-mysql -p 3306:3306 -p 33060:33060 \
    -e MYSQL_ROOT_PASSWORD=pinpoint123 -e MYSQL_DATABASE=test \
    -v "$PWD/mysql/init.sql:/docker-entrypoint-initdb.d/init.sql:ro" \
    mysql:8.0                         # see mysql/run.sh for the full flag set

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

The initial schema and seed data come from [`init.sql`](init.sql), applied on the container's first startup (`demo_users` table).

