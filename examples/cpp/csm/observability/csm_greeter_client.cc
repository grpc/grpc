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

#include <sys/types.h>

#include <chrono>
#include <condition_variable>
#include <iostream>
#include <memory>
#include <string>

#include "absl/flags/flag.h"
#include "absl/flags/parse.h"
#include "absl/types/optional.h"
#include "opentelemetry/exporters/prometheus/exporter_factory.h"
#include "opentelemetry/exporters/prometheus/exporter_options.h"
#include "opentelemetry/sdk/metrics/meter_provider.h"

#include <grpcpp/ext/csm_observability.h>
#include <grpcpp/grpcpp.h>
#include <grpcpp/support/string_ref.h>

#ifdef BAZEL_BUILD
#include "examples/cpp/csm/observability/util.h"
#include "examples/protos/helloworld.grpc.pb.h"
#else
#include "helloworld.grpc.pb.h"
#include "util.h"
#endif

ABSL_FLAG(std::string, target, "xds:///helloworld:50051", "Target string");
ABSL_FLAG(uint, delay_s, 1, "Delay between requests");

using grpc::Channel;
using grpc::ClientContext;
using grpc::Status;
using helloworld::Greeter;
using helloworld::HelloReply;
using helloworld::HelloRequest;

namespace {

class GreeterClient {
 public:
  GreeterClient(std::shared_ptr<Channel> channel)
      : stub_(Greeter::NewStub(channel)) {}
  // Assembles the client's payload, sends it and presents the response back
  // from the server.
  void SayHello() {
    // Data we are sending to the server.
    HelloRequest request;
    request.set_name("world");
    // Container for the data we expect from the server.
    HelloReply reply;
    // Context for the client. It could be used to convey extra information to
    // the server and/or tweak certain RPC behaviors.
    ClientContext context;
    // The actual RPC.
    std::mutex mu;
    std::condition_variable cv;
    absl::optional<Status> status;
    std::unique_lock<std::mutex> lock(mu);
    stub_->async()->SayHello(&context, &request, &reply, [&](Status s) {
      std::lock_guard<std::mutex> lock(mu);
      status = std::move(s);
      cv.notify_one();
    });
    while (!status.has_value()) {
      cv.wait(lock);
    }
    if (!status->ok()) {
      std::cout << "RPC failed" << status->error_code() << ": "
                << status->error_message() << std::endl;
      return;
    }
    std::cout << "Greeter received: " << reply.message() << std::endl;
  }

 private:
  std::unique_ptr<Greeter::Stub> stub_;
};

absl::StatusOr<grpc::CsmObservability> InitializeObservability() {
  opentelemetry::exporter::metrics::PrometheusExporterOptions opts;
  // default was "localhost:9464" which causes connection issue across GKE pods
  opts.url = "0.0.0.0:9464";
  auto prometheus_exporter =
      opentelemetry::exporter::metrics::PrometheusExporterFactory::Create(opts);
  auto meter_provider =
      std::make_shared<opentelemetry::sdk::metrics::MeterProvider>();
  // The default histogram boundaries are not granular enough for RPCs. Override
  // the "grpc.client.attempt.duration" view as recommended by
  // https://github.com/grpc/proposal/blob/master/A66-otel-stats.md.
  AddLatencyView(meter_provider.get(), "grpc.client.attempt.duration", "s");
  meter_provider->AddMetricReader(std::move(prometheus_exporter));
  return grpc::CsmObservabilityBuilder()
      .SetMeterProvider(std::move(meter_provider))
      .BuildAndRegister();
}

}  // namespace

int main(int argc, char** argv) {
  absl::ParseCommandLine(argc, argv);
  // Setup CSM observability
  auto observability = InitializeObservability();
  if (!observability.ok()) {
    std::cerr << "CsmObservability::Init() failed: "
              << observability.status().ToString() << std::endl;
    return static_cast<int>(observability.status().code());
  }
  GreeterClient greeter(grpc::CreateChannel(
      absl::GetFlag(FLAGS_target), grpc::InsecureChannelCredentials()));
  while (true) {
    greeter.SayHello();
    std::this_thread::sleep_for(
        std::chrono::seconds(absl::GetFlag(FLAGS_delay_s)));
  }
  return 0;
}
