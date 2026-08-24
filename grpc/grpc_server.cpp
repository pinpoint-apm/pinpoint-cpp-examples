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
#include <memory>
#include <string>
#include <vector>

#include <grpcpp/grpcpp.h>
#include <grpcpp/ext/proto_server_reflection_plugin.h>
#include <grpcpp/health_check_service_interface.h>

#include "pinpoint/tracer.h"

#include "pinpoint_grpc_context.h"
#include "pinpoint_grpc_interceptors.h"

#include "testapp.grpc.pb.h"

namespace grpc_demo {

class HelloServiceImpl final : public grpcdemo::Hello::Service {
 public:
  grpc::Status UnaryCallUnaryReturn(grpc::ServerContext* context,
                                    const grpcdemo::Greeting* request,
                                    grpcdemo::Greeting* response) override {
    (void)context;

    auto span = GetCurrentSpan();
    pinpoint::helper::ScopedSpanEvent scoped(span, "HelloServiceImpl::UnaryCallUnaryReturn");
    scoped->SetAnnotation(pinpoint::ANNOTATION_API, "UnaryCallUnaryReturn");

    response->set_msg("Unary response: " + request->msg());
    return grpc::Status::OK;
  }

  grpc::Status UnaryCallStreamReturn(grpc::ServerContext* context,
                                     const grpcdemo::Greeting* request,
                                     grpc::ServerWriter<grpcdemo::Greeting>* writer) override {
    (void)context;

    auto span = GetCurrentSpan();
    pinpoint::helper::ScopedSpanEvent scoped(span, "HelloServiceImpl::UnaryCallStreamReturn");
    scoped->SetAnnotation(pinpoint::ANNOTATION_API, "UnaryCallStreamReturn");

    for (int i = 0; i < 3; ++i) {
      grpcdemo::Greeting resp;
      resp.set_msg("Stream response " + std::to_string(i) + ": " + request->msg());
      writer->Write(resp);
    }
    return grpc::Status::OK;
  }

  grpc::Status StreamCallUnaryReturn(grpc::ServerContext* context,
                                     grpc::ServerReader<grpcdemo::Greeting>* reader,
                                     grpcdemo::Greeting* response) override {
    (void)context;

    auto span = GetCurrentSpan();
    pinpoint::helper::ScopedSpanEvent scoped(span, "HelloServiceImpl::StreamCallUnaryReturn");
    scoped->SetAnnotation(pinpoint::ANNOTATION_API, "StreamCallUnaryReturn");

    std::string combined;
    grpcdemo::Greeting msg;
    while (reader->Read(&msg)) {
      combined.append(msg.msg()).append(" ");
    }
    response->set_msg("Unary response: " + combined);
    return grpc::Status::OK;
  }

  grpc::Status StreamCallStreamReturn(
      grpc::ServerContext* context,
      grpc::ServerReaderWriter<grpcdemo::Greeting, grpcdemo::Greeting>* stream) override {
    (void)context;

    auto span = GetCurrentSpan();
    pinpoint::helper::ScopedSpanEvent scoped(span, "HelloServiceImpl::StreamCallStreamReturn");
    scoped->SetAnnotation(pinpoint::ANNOTATION_API, "StreamCallStreamReturn");

    grpcdemo::Greeting request;
    while (stream->Read(&request)) {
      grpcdemo::Greeting resp;
      resp.set_msg("Echo: " + request.msg());
      stream->Write(resp);
    }
    return grpc::Status::OK;
  }
};

}  // namespace grpc_demo

int main(int argc, char** argv) {
  (void)argc;
  (void)argv;

  setenv("PINPOINT_CPP_APPLICATION_NAME", "cpp-grpc-server-demo", 0);
  setenv("PINPOINT_CPP_HTTP_COLLECT_URL_STAT", "false", 0);

  // StartAgent() brings the agent up asynchronously: the collector connection
  // and registration complete on a background thread, so Enable() only turns
  // true once the collector has accepted the agent. Don't gate startup on it
  // here — the demo runs regardless and spans are delivered once registration
  // lands.
  if (!pinpoint::StartAgent()) {
    std::cerr << "failed to start the pinpoint agent: check the agent log" << std::endl;
  }
  auto agent = pinpoint::GlobalAgent();

  grpc_demo::HelloServiceImpl service;

  grpc::EnableDefaultHealthCheckService(true);
  grpc::reflection::InitProtoReflectionServerBuilderPlugin();

  grpc::ServerBuilder builder;
  builder.AddListeningPort("0.0.0.0:50051", grpc::InsecureServerCredentials());
  builder.RegisterService(&service);

  std::vector<std::unique_ptr<grpc::experimental::ServerInterceptorFactoryInterface>>
      interceptor_creators;
  interceptor_creators.push_back(
      std::make_unique<grpc_demo::PinpointServerInterceptorFactory>());
  builder.experimental().SetInterceptorCreators(std::move(interceptor_creators));

  auto server = builder.BuildAndStart();
  if (!server) {
    std::cerr << "Failed to start gRPC server" << std::endl;
    return 1;
  }

  std::cout << "gRPC server started on 0.0.0.0:50051" << std::endl;
  std::cout << "Methods:" << std::endl;
  std::cout << "  UnaryCallUnaryReturn" << std::endl;
  std::cout << "  UnaryCallStreamReturn" << std::endl;
  std::cout << "  StreamCallUnaryReturn" << std::endl;
  std::cout << "  StreamCallStreamReturn" << std::endl;

  server->Wait();

  agent->Shutdown();
  return 0;
}

