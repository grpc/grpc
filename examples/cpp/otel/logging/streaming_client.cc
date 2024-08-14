// Copyright 2024 The gRPC Authors.
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

#include <condition_variable>
#include <iostream>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "absl/flags/flag.h"
#include "absl/flags/parse.h"
#include "absl/strings/str_cat.h"
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

ABSL_FLAG(std::string, target, "localhost:50051", "Server address");
ABSL_FLAG(std::string, otlp_endpoint, "localhost:4317",
          "OTLP ingestion endpoint");

using grpc::Channel;
using grpc::ClientContext;
using grpc::Status;
using grpc::StatusCode;
using helloworld::Greeter;
using helloworld::HelloReply;
using helloworld::HelloRequest;

class StreamingClient
    : public grpc::ClientBidiReactor<HelloRequest, HelloReply> {
 public:
  StreamingClient(std::shared_ptr<Channel> channel)
      : stub_(Greeter::NewStub(channel)) {
    stub_->async()->SayHelloBidiStream(&context_, this);
    request_.set_name("Begin");
    StartWrite(&request_);
    StartCall();
  }

  void OnReadDone(bool ok) override {
    if (ok) {
      std::cout << "response message: " << response_.message() << std::endl;
      if (++counter_ < 10) {
        request_.set_name(absl::StrCat(counter_));
        StartWrite(&request_);
      } else {
        // Cancel after sending 10 messages
        context_.TryCancel();
      }
    }
  }

  void OnWriteDone(bool ok) override {
    if (ok) {
      StartRead(&response_);
    }
  }

  void OnDone(const grpc::Status& status) override {
    if (!status.ok()) {
      if (status.error_code() == StatusCode::CANCELLED) {
        // Eventually client will know here that call is cancelled.
        std::cout << "RPC Cancelled!" << std::endl;
      } else {
        std::cout << "RPC Failed: " << status.error_code() << ": "
                  << status.error_message() << std::endl;
      }
    }
    std::unique_lock<std::mutex> l(mu_);
    done_ = true;
    cv_.notify_all();
  }

  void Await() {
    std::unique_lock<std::mutex> l(mu_);
    while (!done_) {
      cv_.wait(l);
    }
  }

 private:
  std::unique_ptr<Greeter::Stub> stub_;
  size_t counter_ = 0;
  ClientContext context_;
  bool done_ = false;
  HelloRequest request_;
  HelloReply response_;
  std::mutex mu_;
  std::condition_variable cv_;
};

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
  // Instantiate the client. It requires a channel, out of which the actual RPCs
  // are created. This channel models a connection to an endpoint specified by
  // the argument "--target=" which is the only expected argument.
  std::string target_str = absl::GetFlag(FLAGS_target);
  StreamingClient client(
      grpc::CreateChannel(target_str, grpc::InsecureChannelCredentials()));
  client.Await();
  return 0;
}
