//
//
// Copyright 2020 gRPC authors.
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
//
//

#include <sstream>

#include "absl/flags/flag.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_split.h"
#include "absl/synchronization/mutex.h"

#include <grpc/grpc.h>
#include <grpc/support/log.h>
#include <grpc/support/time.h>
#include <grpcpp/ext/admin_services.h>
#include <grpcpp/ext/proto_server_reflection_plugin.h>
#include <grpcpp/health_check_service_interface.h>
#include <grpcpp/server.h>
#include <grpcpp/server_builder.h>
#include <grpcpp/server_context.h>
#include <grpcpp/xds_server_builder.h>

#include "src/core/lib/gpr/string.h"
#include "src/core/lib/gprpp/crash.h"
#include "src/core/lib/iomgr/gethostname.h"
#include "src/core/lib/transport/transport.h"
#include "src/proto/grpc/testing/empty.pb.h"
#include "src/proto/grpc/testing/messages.pb.h"
#include "src/proto/grpc/testing/test.grpc.pb.h"
#include "test/core/util/test_config.h"
#include "test/cpp/end2end/test_health_check_service_impl.h"
#include "test/cpp/util/test_config.h"

ABSL_FLAG(int32_t, port, 8080, "Server port for service.");
ABSL_FLAG(int32_t, maintenance_port, 8081,
          "Server port for maintenance if --security is \"secure\".");
ABSL_FLAG(std::string, server_id, "cpp_server",
          "Server ID to include in responses.");
ABSL_FLAG(bool, secure_mode, false,
          "If true, XdsServerCredentials are used, InsecureServerCredentials "
          "otherwise");

using grpc::Server;
using grpc::ServerBuilder;
using grpc::ServerContext;
using grpc::Status;
using grpc::XdsServerBuilder;
using grpc::testing::Empty;
using grpc::testing::HealthCheckServiceImpl;
using grpc::testing::SimpleRequest;
using grpc::testing::SimpleResponse;
using grpc::testing::TestService;
using grpc::testing::XdsUpdateHealthService;

namespace {
constexpr absl::string_view kRpcBehaviorMetadataKey = "rpc-behavior";
constexpr absl::string_view kErrorCodeRpcBehavior = "error-code-";
constexpr absl::string_view kHostnameRpcBehaviorFilter = "hostname=";

std::set<std::string> GetRpcBehaviorMetadata(ServerContext* context) {
  std::set<std::string> rpc_behaviors;
  auto rpc_behavior_metadata =
      context->client_metadata().equal_range(grpc::string_ref(
          kRpcBehaviorMetadataKey.data(), kRpcBehaviorMetadataKey.length()));
  for (auto metadata = rpc_behavior_metadata.first;
       metadata != rpc_behavior_metadata.second; ++metadata) {
    auto value = metadata->second;
    for (auto behavior :
         absl::StrSplit(absl::string_view(value.data(), value.length()), ',')) {
      rpc_behaviors.emplace(behavior);
    }
  }
  return rpc_behaviors;
}

absl::optional<grpc::Status> GetStatusForRpcBehaviorMetadata(
    absl::string_view header_value, absl::string_view hostname) {
  for (auto part : absl::StrSplit(header_value, ' ')) {
    if (absl::ConsumePrefix(&part, kHostnameRpcBehaviorFilter)) {
      if (part.empty()) {
        return Status(
            grpc::StatusCode::INVALID_ARGUMENT,
            absl::StrCat("Empty host name in the RPC behavior header: ",
                         header_value));
      }
      if (part != hostname) {
        gpr_log(
            GPR_DEBUG,
            "RPC behavior for a different host: \"%s\", this one is: \"%s\"",
            std::string(part).c_str(), std::string(hostname).c_str());
        return absl::nullopt;
      }
    } else if (absl::ConsumePrefix(&part, kErrorCodeRpcBehavior)) {
      grpc::StatusCode code;
      if (absl::SimpleAtoi(part, &code)) {
        return Status(
            code,
            absl::StrCat("Rpc failed as per the rpc-behavior header value: ",
                         header_value));
      } else {
        return Status(grpc::StatusCode::INVALID_ARGUMENT,
                      absl::StrCat("Invalid format for rpc-behavior header: ",
                                   header_value));
      }
    } else {
      gpr_log(GPR_ERROR, "Unimplemented RPC behavior: \"%s\"",
              std::string(part).c_str());
    }
  }
  return absl::nullopt;
}
}  // namespace

class TestServiceImpl : public TestService::Service {
 public:
  explicit TestServiceImpl(const std::string& hostname) : hostname_(hostname) {}

  Status UnaryCall(ServerContext* context, const SimpleRequest* /*request*/,
                   SimpleResponse* response) override {
    response->set_server_id(absl::GetFlag(FLAGS_server_id));
    for (const auto& rpc_behavior : GetRpcBehaviorMetadata(context)) {
      auto maybe_status =
          GetStatusForRpcBehaviorMetadata(rpc_behavior, hostname_);
      if (maybe_status.has_value()) {
        return *maybe_status;
      }
    }
    response->set_hostname(hostname_);
    context->AddInitialMetadata("hostname", hostname_);
    return Status::OK;
  }

  Status EmptyCall(ServerContext* context, const Empty* /*request*/,
                   Empty* /*response*/) override {
    context->AddInitialMetadata("hostname", hostname_);
    return Status::OK;
  }

 private:
  std::string hostname_;
};

class XdsUpdateHealthServiceImpl : public XdsUpdateHealthService::Service {
 public:
  explicit XdsUpdateHealthServiceImpl(
      HealthCheckServiceImpl* health_check_service)
      : health_check_service_(health_check_service) {}

  Status SetServing(ServerContext* /* context */, const Empty* /* request */,
                    Empty* /* response */) override {
    health_check_service_->SetAll(
        grpc::health::v1::HealthCheckResponse::SERVING);
    return Status::OK;
  }

  Status SetNotServing(ServerContext* /* context */, const Empty* /* request */,
                       Empty* /* response */) override {
    health_check_service_->SetAll(
        grpc::health::v1::HealthCheckResponse::NOT_SERVING);
    return Status::OK;
  }

 private:
  HealthCheckServiceImpl* const health_check_service_;
};

void RunServer(bool secure_mode, const int port, const int maintenance_port,
               const std::string& hostname) {
  std::unique_ptr<Server> xds_enabled_server;
  std::unique_ptr<Server> server;
  TestServiceImpl service(hostname);
  HealthCheckServiceImpl health_check_service;
  health_check_service.SetStatus(
      "", grpc::health::v1::HealthCheckResponse::SERVING);
  health_check_service.SetStatus(
      "grpc.testing.TestService",
      grpc::health::v1::HealthCheckResponse::SERVING);
  health_check_service.SetStatus(
      "grpc.testing.XdsUpdateHealthService",
      grpc::health::v1::HealthCheckResponse::SERVING);
  XdsUpdateHealthServiceImpl update_health_service(&health_check_service);

  grpc::reflection::InitProtoReflectionServerBuilderPlugin();
  ServerBuilder builder;
  if (secure_mode) {
    XdsServerBuilder xds_builder;
    xds_builder.RegisterService(&service);
    xds_builder.AddListeningPort(
        absl::StrCat("0.0.0.0:", port),
        grpc::XdsServerCredentials(grpc::InsecureServerCredentials()));
    xds_enabled_server = xds_builder.BuildAndStart();
    gpr_log(GPR_INFO, "Server starting on 0.0.0.0:%d", port);
    builder.RegisterService(&health_check_service);
    builder.RegisterService(&update_health_service);
    grpc::AddAdminServices(&builder);
    builder.AddListeningPort(absl::StrCat("0.0.0.0:", maintenance_port),
                             grpc::InsecureServerCredentials());
    server = builder.BuildAndStart();
    gpr_log(GPR_INFO, "Maintenance server listening on 0.0.0.0:%d",
            maintenance_port);
  } else {
    builder.RegisterService(&service);
    builder.RegisterService(&health_check_service);
    builder.RegisterService(&update_health_service);
    grpc::AddAdminServices(&builder);
    builder.AddListeningPort(absl::StrCat("0.0.0.0:", port),
                             grpc::InsecureServerCredentials());
    server = builder.BuildAndStart();
    gpr_log(GPR_INFO, "Server listening on 0.0.0.0:%d", port);
  }

  server->Wait();
}

int main(int argc, char** argv) {
  grpc::testing::TestEnvironment env(&argc, argv);
  grpc::testing::InitTest(&argc, &argv, true);
  char* hostname = grpc_gethostname();
  if (hostname == nullptr) {
    std::cout << "Failed to get hostname, terminating" << std::endl;
    return 1;
  }
  int port = absl::GetFlag(FLAGS_port);
  if (port == 0) {
    std::cout << "Invalid port, terminating" << std::endl;
    return 1;
  }
  int maintenance_port = absl::GetFlag(FLAGS_maintenance_port);
  if (maintenance_port == 0) {
    std::cout << "Invalid maintenance port, terminating" << std::endl;
    return 1;
  }
  grpc::EnableDefaultHealthCheckService(false);
  RunServer(absl::GetFlag(FLAGS_secure_mode), port, maintenance_port, hostname);

  return 0;
}
