//
//
// Copyright 2023 gRPC authors.
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

#include "test/cpp/interop/xds_interop_server_lib.h"

#include <memory>

#include "absl/strings/str_cat.h"
#include "absl/strings/str_split.h"

#include <grpc/grpc.h>
#include <grpc/support/log.h>
#include <grpc/support/time.h>
#include <grpcpp/ext/admin_services.h>
#include <grpcpp/ext/proto_server_reflection_plugin.h>
#include <grpcpp/server.h>
#include <grpcpp/server_builder.h>
#include <grpcpp/server_context.h>
#include <grpcpp/xds_server_builder.h>

#include "src/proto/grpc/testing/empty.pb.h"
#include "src/proto/grpc/testing/messages.pb.h"
#include "src/proto/grpc/testing/test.grpc.pb.h"
#include "test/cpp/end2end/test_health_check_service_impl.h"
#include "test/cpp/interop/pre_stop_hook_server.h"

namespace grpc {
namespace testing {
namespace {

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

constexpr absl::string_view kRpcBehaviorMetadataKey = "rpc-behavior";
constexpr absl::string_view kErrorCodeRpcBehavior = "error-code-";
constexpr absl::string_view kHostnameRpcBehaviorFilter = "hostname=";

std::vector<std::string> GetRpcBehaviorMetadata(ServerContext* context) {
  std::vector<std::string> rpc_behaviors;
  auto rpc_behavior_metadata =
      context->client_metadata().equal_range(grpc::string_ref(
          kRpcBehaviorMetadataKey.data(), kRpcBehaviorMetadataKey.length()));
  for (auto metadata = rpc_behavior_metadata.first;
       metadata != rpc_behavior_metadata.second; ++metadata) {
    auto value = metadata->second;
    for (auto behavior :
         absl::StrSplit(absl::string_view(value.data(), value.length()), ',')) {
      rpc_behaviors.emplace_back(behavior);
    }
  }
  return rpc_behaviors;
}

class TestServiceImpl : public TestService::Service {
 public:
  explicit TestServiceImpl(absl::string_view hostname,
                           absl::string_view server_id)
      : hostname_(hostname), server_id_(server_id) {}

  Status UnaryCall(ServerContext* context, const SimpleRequest* request,
                   SimpleResponse* response) override {
    response->set_server_id(server_id_);
    for (const auto& rpc_behavior : GetRpcBehaviorMetadata(context)) {
      auto maybe_status =
          GetStatusForRpcBehaviorMetadata(rpc_behavior, hostname_);
      if (maybe_status.has_value()) {
        return *maybe_status;
      }
    }
    if (request->response_size() > 0) {
      std::string payload(request->response_size(), '0');
      response->mutable_payload()->set_body(payload.c_str(),
                                            request->response_size());
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
  std::string server_id_;
};

class XdsUpdateHealthServiceImpl : public XdsUpdateHealthService::Service {
 public:
  explicit XdsUpdateHealthServiceImpl(
      HealthCheckServiceImpl* health_check_service,
      std::unique_ptr<PreStopHookServerManager> pre_stop_hook_server)
      : health_check_service_(health_check_service),
        pre_stop_hook_server_(std::move(pre_stop_hook_server)) {}

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

  Status SendHookRequest(ServerContext* /* context */,
                         const HookRequest* request,
                         HookResponse* /* response */) override {
    switch (request->command()) {
      case HookRequest::START:
        return pre_stop_hook_server_->Start(request->server_port(), 30 /* s */);
      case HookRequest::STOP:
        return pre_stop_hook_server_->Stop();
      case HookRequest::RETURN:
        pre_stop_hook_server_->Return(
            static_cast<StatusCode>(request->grpc_code_to_return()),
            request->grpc_status_description());
        return Status::OK;
      default:
        return Status(
            StatusCode::INVALID_ARGUMENT,
            absl::StrFormat("Invalid command %d", request->command()));
    }
  }

 private:
  HealthCheckServiceImpl* const health_check_service_;
  std::unique_ptr<PreStopHookServerManager> pre_stop_hook_server_;
};

class MaintenanceServices {
 public:
  MaintenanceServices()
      : update_health_service_(&health_check_service_,
                               std::make_unique<PreStopHookServerManager>()) {
    health_check_service_.SetStatus(
        "", grpc::health::v1::HealthCheckResponse::SERVING);
    health_check_service_.SetStatus(
        "grpc.testing.TestService",
        grpc::health::v1::HealthCheckResponse::SERVING);
    health_check_service_.SetStatus(
        "grpc.testing.XdsUpdateHealthService",
        grpc::health::v1::HealthCheckResponse::SERVING);
  }

  void AddToServerBuilder(ServerBuilder* builder) {
    builder->RegisterService(&health_check_service_);
    builder->RegisterService(&update_health_service_);
    builder->RegisterService(&hook_service_);
    grpc::AddAdminServices(builder);
  }

 private:
  HealthCheckServiceImpl health_check_service_;
  XdsUpdateHealthServiceImpl update_health_service_;
  HookServiceImpl hook_service_;
};
}  // namespace

absl::optional<grpc::Status> GetStatusForRpcBehaviorMetadata(
    absl::string_view header_value, absl::string_view hostname) {
  for (auto part : absl::StrSplit(header_value, ' ')) {
    if (absl::ConsumePrefix(&part, kHostnameRpcBehaviorFilter)) {
      gpr_log(GPR_INFO, "%s", std::string(part).c_str());
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
      // TODO (eugeneo): Add support for other behaviors as needed
      return Status(
          grpc::StatusCode::INVALID_ARGUMENT,
          absl::StrCat("Unsupported rpc behavior header: ", header_value));
    }
  }
  return absl::nullopt;
}

void RunServer(bool secure_mode, bool enable_csm_observability, int port,
               const int maintenance_port, absl::string_view hostname,
               absl::string_view server_id,
               const std::function<void(Server*)>& server_callback) {
  std::unique_ptr<Server> xds_enabled_server;
  std::unique_ptr<Server> server;
  TestServiceImpl service(hostname, server_id);

  grpc::reflection::InitProtoReflectionServerBuilderPlugin();
  MaintenanceServices maintenance_services;
  if (secure_mode) {
    XdsServerBuilder xds_builder;
    xds_builder.RegisterService(&service);
    xds_builder.AddListeningPort(
        absl::StrCat("0.0.0.0:", port),
        grpc::XdsServerCredentials(grpc::InsecureServerCredentials()));
    xds_enabled_server = xds_builder.BuildAndStart();
    gpr_log(GPR_INFO, "Server starting on 0.0.0.0:%d", port);
    ServerBuilder builder;
    maintenance_services.AddToServerBuilder(&builder);
    server = builder
                 .AddListeningPort(absl::StrCat("0.0.0.0:", maintenance_port),
                                   grpc::InsecureServerCredentials())
                 .BuildAndStart();
    gpr_log(GPR_INFO, "Maintenance server listening on 0.0.0.0:%d",
            maintenance_port);
  } else {
    // CSM Observability requires an xDS enabled server.
    auto builder = enable_csm_observability
                       ? std::make_unique<XdsServerBuilder>()
                       : std::make_unique<ServerBuilder>();
    maintenance_services.AddToServerBuilder(builder.get());
    server = builder
                 ->AddListeningPort(absl::StrCat("0.0.0.0:", port),
                                    grpc::InsecureServerCredentials())
                 .RegisterService(&service)
                 .BuildAndStart();
    gpr_log(GPR_INFO, "Server listening on 0.0.0.0:%d", port);
  }
  server_callback(server.get());
  server->Wait();
}

}  // namespace testing
}  // namespace grpc
