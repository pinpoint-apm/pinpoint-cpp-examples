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

#pragma once

#include <algorithm>
#include <cctype>
#include <optional>
#include <string>
#include <string_view>

#include <grpcpp/grpcpp.h>
#include <grpcpp/server_context.h>

#include "pinpoint/tracer.h"

namespace grpc_demo {

namespace detail {
inline std::string ToLower(std::string_view value) {
  std::string result(value);
  std::transform(result.begin(), result.end(), result.begin(),
                 [](unsigned char character) { return std::tolower(character); });
  return result;
}
}  // namespace detail

// Thread-local storage for the active span within the current thread.
inline thread_local pinpoint::SpanPtr g_current_span = nullptr;

inline void SetCurrentSpan(const pinpoint::SpanPtr& span) { g_current_span = span; }

inline pinpoint::SpanPtr GetCurrentSpan() { return g_current_span; }

inline void ClearCurrentSpan() { g_current_span.reset(); }

// Utility class to read Pinpoint trace context from gRPC server metadata.
class GrpcServerTraceContextReader final : public pinpoint::TraceContextReader {
 public:
  explicit GrpcServerTraceContextReader(grpc::ServerContextBase* context)
      : context_(context) {}
  ~GrpcServerTraceContextReader() override = default;

  std::optional<std::string_view> Get(std::string_view key) const override {
    if (context_ == nullptr) {
      return std::nullopt;
    }

    const auto lower_key = detail::ToLower(key);
    const auto& metadata = context_->client_metadata();
    for (const auto& entry : metadata) {
      std::string entry_key(entry.first.data(), entry.first.size());
      if (detail::ToLower(entry_key) == lower_key) {
        return std::string_view(entry.second.data(), entry.second.size());
      }
    }
    return std::nullopt;
  }

 private:
  grpc::ServerContextBase* context_;
};

// Utility class to inject Pinpoint trace context into gRPC client metadata.
class GrpcClientTraceContextWriter final : public pinpoint::TraceContextWriter {
 public:
  explicit GrpcClientTraceContextWriter(grpc::ClientContext* context) : context_(context) {}
  ~GrpcClientTraceContextWriter() override = default;

  void Set(std::string_view key, std::string_view value) override {
    if (context_ == nullptr) {
      return;
    }

    context_->AddMetadata(detail::ToLower(key), std::string(value));
  }

 private:
  grpc::ClientContext* context_;
};

}  // namespace grpc_demo

