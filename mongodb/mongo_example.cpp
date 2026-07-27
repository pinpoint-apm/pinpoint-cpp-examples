#include <iostream>
#include <sstream>
#include <string>
#include <cstdlib>

#include <bsoncxx/builder/stream/document.hpp>
#include <bsoncxx/json.hpp>
#include <mongocxx/client.hpp>
#include <mongocxx/instance.hpp>
#include <mongocxx/uri.hpp>

// HTTP Server headers
#include "httplib.h"
#include "http_trace_context.h"

#include "pinpoint/tracer.h"

// Global agent and connection settings
pinpoint::AgentPtr g_agent = nullptr;
std::string g_mongo_uri = "mongodb://localhost:27017";

// Thread local storage for span (same pattern as the mysql example)
thread_local pinpoint::SpanPtr current_span;

void set_span_context(pinpoint::SpanPtr span) { current_span = span; }
pinpoint::SpanPtr get_span_context() { return current_span; }

// Helper to trace MongoDB operations
template <typename Func>
void trace_mongo_op(pinpoint::SpanPtr span, const std::string& operation_name, const std::string& collection_name, const std::string& query_desc, Func func) {
    // RAII guard: the span event ends when `se` leaves scope, even if func() throws.
    pinpoint::helper::ScopedSpanEvent se(span, operation_name, pinpoint::SERVICE_TYPE_MONGODB_QUERY);
    se->SetEndPoint("localhost:27017"); // Ideally, get this from URI
    se->SetDestination("mongodb-demo");

    // Record collection and query details
    // Pinpoint MongoDB plugin usually records collection info in specific annotations
    // Since we don't have those exact constants exposed in the basic header, we'll use SQL/Generic fields
    // or custom annotations if available. For now, we put collection info in description or as SQL.
    std::string full_desc = "Collection: " + collection_name + ", Query: " + query_desc;
    se->SetSqlQuery(full_desc, {});

    try {
        func();
    } catch (const std::exception& e) {
        se->SetError(e.what());
        std::cerr << "Error in " << operation_name << ": " << e.what() << std::endl;
    }
}

// Runs the Insert → Find → Update → Delete demo under the given span.
// A mongocxx::client is created per request: it is not thread-safe, and
// httplib may serve concurrent requests on different threads.
bool run_mongo_demo(pinpoint::SpanPtr span, std::string& error) {
    try {
        mongocxx::uri uri(g_mongo_uri);
        mongocxx::client client(uri);

        auto db = client["testdb"];
        auto collection = db["testcollection"];

        // 1. Insert
        trace_mongo_op(span, "Insert", "testcollection", "Insert {name: 'MongoDB', type: 'database'}", [&]() {
            bsoncxx::builder::stream::document document{};
            document << "name" << "MongoDB"
                     << "type" << "database"
                     << "count" << 1
                     << "info" << bsoncxx::builder::stream::open_document
                        << "x" << 203
                        << "y" << 102
                     << bsoncxx::builder::stream::close_document;

            auto result = collection.insert_one(document.view());
            if (result) {
                std::cout << "Inserted document with id: " << result->inserted_id().get_oid().value.to_string() << std::endl;
            }
        });

        // 2. Find
        trace_mongo_op(span, "Find", "testcollection", "Find {name: 'MongoDB'}", [&]() {
            bsoncxx::builder::stream::document filter_builder{};
            filter_builder << "name" << "MongoDB";

            auto cursor = collection.find(filter_builder.view());
            for (auto&& doc : cursor) {
                std::cout << "Found document: " << bsoncxx::to_json(doc) << std::endl;
            }
        });

        // 3. Update
        trace_mongo_op(span, "Update", "testcollection", "Update {name: 'MongoDB'} set {i: 110}", [&]() {
            bsoncxx::builder::stream::document filter_builder{};
            filter_builder << "name" << "MongoDB";

            bsoncxx::builder::stream::document update_builder{};
            update_builder << "$set" << bsoncxx::builder::stream::open_document
                           << "i" << 110
                           << bsoncxx::builder::stream::close_document;

            auto result = collection.update_one(filter_builder.view(), update_builder.view());
            if (result) {
                 std::cout << "Matched " << result->matched_count() << " documents, modified " << result->modified_count() << std::endl;
            }
        });

        // 4. Delete
        trace_mongo_op(span, "Delete", "testcollection", "Delete {name: 'MongoDB'}", [&]() {
            bsoncxx::builder::stream::document filter_builder{};
            filter_builder << "name" << "MongoDB";

            auto result = collection.delete_one(filter_builder.view());
            if (result) {
                std::cout << "Deleted " << result->deleted_count() << " documents" << std::endl;
            }
        });

        return true;
    } catch (const std::exception& e) {
        error = e.what();
        std::cerr << "Exception: " << e.what() << std::endl;
        return false;
    }
}

// HTTP handler: runs the CRUD demo and reports the outcome as JSON.
void handle_run_demo(const httplib::Request& req, httplib::Response& res) {
    auto span = get_span_context();  // Get span from thread local storage
    // RAII guard: the span event ends when `spanEvent` leaves scope.
    pinpoint::helper::ScopedSpanEvent spanEvent(span, "handle_run_demo");

    std::string error;
    bool success = run_mongo_demo(span, error);

    std::stringstream json_response;
    if (success) {
        json_response << "{\n";
        json_response << "  \"success\": true,\n";
        json_response << "  \"uri\": \"" << g_mongo_uri << "\",\n";
        json_response << "  \"operations\": [\"Insert\", \"Find\", \"Update\", \"Delete\"]\n";
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
    auto span = g_agent->NewSpan("MongoDBDemo", req.path, request_reader);

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

int main(int argc, char* argv[]) {
    if (const char* env_uri = std::getenv("MONGO_URI")) {
        g_mongo_uri = env_uri;
    }

    // Pinpoint Agent Init
    setenv("PINPOINT_CPP_APPLICATION_NAME", "cpp-mongodb-demo", 0);
    g_agent = pinpoint::CreateAgent();
    g_agent->Start();

    // MongoDB C++ Driver Init (must be constructed once per process and
    // outlive every client created from it)
    mongocxx::instance instance{};

    httplib::Server server;
    server.Get("/run_demo", wrap_handler(handle_run_demo));

    std::cout << "MongoDB Demo with Pinpoint Tracing (HTTP Server)" << std::endl;
    std::cout << "MongoDB URI: " << g_mongo_uri << std::endl;
    std::cout << "\nHTTP Server starting on http://localhost:8092" << std::endl;
    std::cout << "Endpoints:" << std::endl;
    std::cout << "  GET /run_demo  - Run Insert/Find/Update/Delete demo with tracing" << std::endl;
    std::cout << "\nTry:  curl http://localhost:8092/run_demo" << std::endl;
    std::cout << "\nPress Ctrl+C to stop the server..." << std::endl;

    server.listen("0.0.0.0", 8092);

    g_agent->Shutdown();
    return 0;
}
