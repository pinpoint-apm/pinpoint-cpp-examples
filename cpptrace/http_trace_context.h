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

#include <algorithm>
#include <string_view>

#include "httplib.h"
#include "pinpoint/tracer.h"

class HttpHeaderReader final : public pinpoint::HeaderReader {
public:
    explicit HttpHeaderReader(const httplib::Headers& header) : headers_(header) {}
    ~HttpHeaderReader() override = default;

    std::optional<std::string_view> Get(std::string_view key) const override {
        auto it = headers_.find(std::string(key));
        if (it == headers_.end()) {
            return std::nullopt;
        }
        return std::string_view(it->second);
    }

    void ForEach(std::function<bool(std::string_view, std::string_view)> callback) const override {
        for (const auto& pair : headers_) {
            if (!callback(pair.first, pair.second)) {
                break;
            }
        }
    }

private:
    const httplib::Headers& headers_;
};

class HttpHeaderReaderWriter final : public pinpoint::HeaderReaderWriter {
public:
    explicit HttpHeaderReaderWriter(httplib::Headers& header) : headers_(header) {}
    ~HttpHeaderReaderWriter() override = default;

    std::optional<std::string_view> Get(std::string_view key) const override {
        auto it = headers_.find(std::string(key));
        if (it == headers_.end()) {
            return std::nullopt;
        }
        return std::string_view(it->second);
    }

    void ForEach(std::function<bool(std::string_view, std::string_view)> callback) const override {
        for (const auto& pair : headers_) {
            if (!callback(pair.first, pair.second)) {
                break;
            }
        }
    }

    void Set(std::string_view key, std::string_view value) override {
        headers_.emplace(key, value);
    }

private:
    httplib::Headers& headers_;
};
