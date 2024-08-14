// Copyright 2023 The gRPC Authors.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include "absl/flags/flag.h"
#include "absl/flags/parse.h"
#include "absl/strings/str_format.h"
#include "opentelemetry/exporters/otlp/otlp_grpc_log_record_exporter_factory.h"
#include "opentelemetry/exporters/otlp/otlp_grpc_log_record_exporter_options.h"
#include "opentelemetry/sdk/logs/logger_provider.h"
#include "opentelemetry/sdk/logs/logger_provider_factory.h"
#include "opentelemetry/sdk/logs/processor.h"
#include "opentelemetry/sdk/logs/simple_log_record_processor_factory.h"

#include <grpcpp/ext/otel_plugin.h>
#include <grpcpp/grpcpp.h>

#ifdef BAZEL_BUILD
#include "examples/protos/helloworld.grpc.pb.h"
#else
#include "helloworld.grpc.pb.h"
#endif

ABSL_FLAG(uint16_t, port, 50051, "Server port for the service");
ABSL_FLAG(std::string, otlp_endpoint, "localhost:4317",
          "OTLP ingestion endpoint");

using grpc::CallbackServerContext;
using grpc::Server;
using grpc::ServerBidiReactor;
using grpc::ServerBuilder;
using grpc::Status;
using helloworld::Greeter;
using helloworld::HelloReply;
using helloworld::HelloRequest;

// Logic behind the server's behavior.
class StreamingServiceImpl final : public Greeter::CallbackService {
  ServerBidiReactor<HelloRequest, HelloReply>* SayHelloBidiStream(
      CallbackServerContext* context) override {
    class Reactor : public ServerBidiReactor<HelloRequest, HelloReply> {
     public:
      explicit Reactor() { StartRead(&request_); }

      void OnReadDone(bool ok) override {
        if (!ok) {
          // Client cancelled it
          std::cout << "OnReadDone Cancelled!" << std::endl;
          return Finish(grpc::Status::CANCELLED);
        }
        response_.set_message(absl::StrCat("ack " + request_.name()));
        StartWrite(&response_);
      }

      void OnWriteDone(bool ok) override {
        if (!ok) {
          // Client cancelled it
          std::cout << "OnWriteDone Cancelled!" << std::endl;
          return Finish(grpc::Status::CANCELLED);
        }
        StartRead(&request_);
      }

      void OnDone() override { delete this; }

     private:
      HelloRequest request_;
      HelloReply response_;
    };

    return new Reactor();
  }
};

void RunServer(uint16_t port) {
  std::string server_address = absl::StrFormat("0.0.0.0:%d", port);
  StreamingServiceImpl service;

  ServerBuilder builder;
  // Listen on the given address without any authentication mechanism.
  builder.AddListeningPort(server_address, grpc::InsecureServerCredentials());
  // Register "service" as the instance through which we'll communicate with
  // clients. In this case it corresponds to an *synchronous* service.
  builder.RegisterService(&service);
  // Finally assemble the server.
  std::unique_ptr<Server> server(builder.BuildAndStart());
  std::cout << "Server listening on " << server_address << std::endl;

  // Wait for the server to shutdown. Note that some other thread must be
  // responsible for shutting down the server for this call to ever return.
  server->Wait();
}

int main(int argc, char** argv) {
  absl::ParseCommandLine(argc, argv);
  // Register a global gRPC OpenTelemetry plugin configured with an
  // OTLP-over-gRPC log record exporter.
  opentelemetry::exporter::otlp::OtlpGrpcLogRecordExporterOptions log_opts;
  log_opts.endpoint = absl::GetFlag(FLAGS_otlp_endpoint);
  // Create OTLP exporter instance
  auto exporter =
      opentelemetry::exporter::otlp::OtlpGrpcLogRecordExporterFactory::Create(
          log_opts);
  auto processor =
      opentelemetry::sdk::logs::SimpleLogRecordProcessorFactory::Create(
          std::move(exporter));
  std::shared_ptr<opentelemetry::logs::LoggerProvider> logger_provider =
      opentelemetry::sdk::logs::LoggerProviderFactory::Create(
          std::move(processor));
  auto status = grpc::OpenTelemetryPluginBuilder()
                    .SetLoggerProvider(std::move(logger_provider))
                    .BuildAndRegisterGlobal();
  if (!status.ok()) {
    std::cerr << "Failed to register gRPC OpenTelemetry Plugin: "
              << status.ToString() << std::endl;
    return static_cast<int>(status.code());
  }
  RunServer(absl::GetFlag(FLAGS_port));
  return 0;
}
