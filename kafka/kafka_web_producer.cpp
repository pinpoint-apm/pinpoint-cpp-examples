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
#include <string>
#include <cstdlib>
#include <librdkafka/rdkafkacpp.h>

#include "pinpoint/tracer.h"
#include "kafka_trace_context.h"
#include "httplib.h"
#include "http_trace_context.h"

// Global variables for Kafka producer
std::string g_brokers = "localhost:9092";
std::string g_topic_str = "test-topic";
RdKafka::Producer *g_producer = nullptr;
pinpoint::AgentPtr g_agent = nullptr;

class ExampleDeliveryReportCb : public RdKafka::DeliveryReportCb {
public:
  void dr_cb(RdKafka::Message &message) override {
    if (message.err())
      std::cerr << "% Message delivery failed: " << message.errstr() << std::endl;
    else
      std::cerr << "% Message delivered to topic " << message.topic_name() <<
        " [" << message.partition() << "] at offset " <<
        message.offset() << std::endl;
  }
};

ExampleDeliveryReportCb g_dr_cb;

// Helper to send Kafka message
bool produce_message(const std::string& message_payload, pinpoint::SpanPtr span) {
    if (!g_producer) return false;

    // Create Span Event for Kafka Produce; the RAII guard ends it on every
    // exit path (including the early error return below).
    pinpoint::helper::ScopedSpanEvent se(span, "KafkaProduce", pinpoint::SERVICE_TYPE_KAFKA);
    se->SetEndPoint(g_brokers);
    se->SetDestination(g_topic_str);

    // Prepare headers for trace propagation from the outgoing span event.
    RdKafka::Headers *headers = RdKafka::Headers::create();
    KafkaTraceContextWriter writer(headers);
    se->InjectContext(writer);
    
    // Produce
    RdKafka::ErrorCode resp = g_producer->produce(
        g_topic_str,
        RdKafka::Topic::PARTITION_UA,
        RdKafka::Producer::RK_MSG_COPY,
        const_cast<char *>(message_payload.c_str()), message_payload.size(),
        NULL, 0,
        0,
        headers, // headers are adopted by message on success
        NULL);

    if (resp != RdKafka::ERR_NO_ERROR) {
        std::cerr << "% Produce failed: " << RdKafka::err2str(resp) << std::endl;
        se->SetError(RdKafka::err2str(resp));
        delete headers;
        return false;
    } else {
        g_producer->poll(0); // trigger callbacks
    }

    return true;
}

void handle_run_producer(const httplib::Request& req, httplib::Response& res) {
    // Extract trace context from HTTP request
    HttpHeaderReader request_reader(req.headers);
    auto span = g_agent->NewSpan("KafkaWebProducer", "/run_producer", request_reader);

    // Records remote address (honoring forwarded headers), endpoint, proxy
    // header annotations and the configured request headers in one call.
    pinpoint::helper::TraceHttpServerRequest(span, req.remote_addr, req.get_header_value("Host"), request_reader);

    // Produce message
    std::string payload = "Message from web producer " + std::to_string(std::time(nullptr));
    if (req.has_param("message")) {
        payload = req.get_param_value("message");
    }

    bool success = produce_message(payload, span);

    // Prepare response
    if (success) {
        res.status = 200;
        res.set_content("Message sent to Kafka: " + payload, "text/plain");
    } else {
        res.status = 500;
        res.set_content("Failed to send message to Kafka", "text/plain");
        span->SetError("Kafka production failed");
    }

    // Records status code, URL stat and the configured response headers.
    HttpHeaderReader response_reader(res.headers);
    pinpoint::helper::TraceHttpServerResponse(span, req.matched_route, req.method, res.status, response_reader);
    span->EndSpan();
}

int main(int argc, char **argv) {
    if (const char* env_brokers = std::getenv("KAFKA_BROKERS")) {
        g_brokers = env_brokers;
    }
    
    // Pinpoint Agent Init
    setenv("PINPOINT_CPP_APPLICATION_NAME", "cpp-kafka-producer-demo", 0);
    setenv("PINPOINT_CPP_UID_VERSION", "v3", 0);
    if (!pinpoint::StartAgent()) {
        std::cerr << "failed to start the pinpoint agent: check the agent log" << std::endl;
    }
    g_agent = pinpoint::GlobalAgent();

    // Kafka Config
    std::string errstr;
    RdKafka::Conf *conf = RdKafka::Conf::create(RdKafka::Conf::CONF_GLOBAL);
    conf->set("metadata.broker.list", g_brokers, errstr);
    conf->set("dr_cb", &g_dr_cb, errstr);

    g_producer = RdKafka::Producer::create(conf, errstr);
    if (!g_producer) {
        std::cerr << "Failed to create producer: " << errstr << std::endl;
        delete conf;
        return 1;
    }
    delete conf;

    // HTTP Server
    httplib::Server server;
    server.Get("/run_producer", handle_run_producer);

    std::cout << "Kafka Web Producer starting on http://localhost:8090" << std::endl;
    server.listen("0.0.0.0", 8090);

    // Cleanup
    if (g_producer) {
        std::cerr << "% Flushing final messages..." << std::endl;
        g_producer->flush(10 * 1000);
        delete g_producer;
    }
    g_agent->Shutdown();

    return 0;
}
