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

/**
 * @file civetweb_example.c
 * @brief Example demonstrating Pinpoint C API integration with CivetWeb
 */

#include "civetweb.h"
#include <pinpoint/tracer_c.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static pt_agent_t g_agent = NULL;

/* ----- Header carriers backed by CivetWeb's request_info ----- */

/* `get` callback for pt_header_reader_t / pt_context_reader_t — looks up a
 * single header by name on the underlying mg_connection. */
static const char* civetweb_header_get(void* userdata, const char* key) {
    return mg_get_header((struct mg_connection*)userdata, key);
}

/* `for_each` callback for pt_header_reader_t — iterates over all incoming
 * request headers held on the mg_connection. */
static void civetweb_header_for_each(void* userdata,
                                     pt_header_foreach_cb callback,
                                     void* callback_userdata) {
    const struct mg_request_info* req_info =
        mg_get_request_info((struct mg_connection*)userdata);
    if (!req_info) return;
    for (int i = 0; i < req_info->num_headers; ++i) {
        if (callback(req_info->http_headers[i].name,
                     req_info->http_headers[i].value,
                     callback_userdata) != 0) {
            break;
        }
    }
}

/* Static-array carrier for response headers we just sent. */
typedef struct {
    const char* const* pairs;  /* { key, value, key, value, ... NULL } */
    int count;                 /* number of key/value PAIRS, not entries */
} static_header_array;

static const char* static_header_get(void* userdata, const char* key) {
    const static_header_array* arr = (const static_header_array*)userdata;
    for (int i = 0; i < arr->count; ++i) {
        if (strcmp(arr->pairs[i * 2], key) == 0) {
            return arr->pairs[i * 2 + 1];
        }
    }
    return NULL;
}

static void static_header_for_each(void* userdata,
                                   pt_header_foreach_cb callback,
                                   void* callback_userdata) {
    const static_header_array* arr = (const static_header_array*)userdata;
    for (int i = 0; i < arr->count; ++i) {
        if (callback(arr->pairs[i * 2], arr->pairs[i * 2 + 1],
                     callback_userdata) != 0) {
            break;
        }
    }
}

/* ----- Handlers ----- */

static int handle_users(struct mg_connection* conn, void* cbdata) {
    (void)cbdata;
    const struct mg_request_info* req_info = mg_get_request_info(conn);

    pt_context_reader_t ctx_reader = { conn, civetweb_header_get };
    pt_span_t span = pt_agent_new_span_with_reader(
        g_agent, req_info->request_method, req_info->request_uri, &ctx_reader);

    if (span == NULL) {
        const char body[] = "Tracing initialization failed\n";
        mg_printf(conn, "HTTP/1.1 500 Internal Server Error\r\n"
                        "Content-Type: text/plain\r\n"
                        "Content-Length: %d\r\n\r\n%s",
                  (int)(sizeof(body) - 1), body);
        return 500;
    }

    pt_header_reader_t request_reader = {
        conn, civetweb_header_get, civetweb_header_for_each
    };
    char remote_addr[64];
    snprintf(remote_addr, sizeof(remote_addr), "%s:%d",
             req_info->remote_addr, req_info->remote_port);
    pt_trace_http_server_request(span, remote_addr,
                                 req_info->request_uri, &request_reader);

    const char* json_response =
        "[\n"
        "  {\"id\": 1, \"name\": \"Alice\", \"email\": \"alice@example.com\"},\n"
        "  {\"id\": 2, \"name\": \"Bob\", \"email\": \"bob@example.com\"},\n"
        "  {\"id\": 3, \"name\": \"Charlie\", \"email\": \"charlie@example.com\"}\n"
        "]\n";

    mg_printf(conn,
              "HTTP/1.1 200 OK\r\n"
              "Content-Type: application/json\r\n"
              "Content-Length: %d\r\n"
              "X-Powered-By: Pinpoint-CivetWeb\r\n"
              "\r\n"
              "%s",
              (int)strlen(json_response), json_response);

    static const char* response_pairs[] = {
        "Content-Type", "application/json",
        "X-Powered-By", "Pinpoint-CivetWeb",
    };
    static_header_array response_arr = { response_pairs, 2 };
    pt_header_reader_t response_reader = {
        &response_arr, static_header_get, static_header_for_each
    };
    pt_trace_http_server_response(span, req_info->request_uri,
                                  req_info->request_method, 200,
                                  &response_reader);

    pt_span_end(span);
    pt_span_destroy(span);
    return 200;
}

static int handle_health(struct mg_connection* conn, void* cbdata) {
    (void)cbdata;
    const struct mg_request_info* req_info = mg_get_request_info(conn);

    pt_context_reader_t ctx_reader = { conn, civetweb_header_get };
    pt_span_t span = pt_agent_new_span_with_reader(
        g_agent, req_info->request_method, req_info->request_uri, &ctx_reader);

    if (span != NULL) {
        pt_header_reader_t request_reader = {
            conn, civetweb_header_get, civetweb_header_for_each
        };
        char remote_addr[64];
        snprintf(remote_addr, sizeof(remote_addr), "%s:%d",
                 req_info->remote_addr, req_info->remote_port);
        pt_trace_http_server_request(span, remote_addr,
                                     req_info->request_uri, &request_reader);
    }

    const char* response = "{\"status\": \"healthy\", \"service\": \"civetweb-example\"}\n";
    mg_printf(conn,
              "HTTP/1.1 200 OK\r\n"
              "Content-Type: application/json\r\n"
              "Content-Length: %d\r\n"
              "\r\n"
              "%s",
              (int)strlen(response), response);

    if (span != NULL) {
        static const char* response_pairs[] = {
            "Content-Type", "application/json",
        };
        static_header_array response_arr = { response_pairs, 1 };
        pt_header_reader_t response_reader = {
            &response_arr, static_header_get, static_header_for_each
        };
        pt_trace_http_server_response(span, req_info->request_uri,
                                      req_info->request_method, 200,
                                      &response_reader);
        pt_span_end(span);
        pt_span_destroy(span);
    }
    return 200;
}

static int handle_root(struct mg_connection* conn, void* cbdata) {
    (void)cbdata;
    const char* html =
        "<!DOCTYPE html>\n"
        "<html>\n"
        "<head><title>Pinpoint CivetWeb Example</title></head>\n"
        "<body>\n"
        "<h1>Pinpoint CivetWeb Example</h1>\n"
        "<p>Demo server showing the Pinpoint C API with CivetWeb.</p>\n"
        "<h2>Endpoints</h2>\n"
        "<ul>\n"
        "<li><a href=\"/api/users\">/api/users</a></li>\n"
        "<li><a href=\"/api/health\">/api/health</a></li>\n"
        "</ul>\n"
        "</body>\n"
        "</html>\n";
    mg_printf(conn,
              "HTTP/1.1 200 OK\r\n"
              "Content-Type: text/html\r\n"
              "Content-Length: %d\r\n"
              "\r\n"
              "%s",
              (int)strlen(html), html);
    return 200;
}

int main(int argc, char* argv[]) {
    printf("Pinpoint CivetWeb Example Server\n");
    printf("=================================\n\n");

    const char* config_file = (argc > 1) ? argv[1] : "pinpoint-config.yaml";
    pt_set_config_file_path(config_file);

    g_agent = pt_create_agent();
    if (g_agent == NULL) {
        fprintf(stderr, "Warning: Failed to create Pinpoint agent. "
                        "Server will continue without tracing.\n\n");
    } else {
        printf("Pinpoint agent created successfully (config: %s)\n\n", config_file);
    }

    struct mg_callbacks callbacks;
    memset(&callbacks, 0, sizeof(callbacks));

    const char* options[] = {
        "listening_ports", "8080",
        "num_threads", "4",
        NULL
    };

    struct mg_context* ctx = mg_start(&callbacks, NULL, options);
    if (ctx == NULL) {
        fprintf(stderr, "Error: Failed to start CivetWeb server\n");
        if (g_agent != NULL) {
            pt_agent_shutdown(g_agent);
            pt_agent_destroy(g_agent);
        }
        return 1;
    }

    mg_set_request_handler(ctx, "/", handle_root, NULL);
    mg_set_request_handler(ctx, "/api/users", handle_users, NULL);
    mg_set_request_handler(ctx, "/api/health", handle_health, NULL);

    printf("Server started on port 8080\n");
    printf("Press Enter to stop the server...\n");
    getchar();

    printf("\nStopping server...\n");
    mg_stop(ctx);

    if (g_agent != NULL) {
        pt_agent_shutdown(g_agent);
        pt_agent_destroy(g_agent);
        printf("Pinpoint agent shut down\n");
    }
    printf("Server stopped\n");
    return 0;
}
