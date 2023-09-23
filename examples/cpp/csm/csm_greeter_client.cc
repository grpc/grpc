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

#include <condition_variable>
#include <iostream>
#include <memory>
#include <mutex>
#include <string>

#include "absl/flags/flag.h"
#include "absl/flags/parse.h"
#include "absl/strings/str_split.h"
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

using grpc::Channel;
using grpc::ClientContext;
using grpc::Status;
using helloworld::Greeter;
using helloworld::HelloReply;
using helloworld::HelloRequest;

struct Cookie {
  std::string name;
  std::string value;
  std::set<std::string> attributes;

  std::pair<std::string, std::string> Header() const {
    return std::make_pair("cookie", absl::StrFormat("%s=%s", name, value));
  }

  template <typename Sink>
  friend void AbslStringify(Sink& sink, const Cookie& cookie) {
    absl::Format(&sink, "(Cookie: %s, value: %s, attributes: {%s})",
                 cookie.name, cookie.value,
                 absl::StrJoin(cookie.attributes, ", "));
  }
};

class GreeterClient {
 protected:
  static Cookie ParseCookie(absl::string_view header) {
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

  static std::vector<Cookie> GetCookies(
      const std::multimap<grpc::string_ref, grpc::string_ref>&
          server_initial_metadata,
      absl::string_view cookie_name) {
    std::vector<Cookie> values;
    auto pair = server_initial_metadata.equal_range("set-cookie");
    for (auto it = pair.first; it != pair.second; ++it) {
      gpr_log(GPR_INFO, "set-cookie header: %s", it->second.data());
      const auto cookie = ParseCookie(it->second.data());
      if (cookie.name == cookie_name) {
        values.emplace_back(cookie);
      }
    }
    return values;
  }

 public:
  GreeterClient(std::shared_ptr<Channel> channel)
      : stub_(Greeter::NewStub(channel)) {}

  // Assembles the client's payload, sends it and presents the response back
  // from the server.
  std::string SayHello(const std::string& user, Cookie* cookieFromServer,
                       const Cookie* cookieToServer) {
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
    if (cookieToServer != NULL) {
      std::pair<std::string, std::string> cookieHeader =
          cookieToServer->Header();
      context.AddMetadata(cookieHeader.first, cookieHeader.second);
    }
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
      if (cookieFromServer != NULL) {
        const std::multimap<grpc::string_ref, grpc::string_ref>&
            server_initial_metadata = context.GetServerInitialMetadata();
        std::vector<Cookie> cookies =
            GetCookies(server_initial_metadata, "GSSA");
        if (!cookies.empty()) {
          *cookieFromServer = cookies.front();
        }
      }
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

static void sayHello(GreeterClient& greeter, Cookie* cookieFromServer,
                     const Cookie* cookieToServer) {
  std::string user("world");
  std::string reply = greeter.SayHello(user, cookieFromServer, cookieToServer);
  std::cout << "Greeter received: " << reply << std::endl;
  std::this_thread::sleep_for(std::chrono::seconds(5));
}

int main(int argc, char** argv) {
  absl::ParseCommandLine(argc, argv);
  opentelemetry::exporter::metrics::PrometheusExporterOptions opts;
  // default was "localhost:9464" which causes connection issue across GKE pods
  opts.url = "0.0.0.0:9464";
  auto prometheus_exporter =
      opentelemetry::exporter::metrics::PrometheusExporterFactory::Create(opts);
  auto meter_provider =
      std::make_shared<opentelemetry::sdk::metrics::MeterProvider>();
  meter_provider->AddMetricReader(std::move(prometheus_exporter));
  auto observability = grpc::experimental::CsmObservabilityBuilder()
                           .SetMeterProvider(std::move(meter_provider))
                           .BuildAndRegister();
  if (!observability.ok()) {
    std::cerr << "CsmObservability::Init() failed: "
              << observability.status().ToString() << std::endl;
    return static_cast<int>(observability.status().code());
  }
  GreeterClient greeter(grpc::CreateChannel(
      absl::GetFlag(FLAGS_target), grpc::InsecureChannelCredentials()));

  Cookie session_cookie;
  sayHello(greeter, &session_cookie, NULL);
  while (true) {
    sayHello(greeter, NULL, &session_cookie);
  }
  return 0;
}
