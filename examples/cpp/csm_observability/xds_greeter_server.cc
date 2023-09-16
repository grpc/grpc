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

#include <iostream>
#include <memory>
#include <string>

#include "absl/flags/flag.h"
#include "absl/flags/parse.h"
#include "absl/strings/str_cat.h"
#include "opentelemetry/exporters/prometheus/exporter_factory.h"
#include "opentelemetry/exporters/prometheus/exporter_options.h"
#include "opentelemetry/sdk/metrics/meter_provider.h"

#include <grpcpp/ext/admin_services.h>
#include <grpcpp/ext/csm_observability.h>
#include <grpcpp/ext/proto_server_reflection_plugin.h>
#include <grpcpp/grpcpp.h>
#include <grpcpp/health_check_service_interface.h>
#include <grpcpp/xds_server_builder.h>

#ifdef BAZEL_BUILD
#include "examples/protos/helloworld.grpc.pb.h"
#else
#include "helloworld.grpc.pb.h"
#endif

ABSL_FLAG(int32_t, port, 50051, "Server port for service.");
ABSL_FLAG(int32_t, maintenance_port, 50052,
          "Server port for maintenance if --secure is used.");
ABSL_FLAG(bool, secure, true, "Secure mode");

using grpc::CallbackServerContext;
using grpc::Server;
using grpc::ServerBuilder;
using grpc::ServerUnaryReactor;
using grpc::Status;
using helloworld::Greeter;
using helloworld::HelloReply;
using helloworld::HelloRequest;

// Logic and data behind the server's behavior.
class GreeterServiceImpl final : public Greeter::CallbackService {
  ServerUnaryReactor* SayHello(CallbackServerContext* context,
                               const HelloRequest* request,
                               HelloReply* reply) override {
    std::string prefix("Hello ");
    reply->set_message(prefix + request->name());

    ServerUnaryReactor* reactor = context->DefaultReactor();
    reactor->Finish(Status::OK);
    return reactor;
  }
};

void RunServer() {
  grpc::EnableDefaultHealthCheckService(true);
  grpc::reflection::InitProtoReflectionServerBuilderPlugin();
  int port = absl::GetFlag(FLAGS_port);
  int maintenance_port = absl::GetFlag(FLAGS_maintenance_port);
  grpc::XdsServerBuilder xds_builder;
  ServerBuilder builder;
  std::unique_ptr<Server> xds_enabled_server;
  std::unique_ptr<Server> server;
  GreeterServiceImpl service;
  // Register "service" as the instance through which we'll communicate with
  // clients. In this case it corresponds to an *synchronous* service.
  xds_builder.RegisterService(&service);
  if (absl::GetFlag(FLAGS_secure)) {
    // Listen on the given address with XdsServerCredentials and a fallback of
    // InsecureServerCredentials
    xds_builder.AddListeningPort(
        absl::StrCat("0.0.0.0:", port),
        grpc::XdsServerCredentials(grpc::InsecureServerCredentials()));
    xds_enabled_server = xds_builder.BuildAndStart();
    gpr_log(GPR_INFO, "Server starting on 0.0.0.0:%d", port);
    grpc::AddAdminServices(&builder);
    // For the maintenance server, do not use any authentication mechanism.
    builder.AddListeningPort(absl::StrCat("0.0.0.0:", maintenance_port),
                             grpc::InsecureServerCredentials());
    server = builder.BuildAndStart();
    gpr_log(GPR_INFO, "Maintenance server listening on 0.0.0.0:%d",
            maintenance_port);
  } else {
    grpc::AddAdminServices(&xds_builder);
    // Listen on the given address without any authentication mechanism.
    builder.AddListeningPort(absl::StrCat("0.0.0.0:", port),
                             grpc::InsecureServerCredentials());
    server = xds_builder.BuildAndStart();
    gpr_log(GPR_INFO, "Server listening on 0.0.0.0:%d", port);
  }

  // Wait for the server to shutdown. Note that some other thread must be
  // responsible for shutting down the server for this call to ever return.
  server->Wait();
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
  RunServer();
  return 0;
}
