/*
 *
 * Copyright 2021 gRPC authors.
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

// Explicitly define HAVE_ABSEIL to avoid conflict with OTel's Abseil
// version. Refer
// https://github.com/open-telemetry/opentelemetry-cpp/issues/1042.
#ifndef HAVE_ABSEIL
#define HAVE_ABSEIL
#endif

#include <grpcpp/ext/otel_plugin.h>
#include <grpcpp/grpcpp.h>

#include <condition_variable>
#include <mutex>
#include <string>

#include "absl/flags/flag.h"
#include "absl/flags/parse.h"
#include "absl/log/initialize.h"
#include "opentelemetry/exporters/prometheus/exporter_factory.h"
#include "opentelemetry/exporters/prometheus/exporter_options.h"
#include "opentelemetry/sdk/metrics/meter_provider.h"

#ifdef BAZEL_BUILD
#include "examples/cpp/otel/codelab/util.h"
#include "examples/protos/helloworld.grpc.pb.h"
#else
#include "helloworld.grpc.pb.h"
#include "util.h"
#endif

ABSL_FLAG(std::string, target, "localhost:50051", "Server address");
ABSL_FLAG(std::string, prometheus_endpoint, "localhost:9465",
          "Prometheus exporter endpoint");

namespace {

using grpc::Channel;
using grpc::ClientContext;
using grpc::Status;
using helloworld::Greeter;
using helloworld::HelloReply;
using helloworld::HelloRequest;

class GreeterClient {
 public:
  GreeterClient(std::shared_ptr<Channel> channel)
      : stub_(Greeter::NewStub(channel)) {}

  // Assembles the client's payload, sends it and presents the response back
  // from the server.
  std::string SayHello(const std::string& user) {
    // Data we are sending to the server.
    HelloRequest request;
    request.set_name(user);

    // Container for the data we expect from the server.
    HelloReply reply;

    // Context for the client. It could be used to convey extra information to
    // the server and/or tweak certain RPC behaviors.
    ClientContext context;

    // The actual RPC.
    std::mutex mu;
    std::condition_variable cv;
    bool done = false;
    Status status;
    stub_->async()->SayHello(&context, &request, &reply,
                             [&mu, &cv, &done, &status](Status s) {
                               status = std::move(s);
                               std::lock_guard<std::mutex> lock(mu);
                               done = true;
                               cv.notify_one();
                             });

    std::unique_lock<std::mutex> lock(mu);
    while (!done) {
      cv.wait(lock);
    }

    // Act upon its status.
    if (status.ok()) {
      return reply.message();
    } else {
      std::cout << status.error_code() << ": " << status.error_message()
                << std::endl;
      return "RPC failed";
    }
  }

 private:
  std::unique_ptr<Greeter::Stub> stub_;
};

void RunClient(const std::string& target_str) {
  // Instantiate the client. It requires a channel, out of which the actual RPCs
  // are created. This channel models a connection to an endpoint specified by
  // the argument "--target=" which is the only expected argument.
  grpc::ChannelArguments args;
  // Continuously send RPCs every second.
  while (true) {
    GreeterClient greeter(grpc::CreateCustomChannel(
        target_str, grpc::InsecureChannelCredentials(), args));
    std::string user("world");
    std::string reply = greeter.SayHello(user);
    std::cout << "Greeter received: " << reply << std::endl;
    std::this_thread::sleep_for(std::chrono::seconds(1));
  }
}

}  // namespace

int main(int argc, char** argv) {
  absl::ParseCommandLine(argc, argv);
  absl::InitializeLog();
  // Register a global gRPC OpenTelemetry plugin configured with a prometheus
  // exporter.
  opentelemetry::exporter::metrics::PrometheusExporterOptions opts;
  opts.url = absl::GetFlag(FLAGS_prometheus_endpoint);
  auto prometheus_exporter =
      opentelemetry::exporter::metrics::PrometheusExporterFactory::Create(opts);
  auto meter_provider =
      std::make_shared<opentelemetry::sdk::metrics::MeterProvider>();
  // The default histogram boundaries are not granular enough for RPCs. Override
  // the "grpc.client.attempt.duration" view as recommended by
  // https://github.com/grpc/proposal/blob/master/A66-otel-stats.md.
  AddLatencyView(meter_provider.get(), "grpc.client.attempt.duration", "s");
  meter_provider->AddMetricReader(std::move(prometheus_exporter));
  auto status = grpc::OpenTelemetryPluginBuilder()
                    .SetMeterProvider(std::move(meter_provider))
                    .BuildAndRegisterGlobal();
  if (!status.ok()) {
    std::cerr << "Failed to register gRPC OpenTelemetry Plugin: "
              << status.ToString() << std::endl;
    return static_cast<int>(status.code());
  }

  // Continuously send RPCs.
  RunClient(absl::GetFlag(FLAGS_target));

  return 0;
}
