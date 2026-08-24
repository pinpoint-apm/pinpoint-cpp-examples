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

#include <cctype>
#include <iostream>
#include <string>
#include <cstdlib>
#include <curl/curl.h>

#include "pinpoint/tracer.h"
#include "httplib.h"
#include "http_trace_context.h"

// Global variables
pinpoint::AgentPtr g_agent = nullptr;
std::string g_target_url = "http://google.com";

// Trace Context Writer adapter for libcurl
class CurlTraceContextWriter : public pinpoint::TraceContextWriter {
public:
    explicit CurlTraceContextWriter(struct curl_slist** headers) : headers_(headers) {}

    void Set(std::string_view key, std::string_view value) override {
        std::string header = std::string(key) + ": " + std::string(value);
        *headers_ = curl_slist_append(*headers_, header.c_str());
    }

private:
    struct curl_slist** headers_;
};

// HeaderReader adapter over a curl_slist of "Key: value" lines, so the
// outgoing headers can be handed to the agent's HTTP client trace helpers.
class CurlHeaderReader final : public pinpoint::HeaderReader {
public:
    explicit CurlHeaderReader(const struct curl_slist* headers) : headers_(headers) {}

    std::optional<std::string_view> Get(std::string_view key) const override {
        for (const struct curl_slist* item = headers_; item != nullptr; item = item->next) {
            std::string_view line(item->data);
            auto sep = line.find(':');
            if (sep == std::string_view::npos) {
                continue;
            }
            auto name = line.substr(0, sep);
            // HTTP field names are case-insensitive.
            if (name.size() == key.size() &&
                std::equal(name.begin(), name.end(), key.begin(), [](char a, char b) {
                    return std::tolower(static_cast<unsigned char>(a)) ==
                           std::tolower(static_cast<unsigned char>(b));
                })) {
                return trim_value(line.substr(sep + 1));
            }
        }
        return std::nullopt;
    }

    void ForEach(std::function<bool(std::string_view, std::string_view)> callback) const override {
        for (const struct curl_slist* item = headers_; item != nullptr; item = item->next) {
            std::string_view line(item->data);
            auto sep = line.find(':');
            if (sep == std::string_view::npos) {
                continue;
            }
            if (!callback(line.substr(0, sep), trim_value(line.substr(sep + 1)))) {
                break;
            }
        }
    }

private:
    static std::string_view trim_value(std::string_view value) {
        while (!value.empty() && value.front() == ' ') {
            value.remove_prefix(1);
        }
        return value;
    }

    const struct curl_slist* headers_;
};

// Extracts "host[:port]" from a URL for the span event's endpoint/destination.
static std::string extract_host(const std::string& url) {
    auto start = url.find("://");
    start = (start == std::string::npos) ? 0 : start + 3;
    auto end = url.find('/', start);
    return url.substr(start, end == std::string::npos ? std::string::npos : end - start);
}

// Write callback for cURL to capture response body (optional)
size_t WriteCallback(void* contents, size_t size, size_t nmemb, void* userp) {
    ((std::string*)userp)->append((char*)contents, size * nmemb);
    return size * nmemb;
}

bool perform_curl_request(const std::string& url, pinpoint::SpanPtr span, std::string& out_response) {
    CURL* curl;
    CURLcode res;
    struct curl_slist* headers = NULL;

    // Create SpanEvent for the outgoing HTTP call; the RAII guard ends it on
    // every exit path (including the early error return below).
    pinpoint::helper::ScopedSpanEvent se(span, "execute_curl", pinpoint::SERVICE_TYPE_CPP_HTTP_CLIENT);

    curl = curl_easy_init();
    if(curl) {
        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &out_response);

        // Inject Trace Context headers
        CurlTraceContextWriter writer(&headers);
        se->InjectContext(writer);

        // Records service type, endpoint/destination, the URL annotation and
        // the configured request headers in one call.
        CurlHeaderReader request_reader(headers);
        pinpoint::helper::TraceHttpClientRequest(se.value(), extract_host(url), url, request_reader);

        // Add custom headers to cURL
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

        // Perform the request
        res = curl_easy_perform(curl);
        
        long response_code = 0;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response_code);

        if(res != CURLE_OK) {
            std::cerr << "curl_easy_perform() failed: " << curl_easy_strerror(res) << std::endl;
            se->SetError(curl_easy_strerror(res));

            curl_easy_cleanup(curl);
            curl_slist_free_all(headers);
            return false;
        }
        
        // Records the status code annotation and the configured response
        // headers (this example does not capture response headers).
        CurlHeaderReader response_reader(nullptr);
        pinpoint::helper::TraceHttpClientResponse(se.value(), (int)response_code, response_reader);

        // Clean up
        curl_easy_cleanup(curl);
        curl_slist_free_all(headers);
    }

    return true;
}

void handle_run_request(const httplib::Request& req, httplib::Response& res) {
    // Extract trace context from HTTP request headers
    HttpHeaderReader request_reader(req.headers);
    auto span = g_agent->NewSpan("CurlWebExample", "/run_request", request_reader);

    // Records remote address (honoring forwarded headers), endpoint, proxy
    // header annotations and the configured request headers in one call.
    pinpoint::helper::TraceHttpServerRequest(span, req.remote_addr, req.get_header_value("Host"), request_reader);

    // Determine target URL
    std::string target = g_target_url;
    if (req.has_param("url")) {
        target = req.get_param_value("url");
    }

    std::string response_body;
    bool success = perform_curl_request(target, span, response_body);

    if (success) {
        res.status = 200;
        res.set_content("cURL request successful\nResponse body preview: " + response_body.substr(0, 200), "text/plain");
    } else {
        res.status = 500;
        res.set_content("cURL request failed", "text/plain");
        span->SetError("cURL request failed");
    }

    // Records status code, URL stat and the configured response headers.
    HttpHeaderReader response_reader(res.headers);
    pinpoint::helper::TraceHttpServerResponse(span, req.matched_route, req.method, res.status, response_reader);
    span->EndSpan();
}

int main(int argc, char* argv[]) {
    if (const char* env_url = std::getenv("TARGET_URL")) {
        g_target_url = env_url;
    }

    // Pinpoint Agent Init
    setenv("PINPOINT_CPP_APPLICATION_NAME", "cpp-curl-demo", 0);
    if (!pinpoint::StartAgent()) {
        std::cerr << "failed to start the pinpoint agent: check the agent log" << std::endl;
    }
    g_agent = pinpoint::GlobalAgent();

    httplib::Server server;
    server.Get("/run_request", handle_run_request);

    std::cout << "Curl Web Example starting on http://localhost:8091" << std::endl;
    std::cout << "Default target URL: " << g_target_url << std::endl;
    
    server.listen("0.0.0.0", 8091);
    
    g_agent->Shutdown();
    return 0;
}
