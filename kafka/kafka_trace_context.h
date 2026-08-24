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

#ifndef KAFKA_TRACE_CONTEXT_H
#define KAFKA_TRACE_CONTEXT_H

#include "pinpoint/tracer.h"
#include <librdkafka/rdkafkacpp.h>
#include <string>
#include <optional>

class KafkaTraceContextReader : public pinpoint::TraceContextReader {
public:
    explicit KafkaTraceContextReader(const RdKafka::Headers* headers) : headers_(headers) {}

    std::optional<std::string_view> Get(std::string_view key) const override {
        if (!headers_) {
            return std::nullopt;
        }

        std::string key_str(key);
        auto result = headers_->get_last(key_str);
        
        if (result.err() == RdKafka::ErrorCode::ERR_NO_ERROR) {
            value_buffer_ = result.value_string();
            return std::string_view(value_buffer_);
        }
        return std::nullopt;
    }

private:
    const RdKafka::Headers* headers_;
    mutable std::string value_buffer_;
};

class KafkaTraceContextWriter : public pinpoint::TraceContextWriter {
public:
    explicit KafkaTraceContextWriter(RdKafka::Headers* headers) : headers_(headers) {}

    void Set(std::string_view key, std::string_view value) override {
        if (!headers_) return;
        headers_->add(std::string(key), std::string(value));
    }

private:
    RdKafka::Headers* headers_;
};

#endif // KAFKA_TRACE_CONTEXT_H
