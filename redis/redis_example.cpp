/*
 * Copyright 2025 NAVER Corp.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <iostream>
#include <sstream>
#include <string>
#include <cstdlib>
#include <cstring>
#include <hiredis.h>

// HTTP Server headers
#include "httplib.h"
#include "http_trace_context.h"

#include "pinpoint/tracer.h"

// Global agent and connection settings
pinpoint::AgentPtr g_agent = nullptr;
std::string g_redis_host = "localhost";
int g_redis_port = 6379;
std::string g_redis_endpoint = "localhost:6379";

// Thread local storage for span (same pattern as the mysql example)
thread_local pinpoint::SpanPtr current_span;

void set_span_context(pinpoint::SpanPtr span) { current_span = span; }
pinpoint::SpanPtr get_span_context() { return current_span; }

void execute_command(redisContext *c, pinpoint::SpanPtr span, const char *format, ...) {
    va_list ap;
    void *reply = NULL;

    char command_buf[1024];
    va_start(ap, format);
    vsnprintf(command_buf, sizeof(command_buf), format, ap);
    va_end(ap);

    // Start Span Event for Redis Command; the RAII guard ends it on scope exit.
    pinpoint::helper::ScopedSpanEvent span_event(span, "redis_command", pinpoint::SERVICE_TYPE_REDIS);
    span_event->SetEndPoint(g_redis_endpoint);
    span_event->SetDestination("redis-server");

    // Record the command itself. Redis has no dedicated annotation key, so the
    // generic API slot carries it.
    span_event->SetAnnotation(pinpoint::ANNOTATION_API, command_buf);

    // Re-start va_list for redisCommand
    va_start(ap, format);
    reply = redisvCommand(c, format, ap);
    va_end(ap);

    redisReply *r = (redisReply*)reply;
    if (r == NULL) {
        std::cerr << "Error: " << c->errstr << std::endl;
        span_event->SetError(c->errstr);
    } else {
        if (r->type == REDIS_REPLY_ERROR) {
            std::cerr << "Redis Error: " << r->str << std::endl;
            span_event->SetError(r->str);
        } else {
             std::cout << "Command '" << command_buf << "' executed. ";
             if (r->type == REDIS_REPLY_STRING) {
                 std::cout << "Result: " << r->str << std::endl;
             } else if (r->type == REDIS_REPLY_INTEGER) {
                 std::cout << "Result: " << r->integer << std::endl;
             } else if (r->type == REDIS_REPLY_STATUS) {
                 std::cout << "Status: " << r->str << std::endl;
             } else {
                 std::cout << "Type: " << r->type << std::endl;
             }
        }
        freeReplyObject(reply);
    }
}

// Runs the PING → SET → GET → INCR → LPUSH → LRANGE demo under the given span.
// A redisContext is created per request: it is a single connection and not
// thread-safe, and httplib may serve concurrent requests on different threads.
bool run_redis_demo(pinpoint::SpanPtr span, std::string& error) {
    struct timeval timeout = { 1, 500000 }; // 1.5 seconds
    redisContext *c = redisConnectWithTimeout(g_redis_host.c_str(), g_redis_port, timeout);
    if (c == NULL || c->err) {
        if (c) {
            error = c->errstr;
            redisFree(c);
        } else {
            error = "can't allocate redis context";
        }
        std::cerr << "Connection error: " << error << std::endl;
        return false;
    }

    // PING
    execute_command(c, span, "PING");

    // SET
    execute_command(c, span, "SET %s %s", "foo", "hello world");

    // GET
    execute_command(c, span, "GET foo");

    // INCR
    execute_command(c, span, "INCR counter");
    execute_command(c, span, "INCR counter");

    // Clean up list
    execute_command(c, span, "DEL mylist");

    // LPUSH loop
    for (int j = 0; j < 5; j++) {
        char buf[64];
        snprintf(buf, 64, "%d", j);
        execute_command(c, span, "LPUSH mylist element-%s", buf);
    }

    // LRANGE
    execute_command(c, span, "LRANGE mylist 0 -1");

    redisFree(c);
    return true;
}

// HTTP handler: runs the Redis command demo and reports the outcome as JSON.
void handle_run_demo(const httplib::Request& req, httplib::Response& res) {
    auto span = get_span_context();  // Get span from thread local storage
    // RAII guard: the span event ends when `spanEvent` leaves scope.
    pinpoint::helper::ScopedSpanEvent spanEvent(span, "handle_run_demo");

    std::string error;
    bool success = run_redis_demo(span, error);

    std::stringstream json_response;
    if (success) {
        json_response << "{\n";
        json_response << "  \"success\": true,\n";
        json_response << "  \"redis\": \"" << g_redis_endpoint << "\",\n";
        json_response << "  \"commands\": [\"PING\", \"SET\", \"GET\", \"INCR\", \"DEL\", \"LPUSH\", \"LRANGE\"]\n";
        json_response << "}";
        res.status = 200;
    } else {
        json_response << "{\n";
        json_response << "  \"success\": false,\n";
        json_response << "  \"error\": \"" << error << "\"\n";
        json_response << "}";
        res.status = 503;
        spanEvent->SetError(error);
    }

    res.set_content(json_response.str(), "application/json");
    std::cout << "Request handled: " << req.path << " -> Status: " << res.status << std::endl;
}

pinpoint::SpanPtr trace_request(const httplib::Request& req) {
    HttpHeaderReader request_reader(req.headers);
    auto span = g_agent->NewSpan("RedisDemo", req.path, request_reader);

    auto end_point = req.get_header_value("Host");
    if (end_point.empty()) {
        end_point = req.local_addr + ":" + std::to_string(req.local_port);
    }

    // Records remote address (honoring forwarded headers), endpoint, proxy
    // header annotations and the configured request headers in one call.
    pinpoint::helper::TraceHttpServerRequest(span, req.remote_addr, end_point, request_reader);

    return span;
}

void trace_response(const httplib::Request& req, httplib::Response& res, pinpoint::SpanPtr span) {
    // Records status code, URL stat and the configured response headers.
    HttpHeaderReader http_reader(res.headers);
    pinpoint::helper::TraceHttpServerResponse(span, req.matched_route, req.method, res.status, http_reader);
    span->EndSpan();
}

httplib::Server::Handler
wrap_handler(httplib::Server::Handler handler) {
    return [handler](const httplib::Request& req, httplib::Response& res) {
        auto span = trace_request(req);
        set_span_context(span);  // Store span in thread local storage

        handler(req, res);

        trace_response(req, res, span);
        set_span_context(nullptr);  // Clear span from thread local storage
    };
}

int main(int argc, char **argv) {
    if (const char* env_host = std::getenv("REDIS_HOST")) {
        g_redis_host = env_host;
    }
    if (const char* env_port = std::getenv("REDIS_PORT")) {
        g_redis_port = std::atoi(env_port);
    }
    g_redis_endpoint = g_redis_host + ":" + std::to_string(g_redis_port);

    // Initialize Pinpoint Agent
    setenv("PINPOINT_CPP_APPLICATION_NAME", "cpp-redis-demo", 0);
    if (!pinpoint::StartAgent()) {
        std::cerr << "failed to start the pinpoint agent: check the agent log" << std::endl;
    }
    g_agent = pinpoint::GlobalAgent();

    httplib::Server server;
    server.Get("/run_demo", wrap_handler(handle_run_demo));

    std::cout << "Redis Demo with Pinpoint Tracing (HTTP Server)" << std::endl;
    std::cout << "Redis: " << g_redis_endpoint << std::endl;
    std::cout << "\nHTTP Server starting on http://localhost:8093" << std::endl;
    std::cout << "Endpoints:" << std::endl;
    std::cout << "  GET /run_demo  - Run PING/SET/GET/INCR/LPUSH/LRANGE demo with tracing" << std::endl;
    std::cout << "\nTry:  curl http://localhost:8093/run_demo" << std::endl;
    std::cout << "\nPress Ctrl+C to stop the server..." << std::endl;

    server.listen("0.0.0.0", 8093);

    g_agent->Shutdown();
    return 0;
}
