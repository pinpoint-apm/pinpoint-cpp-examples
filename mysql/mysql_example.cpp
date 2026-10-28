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

#include <string>
#include <string_view>
#include <iostream>
#include <sstream>
#include <vector>
#include <memory>
#include <chrono>
#include <cstring>
#include <functional>

// MySQL Connector C++ X DevAPI headers  
#include <mysqlx/xdevapi.h>

// HTTP Server headers
#include "httplib.h"
#include "http_trace_context.h"

// Pinpoint headers
#include "pinpoint/tracer.h"

// Thread local storage for span
thread_local pinpoint::SpanPtr current_span;

// Helper functions for thread local span management
void set_span_context(pinpoint::SpanPtr span) { current_span = span; }
pinpoint::SpanPtr get_span_context() { return current_span; }

// Forward declarations
struct DatabaseConfig {
    std::string host = "localhost";
    std::string port = "33060";  // X Protocol port
    std::string database = "test";
    std::string username = "root";
    std::string password = "pinpoint123";
    
    std::string getConnectionString() const {
        return "tcp://" + host + ":" + port + "/" + database;
    }
};

class DatabaseDemo {
public:
    explicit DatabaseDemo(const DatabaseConfig& config);
    ~DatabaseDemo();
    
    bool connect();
    void disconnect();
    
    // SQL query type enum
    enum class QueryType {
        SELECT,
        INSERT,
        UPDATE,
        DELETE,
        OTHER
    };
    
    // Traced SQL execution methods
    void executeSelectQuery(const std::string& sql, const std::vector<std::string>& params = {});
    void executeDMLQuery(QueryType type, const std::string& sql, const std::vector<std::string>& params = {});
    
    // Convenience wrappers for common DML operations
    void executeInsertQuery(const std::string& sql, const std::vector<std::string>& params = {}) {
        executeDMLQuery(QueryType::INSERT, sql, params);
    }
    void executeUpdateQuery(const std::string& sql, const std::vector<std::string>& params = {}) {
        executeDMLQuery(QueryType::UPDATE, sql, params);
    }
    void executeDeleteQuery(const std::string& sql, const std::vector<std::string>& params = {}) {
        executeDMLQuery(QueryType::DELETE, sql, params);
    }
    
    // Run demo scenarios
    void runDemo();
    
    // HTTP handler version that returns JSON response
    std::string runDemoHandler();

private:
    DatabaseConfig config_;
    std::unique_ptr<mysqlx::Session> session_;
    
    // Tracing helper methods
    void initSqlSpanEvent(pinpoint::SpanEventPtr spanEvent);
    void traceSqlExecution(pinpoint::SpanEventPtr spanEvent, const std::string& sql,
                          const std::vector<std::string>& params, bool success,
                          const std::string& error = "");
    std::string formatParameters(const std::vector<std::string>& params);
    
    // Helper to convert QueryType to string
    std::string queryTypeToString(QueryType type) const {
        switch (type) {
            case QueryType::SELECT: return "SELECT";
            case QueryType::INSERT: return "INSERT";
            case QueryType::UPDATE: return "UPDATE";
            case QueryType::DELETE: return "DELETE";
            case QueryType::OTHER:  return "OTHER";
            default: return "UNKNOWN";
        }
    }
};

DatabaseDemo::DatabaseDemo(const DatabaseConfig& config) 
    : config_(config) {
}

DatabaseDemo::~DatabaseDemo() {
    disconnect();
}

bool DatabaseDemo::connect() {
    try {
        std::cout << "Connecting to database: " << config_.getConnectionString() << std::endl;
        
        std::string connectionString = "mysqlx://" + config_.username + ":" + config_.password + 
                                      "@" + config_.host + ":" + config_.port + "/" + config_.database;
        
        session_ = std::make_unique<mysqlx::Session>(connectionString);
        
        if (session_) {
            std::cout << "Successfully connected to MySQL database via X Protocol" << std::endl;
            return true;
        }
        
    } catch (const mysqlx::Error& e) {
        std::cerr << "Connection failed: " << e.what() << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "Connection failed: " << e.what() << std::endl;
    }
    
    return false;
}

void DatabaseDemo::disconnect() {
    if (session_) {
        session_->close();
        session_.reset();
        std::cout << "Database connection closed" << std::endl;
    }
}

void DatabaseDemo::initSqlSpanEvent(pinpoint::SpanEventPtr spanEvent) {
    spanEvent->SetEndPoint(config_.host + ":" + config_.port);
    spanEvent->SetDestination(config_.database);
}

static int random_number() {
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<int> dis(100, 300);
    return dis(gen);
}

void DatabaseDemo::traceSqlExecution(pinpoint::SpanEventPtr spanEvent, const std::string& sql,
                                    const std::vector<std::string>& params, bool success,
                                    const std::string& error) {
    try {
        // SetSqlQuery joins the bind values itself; the views only need to stay
        // valid for the duration of the call, and `params` outlives it.
        std::vector<pinpoint::SqlBindValue> bindArgs;
        bindArgs.reserve(params.size());
        for (const auto& param : params) {
            bindArgs.emplace_back(std::string_view{param});
        }

        spanEvent->SetSqlQuery(sql, bindArgs);

        if (!success && !error.empty()) {
            spanEvent->SetError("DB_ERROR", error);
        }
    } catch (const std::exception& e) {
        std::cerr << "Error in SQL tracing: " << e.what() << std::endl;;
    }

    // Simulated latency; the caller's ScopedSpanEvent guard ends the event
    // after this returns, so the sleep is included in the event duration.
    std::this_thread::sleep_for(std::chrono::milliseconds(random_number()));
}

std::string DatabaseDemo::formatParameters(const std::vector<std::string>& params) {
    if (params.empty()) {
        return "";
    }
    
    std::stringstream ss;
    for (size_t i = 0; i < params.size(); ++i) {
        if (i > 0) {
            ss << ", ";
        }
        ss << params[i];
    }
    return ss.str();
}

void DatabaseDemo::executeSelectQuery(const std::string& sql, const std::vector<std::string>& params) {
    // RAII guard: the SQL span event ends when `spanEvent` leaves scope.
    pinpoint::helper::ScopedSpanEvent spanEvent(get_span_context(), "SQL_SELECT",
                                                pinpoint::SERVICE_TYPE_MYSQL_QUERY);
    initSqlSpanEvent(spanEvent.value());
    bool success = false;
    std::string error;
    
    try {
        auto start = std::chrono::high_resolution_clock::now();
        
        auto stmt = session_->sql(sql);
        if (!params.empty()) {
            for (size_t i = 0; i < params.size(); ++i) {
                stmt.bind(params[i]);
            }
        }
        auto result = stmt.execute();
        
        std::cout << "SELECT Query executed: " << sql << std::endl;
        if (!params.empty()) {
            std::cout << "  Parameters: " << formatParameters(params) << std::endl;
        }
        
        if (result.hasData()) {
            auto rows = result.fetchAll();
            int totalRows = 0;
            
            for (const auto& row : rows) {
                totalRows++;
            }
            std::cout << "  Total rows returned: " << totalRows << std::endl;
        } else {
            std::cout << "  No data returned" << std::endl;
        }
        
        auto end = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
        std::cout << "  Execution time: " << duration.count() << "ms" << std::endl;
        
        success = true;
        
    } catch (const mysqlx::Error& e) {
        error = std::string("MySQL X DevAPI Error: ") + e.what();
        std::cerr << "SELECT failed: " << error << std::endl;
    } catch (const std::exception& e) {
        error = std::string("Exception: ") + e.what();
        std::cerr << "SELECT failed: " << error << std::endl;
    }

    traceSqlExecution(spanEvent.value(), sql, params, success, error);
}

// Unified DML query execution (INSERT, UPDATE, DELETE)
void DatabaseDemo::executeDMLQuery(QueryType type, const std::string& sql, const std::vector<std::string>& params) {
    std::string queryTypeName = queryTypeToString(type);
    // RAII guard: the SQL span event ends when `spanEvent` leaves scope.
    pinpoint::helper::ScopedSpanEvent spanEvent(get_span_context(), "SQL_" + queryTypeName,
                                                pinpoint::SERVICE_TYPE_MYSQL_QUERY);
    initSqlSpanEvent(spanEvent.value());
    bool success = false;
    std::string error;
    
    try {
        auto start = std::chrono::high_resolution_clock::now();
        
        // Prepare and bind parameters
        auto stmt = session_->sql(sql);
        if (!params.empty()) {
            for (size_t i = 0; i < params.size(); ++i) {
                stmt.bind(params[i]);
            }
        }
        
        // Execute query
        auto result = stmt.execute();
        uint64_t affectedRows = result.getAffectedItemsCount();
        
        auto end = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
        
        // Log results
        std::cout << queryTypeName << " Query executed: " << sql << std::endl;
        if (!params.empty()) {
            std::cout << "  Parameters: " << formatParameters(params) << std::endl;
        }
        std::cout << "  Affected rows: " << affectedRows << std::endl;
        std::cout << "  Execution time: " << duration.count() << "ms" << std::endl;
        
        success = true;
        
    } catch (const mysqlx::Error& e) {
        error = std::string("MySQL X DevAPI Error: ") + e.what();
        std::cerr << queryTypeName << " failed: " << error << std::endl;
    } catch (const std::exception& e) {
        error = std::string("Exception: ") + e.what();
        std::cerr << queryTypeName << " failed: " << error << std::endl;
    }

    traceSqlExecution(spanEvent.value(), sql, params, success, error);
}

std::string DatabaseDemo::runDemoHandler() {
    std::stringstream result;
    result << "{\n";
    result << "  \"status\": \"running\",\n";
    result << "  \"message\": \"Database Demo Starting\",\n";
    result << "  \"operations\": [\n";
    
    auto span = get_span_context();  // Get span from thread local storage
    // RAII guard: the span event ends when `spanEvent` leaves scope.
    pinpoint::helper::ScopedSpanEvent spanEvent(span, "runDemoHandler");

    bool first = true;
    auto addOperation = [&](const std::string& op, const std::string& status, const std::string& details = "") {
        if (!first) result << ",\n";
        result << "    {\n";
        result << "      \"operation\": \"" << op << "\",\n";
        result << "      \"status\": \"" << status << "\"";
        if (!details.empty()) {
            result << ",\n      \"details\": \"" << details << "\"";
        }
        result << "\n    }";
        first = false;
    };
    
    try {
        // Create demo table
        std::cout << "\n--- Creating demo table ---" << std::endl;
        executeUpdateQuery(R"(
            CREATE TABLE IF NOT EXISTS demo_users (
                id INT AUTO_INCREMENT PRIMARY KEY,
                name VARCHAR(100) NOT NULL,
                email VARCHAR(100) UNIQUE,
                age INT,
                created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
            )
        )");
        addOperation("create_table", "success", "demo_users table created/verified");
        
        // Clear existing data
        std::cout << "\n--- Cleaning existing data ---" << std::endl;
        executeDeleteQuery("DELETE FROM demo_users");
        addOperation("clear_data", "success", "Existing data cleared");
        
        // Insert demo data
        std::cout << "\n--- Inserting demo data ---" << std::endl;
        executeInsertQuery("INSERT INTO demo_users (name, email, age) VALUES (?, ?, ?)", 
                          {"John Doe", "john@example.com", "25"});
        executeInsertQuery("INSERT INTO demo_users (name, email, age) VALUES (?, ?, ?)", 
                          {"Jane Smith", "jane@example.com", "30"});
        executeInsertQuery("INSERT INTO demo_users (name, email, age) VALUES (?, ?, ?)", 
                          {"Bob Johnson", "bob@example.com", "35"});
        addOperation("insert_data", "success", "3 users inserted");
        
        // Select queries
        std::cout << "\n--- Running SELECT queries ---" << std::endl;
        executeSelectQuery("SELECT * FROM demo_users");
        executeSelectQuery("SELECT * FROM demo_users WHERE age > ?", {"25"});
        executeSelectQuery("SELECT name, email FROM demo_users WHERE name LIKE ?", {"%John%"});
        addOperation("select_queries", "success", "Multiple SELECT queries executed");
        
        // Update queries
        std::cout << "\n--- Running UPDATE queries ---" << std::endl;
        executeUpdateQuery("UPDATE demo_users SET age = ? WHERE name = ?", {"26", "John Doe"});
        executeSelectQuery("SELECT * FROM demo_users WHERE name = ?", {"John Doe"});
        addOperation("update_queries", "success", "UPDATE and verification completed");
        
        // Complex queries
        std::cout << "\n--- Running complex queries ---" << std::endl;
        executeSelectQuery(R"(
            SELECT 
                name, 
                email, 
                age,
                CASE 
                    WHEN age < 30 THEN 'Young'
                    WHEN age < 40 THEN 'Middle'
                    ELSE 'Senior'
                END as age_group
            FROM demo_users 
            ORDER BY age DESC
        )");
        executeSelectQuery("SELECT COUNT(*) as total_users, AVG(age) as avg_age FROM demo_users");
        addOperation("complex_queries", "success", "Complex SELECT with CASE and aggregation");
        
        // Test error case
        std::cout << "\n--- Testing error handling ---" << std::endl;
        executeSelectQuery("SELECT * FROM non_existent_table");
        addOperation("error_handling", "tested", "Error handling with non-existent table");
        
        // Delete some data
        std::cout << "\n--- Deleting some data ---" << std::endl;
        executeDeleteQuery("DELETE FROM demo_users WHERE age > ?", {"30"});
        executeSelectQuery("SELECT COUNT(*) as remaining_users FROM demo_users");
        addOperation("delete_data", "success", "Conditional DELETE executed");
        
        std::cout << "\n=== Database Demo Completed ===" << std::endl;
        
    } catch (const std::exception& e) {
        addOperation("demo_execution", "failed", std::string("Error: ") + e.what());
    }
    
    result << "\n  ],\n";
    result << "  \"demo_status\": \"completed\",\n";
    result << "  \"timestamp\": \"" << std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count() << "\"\n";
    result << "}";

    return result.str();
}

// HTTP server handler functions
void handle_db_demo(const httplib::Request& req, httplib::Response& res);
void handle_db_status(const httplib::Request& req, httplib::Response& res);
pinpoint::SpanPtr trace_request(const httplib::Request& req);
void trace_response(const httplib::Request& req, httplib::Response& res, pinpoint::SpanPtr span);
httplib::Server::Handler wrap_handler(httplib::Server::Handler handler);

pinpoint::SpanPtr trace_request(const httplib::Request& req) {
    auto agent = pinpoint::GlobalAgent();

    HttpHeaderReader request_reader(req.headers);
    auto span = agent->NewSpan("C++ DB Demo", req.path, request_reader);

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

// Global database demo instance
DatabaseDemo* g_demo = nullptr;

void handle_db_demo(const httplib::Request& req, httplib::Response& res) {
    auto span = get_span_context();  // Get span from thread local storage
    // RAII guard: the span event ends when `spanEvent` leaves scope.
    pinpoint::helper::ScopedSpanEvent spanEvent(span, "handle_db_demo");

    std::stringstream json_response;
    
    try {
        if (g_demo && g_demo->connect()) {
            std::string demo_result = g_demo->runDemoHandler();
            
            json_response << "{\n";
            json_response << "  \"success\": true,\n";
            json_response << "  \"database\": \"tcp://localhost:33060/test\",\n";
            json_response << "  \"demo_result\": " << demo_result << "\n";
            json_response << "}";
            
            res.status = 200;
            std::cout << "Database demo executed successfully via HTTP" << std::endl;
            
        } else {
            json_response << "{\n";
            json_response << "  \"success\": false,\n";
            json_response << "  \"error\": \"Database connection failed\",\n";
            json_response << "  \"message\": \"Please ensure MySQL is running and accessible\",\n";
            json_response << "  \"instructions\": [\n";
            json_response << "    \"Install MySQL Server 8.0+ with X Protocol\",\n";
            json_response << "    \"Start MySQL service on localhost:3306\",\n";
            json_response << "    \"Create test database: CREATE DATABASE test;\",\n";
            json_response << "    \"Update connection settings if needed\"\n";
            json_response << "  ]\n";
            json_response << "}";
            
            res.status = 503; // Service Unavailable
            std::cout << "Database demo failed: connection error" << std::endl;
        }
        
    } catch (const std::exception& e) {
        json_response.str("");
        json_response << "{\n";
        json_response << "  \"success\": false,\n";
        json_response << "  \"error\": \"Demo execution failed\",\n";
        json_response << "  \"details\": \"" << e.what() << "\"\n";
        json_response << "}";
        
        res.status = 500;
        spanEvent->SetError(std::string("Demo failed: ") + e.what());
        std::cerr << "Database demo failed with exception: " << e.what() << std::endl;
    }
    
    // Set response
    res.set_content(json_response.str(), "application/json");
    
    std::cout << "Request handled: " << req.path << " -> Status: " << res.status << std::endl;
}

void handle_db_status(const httplib::Request& req, httplib::Response& res) {
    auto span = get_span_context();  // Get span from thread local storage
    // RAII guard: the span event ends when `spanEvent` leaves scope.
    pinpoint::helper::ScopedSpanEvent spanEvent(span, "handle_db_status");

    std::stringstream json_response;
    json_response << "{\n";
    json_response << "  \"service\": \"MySQL Database Demo with Pinpoint Tracing\",\n";
    json_response << "  \"version\": \"1.0.0\",\n";
    json_response << "  \"database\": {\n";
    json_response << "    \"host\": \"localhost\",\n";
    json_response << "    \"port\": \"3306\",\n";
    json_response << "    \"database\": \"test\",\n";
    json_response << "    \"protocol\": \"MySQL X DevAPI\"\n";
    json_response << "  },\n";
    json_response << "  \"endpoints\": {\n";
    json_response << "    \"/db-demo\": \"Run complete database demo with tracing\",\n";
    json_response << "    \"/status\": \"Get service status and configuration\"\n";
    json_response << "  },\n";
    json_response << "  \"timestamp\": \"" << std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count() << "\"\n";
    json_response << "}";
    
    // Set response
    res.set_content(json_response.str(), "application/json");
    res.status = 200;
    
    std::cout << "Request handled: " << req.path << " -> Status info returned" << std::endl;
}

int main() {
    // Pinpoint configuration
    setenv("PINPOINT_CPP_APPLICATION_NAME", "cpp-mysql-demo", 0);
    setenv("PINPOINT_CPP_UID_VERSION", "v3", 0);
    setenv("PINPOINT_CPP_SQL_ENABLE_SQL_STATS", "true", 0);
    setenv("PINPOINT_CPP_HTTP_COLLECT_URL_STAT", "true", 0);
    
    if (!pinpoint::StartAgent()) {
        std::cerr << "failed to start the pinpoint agent: check the agent log" << std::endl;
    }
    auto agent = pinpoint::GlobalAgent();
    
    // Database configuration
    DatabaseConfig config;
    // Read configuration from environment variables or use defaults
    config.host = getenv("MYSQL_HOST") ? getenv("MYSQL_HOST") : "localhost";
    config.port = getenv("MYSQL_PORT") ? getenv("MYSQL_PORT") : "33060";  // X Protocol port
    config.database = getenv("MYSQL_DATABASE") ? getenv("MYSQL_DATABASE") : "test";
    config.username = getenv("MYSQL_USER") ? getenv("MYSQL_USER") : "root";
    config.password = getenv("MYSQL_PASSWORD") ? getenv("MYSQL_PASSWORD") : "pinpoint123";
    
    std::cout << "MySQL Database Demo with Pinpoint Tracing (HTTP Server)" << std::endl;
    std::cout << "=======================================================" << std::endl;
    std::cout << "Database: " << config.getConnectionString() << std::endl;
    std::cout << "User: " << config.username << std::endl;
    
    // Create global database demo instance
    DatabaseDemo demo(config);
    g_demo = &demo;
    
    // Create HTTP server
    httplib::Server server;
    
    // Register endpoints
    server.Get("/db-demo", wrap_handler(handle_db_demo));
    server.Get("/status", wrap_handler(handle_db_status));
    
    // Add CORS headers for web browser access
    server.set_pre_routing_handler([](const httplib::Request& req, httplib::Response& res) {
        res.set_header("Access-Control-Allow-Origin", "*");
        res.set_header("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
        res.set_header("Access-Control-Allow-Headers", "Content-Type, Authorization");
        return httplib::Server::HandlerResponse::Unhandled;
    });
    
    // Handle OPTIONS requests for CORS
    server.Options(".*", [](const httplib::Request&, httplib::Response& res) {
        return;
    });
    
    std::cout << "\nHTTP Server starting on http://localhost:8089" << std::endl;
    std::cout << "===================================================" << std::endl;
    std::cout << "Endpoints:" << std::endl;
    std::cout << "  GET /db-demo  - Run complete database demo with tracing" << std::endl;
    std::cout << "  GET /status   - Get service status and configuration" << std::endl;
    std::cout << "\nTry:" << std::endl;
    std::cout << "  curl http://localhost:8089/status" << std::endl;
    std::cout << "  curl http://localhost:8089/db-demo" << std::endl;
    std::cout << "  Or open http://localhost:8089/status in your browser" << std::endl;
    std::cout << "\nPress Ctrl+C to stop the server..." << std::endl;
    
    // Start the server
    server.listen("localhost", 8089);
    
    // Cleanup
    try {
        if (agent) {
            agent->Shutdown();
        }
    } catch (...) {
        // Ignore shutdown errors
        std::cerr << "Warning: Agent shutdown encountered an issue (this is not critical)" << std::endl;
    }
    
    std::cout << "\nServer stopped!" << std::endl;
    return 0;
}
