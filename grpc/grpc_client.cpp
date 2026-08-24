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

#include <chrono>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include <grpcpp/grpcpp.h>

#include "pinpoint/tracer.h"

// HTTP Server headers
#include "httplib.h"
#include "http_trace_context.h"

#include "pinpoint_grpc_context.h"
#include "pinpoint_grpc_interceptors.h"

#include "testapp.grpc.pb.h"

namespace grpc_demo {

std::unique_ptr<grpcdemo::Hello::Stub> CreateStubWithInterceptor(
    const std::string& target) {
  grpc::ChannelArguments args;
  std::vector<std::unique_ptr<grpc::experimental::ClientInterceptorFactoryInterface>>
      interceptor_creators;
  interceptor_creators.push_back(
      std::make_unique<PinpointClientInterceptorFactory>());

  auto channel = grpc::experimental::CreateCustomChannelWithInterceptors(
      target, grpc::InsecureChannelCredentials(), args,
      std::move(interceptor_creators));
  return grpcdemo::Hello::NewStub(channel);
}

// The RPC helpers below run under the handler's span: the client interceptor
// picks it up via GetCurrentSpan() and attaches a span event per RPC, so they
// only perform the call and record failures on the span.

bool CallUnary(grpcdemo::Hello::Stub* stub, pinpoint::SpanPtr span) {
  grpc::ClientContext context;
  grpcdemo::Greeting request;
  grpcdemo::Greeting response;
  request.set_msg("Hello from unary client");

  auto status = stub->UnaryCallUnaryReturn(&context, request, &response);
  if (!status.ok()) {
    span->SetError(status.error_message());
    return false;
  }
  span->SetAnnotation(pinpoint::ANNOTATION_API, response.msg());
  return true;
}

bool CallServerStreaming(grpcdemo::Hello::Stub* stub, pinpoint::SpanPtr span) {
  grpc::ClientContext context;
  grpcdemo::Greeting request;
  request.set_msg("Stream greetings");

  auto reader = stub->UnaryCallStreamReturn(&context, request);
  grpcdemo::Greeting response;
  while (reader->Read(&response)) {
    std::cout << "Server stream: " << response.msg() << std::endl;
  }
  auto status = reader->Finish();
  if (!status.ok()) {
    span->SetError(status.error_message());
    return false;
  }
  return true;
}

bool CallBidirectional(grpcdemo::Hello::Stub* stub, pinpoint::SpanPtr span) {
  grpc::ClientContext context;
  auto stream = stub->StreamCallStreamReturn(&context);

  for (int i = 0; i < 3; ++i) {
    grpcdemo::Greeting request;
    request.set_msg("Message " + std::to_string(i));
    stream->Write(request);
    grpcdemo::Greeting response;
    if (stream->Read(&response)) {
      std::cout << "Bidi response: " << response.msg() << std::endl;
    }
  }
  stream->WritesDone();
  auto status = stream->Finish();
  if (!status.ok()) {
    span->SetError(status.error_message());
    return false;
  }
  return true;
}

}  // namespace grpc_demo

// Global agent, stub and connection settings
pinpoint::AgentPtr g_agent = nullptr;
std::unique_ptr<grpcdemo::Hello::Stub> g_stub;
std::string g_grpc_target = "localhost:50051";

// HTTP handler: runs the unary → server-streaming → bidi call sequence
// against the gRPC server and reports the outcome as JSON.
void handle_run_client(const httplib::Request& req, httplib::Response& res) {
  auto span = grpc_demo::GetCurrentSpan();
  // RAII guard: the span event ends when `spanEvent` leaves scope.
  pinpoint::helper::ScopedSpanEvent spanEvent(span, "handle_run_client");

  bool unary_ok = grpc_demo::CallUnary(g_stub.get(), span);
  bool stream_ok = grpc_demo::CallServerStreaming(g_stub.get(), span);
  bool bidi_ok = grpc_demo::CallBidirectional(g_stub.get(), span);
  bool success = unary_ok && stream_ok && bidi_ok;

  std::stringstream json_response;
  json_response << "{\n";
  json_response << "  \"success\": " << (success ? "true" : "false") << ",\n";
  json_response << "  \"target\": \"" << g_grpc_target << "\",\n";
  json_response << "  \"calls\": {\n";
  json_response << "    \"unary\": " << (unary_ok ? "true" : "false") << ",\n";
  json_response << "    \"server_streaming\": " << (stream_ok ? "true" : "false") << ",\n";
  json_response << "    \"bidirectional\": " << (bidi_ok ? "true" : "false") << "\n";
  json_response << "  }\n";
  json_response << "}";

  res.status = success ? 200 : 502;
  if (!success) {
    spanEvent->SetError("one or more gRPC calls failed");
  }

  res.set_content(json_response.str(), "application/json");
  std::cout << "Request handled: " << req.path << " -> Status: " << res.status << std::endl;
}

pinpoint::SpanPtr trace_request(const httplib::Request& req) {
  HttpHeaderReader request_reader(req.headers);
  auto span = g_agent->NewSpan("gRPC Client Web", req.path, request_reader);

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
    // The gRPC client interceptor locates the active span through this
    // thread-local, so it doubles as the handler's span context.
    grpc_demo::SetCurrentSpan(span);

    handler(req, res);

    trace_response(req, res, span);
    grpc_demo::ClearCurrentSpan();
  };
}

int main(int argc, char** argv) {
  (void)argc;
  (void)argv;

  if (const char* env_target = std::getenv("GRPC_TARGET")) {
    g_grpc_target = env_target;
  }

  setenv("PINPOINT_CPP_APPLICATION_NAME", "cpp-grpc-client-demo", 0);

  // StartAgent() brings the agent up asynchronously: the collector connection
  // and registration complete on a background thread, so Enable() only turns
  // true once the collector has accepted the agent. Don't gate startup on it
  // here — the demo runs regardless and spans are delivered once registration
  // lands.
  if (!pinpoint::StartAgent()) {
    std::cerr << "failed to start the pinpoint agent: check the agent log" << std::endl;
  }
  g_agent = pinpoint::GlobalAgent();

  // The channel and stub are thread-safe and shared across requests.
  g_stub = grpc_demo::CreateStubWithInterceptor(g_grpc_target);

  httplib::Server server;
  server.Get("/run_client", wrap_handler(handle_run_client));

  std::cout << "gRPC Client Demo with Pinpoint Tracing (HTTP Server)" << std::endl;
  std::cout << "gRPC target: " << g_grpc_target << std::endl;
  std::cout << "\nHTTP Server starting on http://localhost:8094" << std::endl;
  std::cout << "Endpoints:" << std::endl;
  std::cout << "  GET /run_client  - Run unary/server-streaming/bidi calls with tracing" << std::endl;
  std::cout << "\nTry:  curl http://localhost:8094/run_client" << std::endl;
  std::cout << "\nPress Ctrl+C to stop the server..." << std::endl;

  server.listen("0.0.0.0", 8094);

  g_agent->Shutdown();
  return 0;
}
