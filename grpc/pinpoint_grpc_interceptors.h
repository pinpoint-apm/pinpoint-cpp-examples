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

#include <cassert>
#include <memory>
#include <string>
#include <string_view>

#include <grpcpp/server_context.h>
#include <grpcpp/support/client_interceptor.h>
#include <grpcpp/support/server_interceptor.h>

#include "pinpoint/tracer.h"

#include "pinpoint_grpc_context.h"

namespace grpc_demo {

// Pinpoint records a bare host as the span's remote address, but gRPC hands out
// peer strings like "ipv4:127.0.0.1:54321", "ipv6:%5B::1%5D:54321" or
// "unix:/tmp/foo.sock". Drop the scheme, the port and the brackets, and fall
// back to 127.0.0.1 when nothing host-shaped is left - the same result the Go
// agent gets from net.SplitHostPort() on the gRPC peer address.
inline std::string ResolvePeerHost(std::string_view peer) {
  constexpr std::string_view kFallback = "127.0.0.1";
  if (peer.rfind("unix", 0) == 0) {
    return std::string(kFallback);  // a socket path carries no host
  }
  if (peer.rfind("ipv4:", 0) == 0 || peer.rfind("ipv6:", 0) == 0) {
    peer.remove_prefix(5);
  }
  if (const auto colon = peer.rfind(':'); colon != std::string_view::npos) {
    peer = peer.substr(0, colon);  // drop the port
  }
  // An IPv6 host stays bracketed, and gRPC percent-encodes the brackets, so
  // "%5B::1%5D" is what Go's net.SplitHostPort() would report as a bare "::1".
  if (peer.size() > 2 && peer.front() == '[' && peer.back() == ']') {
    peer = peer.substr(1, peer.size() - 2);
  } else if (peer.size() > 6 && peer.rfind("%5B", 0) == 0 &&
             peer.substr(peer.size() - 3) == "%5D") {
    peer = peer.substr(3, peer.size() - 6);
  }
  return peer.empty() ? std::string(kFallback) : std::string(peer);
}

// The destination is the channel's dial target, not the resolved peer: it names
// the callee node on the server map, and the client propagates it as
// Pinpoint-Host so the server records it as its own acceptor host. Mirrors the
// Go agent's handling of grpc.ClientConn.Target().
// Refer to https://github.com/grpc/grpc/blob/master/doc/naming.md
inline std::string ResolveGrpcTarget(std::string_view target) {
  if (target.rfind("unix:", 0) == 0) {
    return "localhost";
  }
  constexpr std::string_view kDnsPrefix = "dns:///";
  if (target.rfind(kDnsPrefix, 0) == 0) {
    return std::string(target.substr(kDnsPrefix.size()));
  }
  return std::string(target);
}

class PinpointServerInterceptor final : public grpc::experimental::Interceptor {
 public:
  explicit PinpointServerInterceptor(grpc::experimental::ServerRpcInfo* info)
      : info_(info) {}

  void Intercept(grpc::experimental::InterceptorBatchMethods* methods) override {
    using grpc::experimental::InterceptionHookPoints;

    if (methods->QueryInterceptionHookPoint(
            InterceptionHookPoints::POST_RECV_INITIAL_METADATA)) {
      if (!span_) {
        auto* server_context_base = info_->server_context();
        GrpcServerTraceContextReader reader(server_context_base);
        auto agent = pinpoint::GlobalAgent();
        const std::string rpc_method = info_->method();
        span_ = agent->NewSpan("gRPC Server", rpc_method, reader);

        if (span_ && span_->IsSampled()) {
          span_->SetServiceType(pinpoint::SERVICE_TYPE_GRPC_SERVER);
          if (server_context_base != nullptr) {
            span_->SetRemoteAddress(ResolvePeerHost(server_context_base->peer()));
          }
        }

        SetCurrentSpan(span_);
      }
    }

    if (methods->QueryInterceptionHookPoint(
            InterceptionHookPoints::PRE_SEND_STATUS)) {
      if (span_) {
        // No SetStatusCode(): that records an HTTP status code annotation, and
        // a gRPC status code is a different scale entirely (14 is UNAVAILABLE,
        // not "HTTP 14"). A failed RPC is reported through SetError() alone,
        // like the Go agent does.
        grpc::Status status = methods->GetSendStatus();
        if (!status.ok()) {
          span_->SetError("grpc error", status.error_message());
        }
        span_->EndSpan();
        span_.reset();
      }
      ClearCurrentSpan();
    }

    methods->Proceed();
  }

 private:
  grpc::experimental::ServerRpcInfo* info_;
  pinpoint::SpanPtr span_;
};

class PinpointServerInterceptorFactory final
    : public grpc::experimental::ServerInterceptorFactoryInterface {
 public:
  grpc::experimental::Interceptor* CreateServerInterceptor(
      grpc::experimental::ServerRpcInfo* info) override {
    return new PinpointServerInterceptor(info);
  }
};

class PinpointClientInterceptor final : public grpc::experimental::Interceptor {
 public:
  PinpointClientInterceptor(grpc::experimental::ClientRpcInfo* info,
                            std::string remote)
      : info_(info), remote_(std::move(remote)) {}

  void Intercept(grpc::experimental::InterceptorBatchMethods* methods) override {
    using grpc::experimental::InterceptionHookPoints;
    if (methods->QueryInterceptionHookPoint(
            InterceptionHookPoints::PRE_SEND_INITIAL_METADATA)) {
      parent_span_ = GetCurrentSpan();
      assert(parent_span_ &&
             "gRPC client interceptor requires a parent span via SetCurrentSpan()");

      const std::string rpc_method = info_->method();

      span_event_ = parent_span_->NewSpanEvent(rpc_method);
      if (span_event_) {
        span_event_->SetServiceType(pinpoint::SERVICE_TYPE_GRPC_CLIENT);
        // Destination and endpoint are both the dial target: the destination
        // names the callee on the server map, the endpoint is what this call
        // actually talked to.
        span_event_->SetDestination(remote_);
        span_event_->SetEndPoint(remote_);
        span_event_->SetAnnotation(pinpoint::ANNOTATION_HTTP_URL,
                                   "grpc://" + remote_ + rpc_method);

        if (auto* context = info_->client_context()) {
          GrpcClientTraceContextWriter writer(context);
          span_event_->InjectContext(writer);
        }
      }
    }

    if (methods->QueryInterceptionHookPoint(
            InterceptionHookPoints::POST_RECV_STATUS)) {
      if (span_event_ && parent_span_) {
        if (auto* status = methods->GetRecvStatus(); status != nullptr && !status->ok()) {
          span_event_->SetError("grpc error", status->error_message());
        }
        span_event_->EndEvent();
        span_event_ = nullptr;
      }
    }

    methods->Proceed();
  }

 private:
  grpc::experimental::ClientRpcInfo* info_;
  std::string remote_;
  pinpoint::SpanPtr parent_span_;
  pinpoint::SpanEventPtr span_event_;
};

class PinpointClientInterceptorFactory final
    : public grpc::experimental::ClientInterceptorFactoryInterface {
 public:
  // gRPC C++ exposes no target getter on the channel, so the dial target is
  // handed in here - the stand-in for the Go agent's grpc.ClientConn.Target().
  explicit PinpointClientInterceptorFactory(std::string_view target)
      : remote_(ResolveGrpcTarget(target)) {}

  grpc::experimental::Interceptor* CreateClientInterceptor(
      grpc::experimental::ClientRpcInfo* info) override {
    return new PinpointClientInterceptor(info, remote_);
  }

 private:
  std::string remote_;
};

}  // namespace grpc_demo

