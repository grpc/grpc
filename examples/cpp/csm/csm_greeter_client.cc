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
#include <optional>
#include <string>

#include "absl/flags/flag.h"
#include "absl/flags/parse.h"
#include "absl/strings/str_join.h"
#include "absl/strings/str_split.h"
#include "absl/types/optional.h"
#include "opentelemetry/exporters/prometheus/exporter_factory.h"
#include "opentelemetry/exporters/prometheus/exporter_options.h"
#include "opentelemetry/sdk/metrics/meter_provider.h"

#include <grpcpp/ext/csm_observability.h>
#include <grpcpp/grpcpp.h>
#include <grpcpp/support/string_ref.h>

#ifdef BAZEL_BUILD
#include "examples/protos/helloworld.grpc.pb.h"
#else
#include "helloworld.grpc.pb.h"
#endif

ABSL_FLAG(std::string, target, "xds:///helloworld:50051", "Target string");
ABSL_FLAG(std::string, cookie_name, "GSSA", "Cookie name");
ABSL_FLAG(uint, delay_s, 5, "Delay between requests");

using grpc::Channel;
using grpc::ClientContext;
using grpc::Status;
using helloworld::Greeter;
using helloworld::HelloReply;
using helloworld::HelloRequest;

namespace {

struct Cookie {
  std::string name;
  std::string value;
  std::set<std::string> attributes;

  template <typename Sink>
  friend void AbslStringify(Sink& sink, const Cookie& cookie) {
    absl::Format(&sink, "(Cookie: %s, value: %s, attributes: {%s})",
                 cookie.name, cookie.value,
                 absl::StrJoin(cookie.attributes, ", "));
  }
};

Cookie ParseCookie(absl::string_view header) {
  Cookie cookie;
  std::pair<absl::string_view, absl::string_view> name_value =
      absl::StrSplit(header, absl::MaxSplits('=', 1));
  cookie.name = std::string(name_value.first);
  std::pair<absl::string_view, absl::string_view> value_attrs =
      absl::StrSplit(name_value.second, absl::MaxSplits(';', 1));
  cookie.value = std::string(value_attrs.first);
  for (absl::string_view segment : absl::StrSplit(value_attrs.second, ';')) {
    cookie.attributes.emplace(absl::StripAsciiWhitespace(segment));
  }
  return cookie;
}

std::vector<Cookie> GetCookies(
    const std::multimap<grpc::string_ref, grpc::string_ref>& initial_metadata,
    absl::string_view cookie_name) {
  std::vector<Cookie> values;
  auto pair = initial_metadata.equal_range("set-cookie");
  for (auto it = pair.first; it != pair.second; ++it) {
    const auto cookie = ParseCookie(it->second.data());
    if (cookie.name == cookie_name) {
      values.emplace_back(std::move(cookie));
    }
  }
  return values;
}

class GreeterClient {
 public:
  GreeterClient(std::shared_ptr<Channel> channel, absl::string_view cookie_name)
      : stub_(Greeter::NewStub(channel)), cookie_name_(cookie_name) {}

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
    // Set the cookie header if we already got a cookie from the server
    if (cookie_from_server_.has_value()) {
      context.AddMetadata("cookie",
                          absl::StrFormat("%s=%s", cookie_from_server_->name,
                                          cookie_from_server_->value));
    }
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
    const std::multimap<grpc::string_ref, grpc::string_ref>&
        server_initial_metadata = context.GetServerInitialMetadata();
    // Update a cookie after a successful request
    std::vector<Cookie> cookies =
        GetCookies(server_initial_metadata, cookie_name_);
    if (!cookies.empty()) {
      cookie_from_server_.emplace(std::move(cookies.front()));
    }
    std::cout << "Greeter received: " << reply.message() << std::endl;
  }

 private:
  std::unique_ptr<Greeter::Stub> stub_;
  std::string cookie_name_;
  absl::optional<Cookie> cookie_from_server_;
};

absl::StatusOr<grpc::CsmObservability> InitializeObservability() {
  opentelemetry::exporter::metrics::PrometheusExporterOptions opts;
  // default was "localhost:9464" which causes connection issue across GKE pods
  opts.url = "0.0.0.0:9464";
  auto prometheus_exporter =
      opentelemetry::exporter::metrics::PrometheusExporterFactory::Create(opts);
  auto meter_provider =
      std::make_shared<opentelemetry::sdk::metrics::MeterProvider>();
  meter_provider->AddMetricReader(std::move(prometheus_exporter));
  return grpc::CsmObservabilityBuilder()
      .SetMeterProvider(std::move(meter_provider))
      .BuildAndRegister();
}

}  // namespace

int main(int argc, char** argv) {
  absl::ParseCommandLine(argc, argv);
  // Setup the observability
  auto observability = InitializeObservability();
  if (!observability.ok()) {
    std::cerr << "CsmObservability::Init() failed: "
              << observability.status().ToString() << std::endl;
    return static_cast<int>(observability.status().code());
  }
  GreeterClient greeter(grpc::CreateChannel(absl::GetFlag(FLAGS_target),
                                            grpc::InsecureChannelCredentials()),
                        absl::GetFlag(FLAGS_cookie_name));
  while (true) {
    greeter.SayHello();
    std::this_thread::sleep_for(
        std::chrono::seconds(absl::GetFlag(FLAGS_delay_s)));
  }
  return 0;
}
