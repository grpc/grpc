/*
 *
 * Copyright 2023 gRPC authors.
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
 *
 */
#include <grpcpp/ext/call_metric_recorder.h>
#include <grpcpp/ext/orca_service.h>
#include <grpcpp/grpcpp.h>
#include <grpcpp/health_check_service_interface.h>
#include <grpcpp/support/status.h>

#include <cstddef>
#include <iostream>
#include <memory>
#include <string>

#include "absl/flags/flag.h"
#include "absl/flags/parse.h"
#include "absl/strings/str_format.h"
#include "examples/protos/helloworld.grpc.pb.h"

using grpc::CallbackServerContext;
using grpc::Server;
using grpc::ServerBuilder;
using grpc::ServerUnaryReactor;
using grpc::Status;
using helloworld::Greeter;
using helloworld::HelloReply;
using helloworld::HelloRequest;

ABSL_FLAG(uint16_t, port, 50051, "Server port for the service");

// Logic and data behind the server's behavior.
class GreeterServiceImpl final : public Greeter::CallbackService {
  ServerUnaryReactor* SayHello(CallbackServerContext* context,
                               const HelloRequest* request,
                               HelloReply* reply) override {
    ServerUnaryReactor* reactor = context->DefaultReactor();
    // Obtain the call metric recorder and use it to report the number of
    // DB queries (custom cost metric) and CPU utilization.
    auto recorder = context->ExperimentalGetCallMetricRecorder();
    if (recorder == nullptr) {
      reactor->Finish({grpc::StatusCode::INTERNAL,
                       "Unable to access metrics recorder. Make sure "
                       "EnableCallMetricRecording had been called."});
      return reactor;
    }
    recorder->RecordRequestCostMetric("db_queries", 10);
    recorder->RecordCpuUtilizationMetric(0.5);
    std::string prefix("Hello ");
    reply->set_message(prefix + request->name());
    reactor->Finish(Status::OK);
    return reactor;
  }
};

void RunServer(uint16_t port) {
  std::string server_address = absl::StrFormat("0.0.0.0:%d", port);
  ServerBuilder builder;
  GreeterServiceImpl service;
  // Setup custom metrics recording. Note that this recorder may be use to send
  // the out-of-band metrics to the client.
  auto server_metric_recorder =
      grpc::experimental::ServerMetricRecorder::Create();
  grpc::experimental::OrcaService orca_service(
      server_metric_recorder.get(),
      grpc::experimental::OrcaService::Options().set_min_report_duration(
          absl::Seconds(0.1)));
  builder.RegisterService(&orca_service);
  grpc::ServerBuilder::experimental_type(&builder).EnableCallMetricRecording(
      server_metric_recorder.get());
  // Resume setting up gRPC server as usual
  grpc::EnableDefaultHealthCheckService(true);
  // Listen on the given address without any authentication mechanism.
  builder.AddListeningPort(server_address, grpc::InsecureServerCredentials());
  // Register "service" as the instance through which we'll communicate with
  // clients. In this case it corresponds to an *synchronous* service.
  builder.RegisterService(&service);
  // Finally assemble the server.
  std::unique_ptr<Server> server(builder.BuildAndStart());
  std::cout << "Server listening on " << server_address << std::endl;
  server->Wait();
}

int main(int argc, char** argv) {
  absl::ParseCommandLine(argc, argv);
  RunServer(absl::GetFlag(FLAGS_port));
  return 0;
}
