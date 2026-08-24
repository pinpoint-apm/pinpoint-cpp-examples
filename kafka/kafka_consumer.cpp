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
#include <csignal>
#include <cstdlib>
#include <librdkafka/rdkafkacpp.h>
#include "pinpoint/tracer.h"
#include "kafka_trace_context.h"

static volatile sig_atomic_t run = 1;

static void sigterm(int sig) {
  run = 0;
}

int main(int argc, char **argv) {
    std::string brokers = "localhost:9092";
    std::string group = "test-group";
    std::string topic_str = "test-topic";

    if (const char* env_brokers = std::getenv("KAFKA_BROKERS")) {
        brokers = env_brokers;
    }
    
    // Pinpoint Agent Init
    setenv("PINPOINT_CPP_APPLICATION_NAME", "cpp-kafka-consumer-demo", 0);
    if (!pinpoint::StartAgent()) {
        std::cerr << "failed to start the pinpoint agent: check the agent log" << std::endl;
    }
    auto agent = pinpoint::GlobalAgent();

    std::string errstr;
    RdKafka::Conf *conf = RdKafka::Conf::create(RdKafka::Conf::CONF_GLOBAL);
    conf->set("metadata.broker.list", brokers, errstr);
    conf->set("group.id", group, errstr);
    conf->set("auto.offset.reset", "earliest", errstr);

    RdKafka::KafkaConsumer *consumer = RdKafka::KafkaConsumer::create(conf, errstr);
    if (!consumer) {
        std::cerr << "Failed to create consumer: " << errstr << std::endl;
        delete conf;
        return 1;
    }
    delete conf;

    std::vector<std::string> topics;
    topics.push_back(topic_str);
    RdKafka::ErrorCode err = consumer->subscribe(topics);
    if (err) {
        std::cerr << "Failed to subscribe to " << topics[0] << ": " << RdKafka::err2str(err) << std::endl;
        delete consumer;
        return 1;
    }

    signal(SIGINT, sigterm);
    signal(SIGTERM, sigterm);

    std::cout << "Consuming messages from " << topic_str << "..." << std::endl;

    while (run) {
        RdKafka::Message *msg = consumer->consume(1000);
        if (msg->err() == RdKafka::ERR_NO_ERROR) {
            // Found message
            RdKafka::Headers* headers = msg->headers(); // can be null
            
            // Create Span with parent context
            std::shared_ptr<pinpoint::Span> span;
            if (headers) {
                KafkaTraceContextReader reader(headers);
                span = agent->NewSpan("KafkaConsume", "kafka_consumer", reader);
            } else {
                span = agent->NewSpan("KafkaConsume", "kafka_consumer");
            }
            
            span->SetServiceType(pinpoint::SERVICE_TYPE_KAFKA);
            span->SetEndPoint(brokers);
            span->SetRemoteAddress(brokers);
            
            // The span above is the Kafka entry point; this event is the
            // work done on the message, so the trace carries a call tree
            // instead of a bare root. The inner scope ends the RAII guard
            // before EndSpan() below.
            {
                pinpoint::helper::ScopedSpanEvent se(span, "ProcessMessage");
                se->SetEndPoint(brokers);
                // Kafka has no dedicated annotation key, so the generic API
                // slot carries which record this was (as in redis_example).
                se->SetAnnotation(pinpoint::ANNOTATION_API,
                                  topic_str + "[" + std::to_string(msg->partition()) +
                                  "] offset " + std::to_string(msg->offset()));

                if (msg->payload()) {
                    std::string payload(static_cast<const char *>(msg->payload()), msg->len());
                    std::cout << "Consumed message: " << payload << std::endl;
                }
            }
            
            span->EndSpan();
        }
        delete msg;
    }
    
    consumer->close();
    delete consumer;
    
    return 0;
}
