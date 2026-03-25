//
// Copyright 2025 gRPC authors.
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

#include <grpc/support/string_util.h>

#include <string>
#include <vector>

#include "envoy/config/cluster/v3/cluster.pb.h"
#include "envoy/extensions/filters/http/ext_proc/v3/ext_proc.pb.h"
#include "envoy/extensions/filters/http/router/v3/router.pb.h"
#include "envoy/extensions/filters/network/http_connection_manager/v3/http_connection_manager.pb.h"
#include "envoy/extensions/grpc_service/call_credentials/access_token/v3/access_token_credentials.pb.h"
#include "envoy/extensions/grpc_service/channel_credentials/google_default/v3/google_default_credentials.pb.h"
#include "envoy/service/ext_proc/v3/external_processor.grpc.pb.h"
#include "src/core/client_channel/backup_poller.h"
#include "src/core/config/config_vars.h"
#include "src/core/ext/filters/ext_proc/ext_proc_filter.h"
#include "test/core/test_util/scoped_env_var.h"
#include "test/core/test_util/test_config.h"
#include "test/cpp/end2end/xds/xds_end2end_test_lib.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace grpc {
namespace testing {
namespace {

using ::envoy::extensions::filters::http::ext_proc::v3::ExternalProcessor;
using ::envoy::extensions::filters::network::http_connection_manager::v3::
    HttpFilter;

constexpr absl::string_view kFilterInstanceName = "ext_proc_instance";
constexpr absl::string_view kExtProcClusterName = "ext_proc_cluster";

class XdsExtProcEnd2endTest : public XdsEnd2endTest {
 public:
  void SetUp() override {
    grpc_core::SetEnv("GRPC_EXPERIMENTAL_XDS_EXT_PROC_ON_CLIENT", "true");
    InitClient(MakeBootstrapBuilder().SetAllowedGrpcService(),
               /*lb_expected_authority=*/"",
               /*xds_resource_does_not_exist_timeout_ms=*/0,
               /*balancer_authority_override=*/"", /*args=*/nullptr);
  }

  void TearDown() override { XdsEnd2endTest::TearDown(); }

  Listener BuildListenerWithExtProcFilter() {
    Listener listener = default_listener_;
    HttpConnectionManager hcm = ClientHcmAccessor().Unpack(listener);
    HttpFilter* filter0 = hcm.mutable_http_filters(0);
    *hcm.add_http_filters() = *filter0;
    filter0->set_name(kFilterInstanceName);
    ExternalProcessor ext_proc;
    auto* google_grpc = ext_proc.mutable_grpc_service()->mutable_google_grpc();
    google_grpc->set_target_uri("dns:server.example.com");
    google_grpc->add_channel_credentials_plugin()->PackFrom(
        envoy::extensions::grpc_service::channel_credentials::google_default::
            v3::GoogleDefaultCredentials());
    envoy::extensions::grpc_service::call_credentials::access_token::v3::
        AccessTokenCredentials call_creds;
    call_creds.set_token("foo");
    google_grpc->add_call_credentials_plugin()->PackFrom(call_creds);
    ext_proc.mutable_processing_mode()->set_request_header_mode(
        envoy::extensions::filters::http::ext_proc::v3::ProcessingMode::SEND);
    ext_proc.mutable_processing_mode()->set_response_header_mode(
        envoy::extensions::filters::http::ext_proc::v3::ProcessingMode::SEND);
    filter0->mutable_typed_config()->PackFrom(ext_proc);
    ClientHcmAccessor().Pack(hcm, &listener);
    return listener;
  }

  Listener BuildListenerWithExtProcFilterAndObservability() {
    Listener listener = default_listener_;
    HttpConnectionManager hcm = ClientHcmAccessor().Unpack(listener);
    HttpFilter* filter0 = hcm.mutable_http_filters(0);
    *hcm.add_http_filters() = *filter0;
    filter0->set_name(kFilterInstanceName);
    ExternalProcessor ext_proc;
    auto* google_grpc = ext_proc.mutable_grpc_service()->mutable_google_grpc();
    google_grpc->set_target_uri("dns:server.example.com");
    google_grpc->add_channel_credentials_plugin()->PackFrom(
        envoy::extensions::grpc_service::channel_credentials::google_default::
            v3::GoogleDefaultCredentials());
    envoy::extensions::grpc_service::call_credentials::access_token::v3::
        AccessTokenCredentials call_creds;
    call_creds.set_token("foo");
    google_grpc->add_call_credentials_plugin()->PackFrom(call_creds);
    ext_proc.mutable_processing_mode()->set_request_header_mode(
        envoy::extensions::filters::http::ext_proc::v3::ProcessingMode::SEND);
    ext_proc.mutable_processing_mode()->set_response_header_mode(
        envoy::extensions::filters::http::ext_proc::v3::ProcessingMode::SEND);
    ext_proc.mutable_processing_mode()->set_request_body_mode(
        envoy::extensions::filters::http::ext_proc::v3::ProcessingMode::GRPC);
    ext_proc.mutable_processing_mode()->set_response_body_mode(
        envoy::extensions::filters::http::ext_proc::v3::ProcessingMode::GRPC);
    ext_proc.set_observability_mode(true);
    filter0->mutable_typed_config()->PackFrom(ext_proc);
    ClientHcmAccessor().Pack(hcm, &listener);
    return listener;
  }
};

INSTANTIATE_TEST_SUITE_P(XdsTest, XdsExtProcEnd2endTest,
                         ::testing::Values(XdsTestType()), &XdsTestType::Name);

TEST_P(XdsExtProcEnd2endTest, Basic) {
  // Set xDS resources.
  CreateAndStartBackends(1, /*xds_enabled=*/false);
  SetListenerAndRouteConfiguration(
      balancer_.get(), BuildListenerWithExtProcFilter(), default_route_config_);

  // Configure ext_proc cluster
  Cluster ext_proc_cluster = default_cluster_;
  ext_proc_cluster.set_name(std::string(kExtProcClusterName));
  balancer_->ads_service()->SetCdsResource(ext_proc_cluster);
  EdsResourceArgs args({{"locality0", CreateEndpointsForBackends()}});
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
  std::multimap<std::string, std::string> server_initial_metadata;
  Status status = SendRpc(RpcOptions().set_echo_metadata_initially(true),
                          /*response=*/nullptr, &server_initial_metadata);
  EXPECT_TRUE(status.ok());
}

TEST_P(XdsExtProcEnd2endTest, ModificationHook) {
  // Set up hooks
  grpc_core::g_test_ext_proc_metadata_modifier =
      [](grpc_metadata_batch* metadata) {
        metadata->Append("x-ext-proc-test",
                         grpc_core::Slice::FromCopiedString("modified"),
                         [](absl::string_view, const grpc_core::Slice&) {});
      };

  // Set xDS resources.
  CreateAndStartBackends(1, /*xds_enabled=*/false);
  SetListenerAndRouteConfiguration(
      balancer_.get(), BuildListenerWithExtProcFilter(), default_route_config_);

  // Configure ext_proc cluster
  Cluster ext_proc_cluster = default_cluster_;
  ext_proc_cluster.set_name(std::string(kExtProcClusterName));
  balancer_->ads_service()->SetCdsResource(ext_proc_cluster);
  EdsResourceArgs args({{"locality0", CreateEndpointsForBackends()}});
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));

  std::multimap<std::string, std::string> server_initial_metadata;
  EchoResponse response;
  Status status = SendRpc(RpcOptions().set_echo_metadata_initially(true),
                          &response, &server_initial_metadata);

  // Clean up hooks
  grpc_core::g_test_ext_proc_metadata_modifier = nullptr;

  EXPECT_TRUE(status.ok());

  bool metadata_found = false;
  for (const auto& kv : server_initial_metadata) {
    if (kv.first == "x-ext-proc-test" && kv.second == "modified") {
      metadata_found = true;
      break;
    }
  }
  EXPECT_TRUE(metadata_found);
}

TEST_P(XdsExtProcEnd2endTest, ModificationHookClientToServerMessages) {
  grpc_core::g_test_ext_proc_client_to_server_message_modifier =
      [](grpc_core::MessageHandle* message) {
        EchoRequest request;
        request.set_message("modified_request");
        std::string serialized;
        request.SerializeToString(&serialized);

        auto* payload = (*message)->payload();
        payload->Clear();
        payload->Append(grpc_core::Slice::FromCopiedString(serialized));
      };

  CreateAndStartBackends(1, /*xds_enabled=*/false);
  SetListenerAndRouteConfiguration(
      balancer_.get(), BuildListenerWithExtProcFilter(), default_route_config_);

  Cluster ext_proc_cluster = default_cluster_;
  ext_proc_cluster.set_name(std::string(kExtProcClusterName));
  balancer_->ads_service()->SetCdsResource(ext_proc_cluster);
  EdsResourceArgs args({{"locality0", CreateEndpointsForBackends()}});
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));

  EchoResponse response;
  Status status = SendRpc(RpcOptions(), &response);

  grpc_core::g_test_ext_proc_client_to_server_message_modifier = nullptr;

  EXPECT_TRUE(status.ok());
  EXPECT_EQ(response.message(), "modified_request");
}

TEST_P(XdsExtProcEnd2endTest, ModificationHookServerToClientMessages) {
  grpc_core::g_test_ext_proc_server_to_client_message_modifier =
      [](grpc_core::MessageHandle* message) {
        EchoResponse response;
        response.set_message("modified_response");
        std::string serialized;
        response.SerializeToString(&serialized);

        auto* payload = (*message)->payload();
        payload->Clear();
        payload->Append(grpc_core::Slice::FromCopiedString(serialized));
      };

  CreateAndStartBackends(1, /*xds_enabled=*/false);
  SetListenerAndRouteConfiguration(
      balancer_.get(), BuildListenerWithExtProcFilter(), default_route_config_);

  Cluster ext_proc_cluster = default_cluster_;
  ext_proc_cluster.set_name(std::string(kExtProcClusterName));
  balancer_->ads_service()->SetCdsResource(ext_proc_cluster);
  EdsResourceArgs args({{"locality0", CreateEndpointsForBackends()}});
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));

  EchoResponse response;
  Status status = SendRpc(RpcOptions(), &response);

  grpc_core::g_test_ext_proc_server_to_client_message_modifier = nullptr;

  EXPECT_TRUE(status.ok());
  EXPECT_EQ(response.message(), "modified_response");
}

TEST_P(XdsExtProcEnd2endTest, ModificationHookServerInitialMetadata) {
  grpc_core::g_test_ext_proc_server_initial_metadata_modifier =
      [](grpc_metadata_batch* metadata) {
        metadata->Append("x-server-initial-metadata-test",
                         grpc_core::Slice::FromCopiedString("modified"),
                         [](absl::string_view, const grpc_core::Slice&) {});
      };

  CreateAndStartBackends(1, /*xds_enabled=*/false);
  SetListenerAndRouteConfiguration(
      balancer_.get(), BuildListenerWithExtProcFilter(), default_route_config_);

  Cluster ext_proc_cluster = default_cluster_;
  ext_proc_cluster.set_name(std::string(kExtProcClusterName));
  balancer_->ads_service()->SetCdsResource(ext_proc_cluster);
  EdsResourceArgs args({{"locality0", CreateEndpointsForBackends()}});
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));

  std::multimap<std::string, std::string> server_initial_metadata;
  Status status = SendRpc(RpcOptions(), nullptr, &server_initial_metadata);

  grpc_core::g_test_ext_proc_server_initial_metadata_modifier = nullptr;

  EXPECT_TRUE(status.ok());
  bool metadata_found = false;
  for (const auto& kv : server_initial_metadata) {
    if (kv.first == "x-server-initial-metadata-test" &&
        kv.second == "modified") {
      metadata_found = true;
      break;
    }
  }
  EXPECT_TRUE(metadata_found);
}

TEST_P(XdsExtProcEnd2endTest, ModificationHookServerTrailingMetadata) {
  grpc_core::g_test_ext_proc_server_trailing_metadata_modifier =
      [](grpc_metadata_batch* metadata) {
        metadata->Append("x-server-trailing-metadata-test",
                         grpc_core::Slice::FromCopiedString("modified"),
                         [](absl::string_view, const grpc_core::Slice&) {});
      };

  CreateAndStartBackends(1, /*xds_enabled=*/false);
  SetListenerAndRouteConfiguration(
      balancer_.get(), BuildListenerWithExtProcFilter(), default_route_config_);

  Cluster ext_proc_cluster = default_cluster_;
  ext_proc_cluster.set_name(std::string(kExtProcClusterName));
  balancer_->ads_service()->SetCdsResource(ext_proc_cluster);
  EdsResourceArgs args({{"locality0", CreateEndpointsForBackends()}});
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));

  auto SendRpcWithTrailingMetadata =
      [&](std::multimap<std::string, std::string>* server_trailing_metadata) {
        ClientContext context;
        EchoRequest request;
        EchoResponse response;
        request.set_message(kRequestMessage);
        Status status = stub_->Echo(&context, request, &response);
        if (server_trailing_metadata != nullptr) {
          for (const auto& [key, value] : context.GetServerTrailingMetadata()) {
            std::string header(key.data(), key.size());
            absl::AsciiStrToLower(&header);
            server_trailing_metadata->emplace(
                header, std::string(value.data(), value.size()));
          }
        }
        return status;
      };

  std::multimap<std::string, std::string> server_trailing_metadata;
  Status status = SendRpcWithTrailingMetadata(&server_trailing_metadata);

  grpc_core::g_test_ext_proc_server_trailing_metadata_modifier = nullptr;

  EXPECT_TRUE(status.ok());
  bool metadata_found = false;
  for (const auto& kv : server_trailing_metadata) {
    if (kv.first == "x-server-trailing-metadata-test" &&
        kv.second == "modified") {
      metadata_found = true;
      break;
    }
  }
  EXPECT_TRUE(metadata_found);
}

TEST_P(XdsExtProcEnd2endTest, ObservabilityMode) {
  grpc_core::g_test_ext_proc_metadata_modifier =
      [](grpc_metadata_batch* metadata) {
        metadata->Append("x-client-initial-metadata-test",
                         grpc_core::Slice::FromCopiedString("modified"),
                         [](absl::string_view, const grpc_core::Slice&) {});
      };
  grpc_core::g_test_ext_proc_client_to_server_message_modifier =
      [](grpc_core::MessageHandle* message) {
        EchoRequest request;
        request.set_message("modified_request");
        std::string serialized;
        request.SerializeToString(&serialized);
        auto* payload = (*message)->payload();
        payload->Clear();
        payload->Append(grpc_core::Slice::FromCopiedString(serialized));
      };
  grpc_core::g_test_ext_proc_server_to_client_message_modifier =
      [](grpc_core::MessageHandle* message) {
        EchoResponse response;
        response.set_message("modified_response");
        std::string serialized;
        response.SerializeToString(&serialized);
        auto* payload = (*message)->payload();
        payload->Clear();
        payload->Append(grpc_core::Slice::FromCopiedString(serialized));
      };

  // Set xDS resources.
  CreateAndStartBackends(1, /*xds_enabled=*/false);

  Listener listener = BuildListenerWithExtProcFilterAndObservability();

  SetListenerAndRouteConfiguration(balancer_.get(), listener,
                                   default_route_config_);

  // Configure ext_proc cluster
  Cluster ext_proc_cluster = default_cluster_;
  ext_proc_cluster.set_name(std::string(kExtProcClusterName));
  balancer_->ads_service()->SetCdsResource(ext_proc_cluster);
  EdsResourceArgs args({{"locality0", CreateEndpointsForBackends()}});
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));

  EchoResponse response;
  Status status = SendRpc(RpcOptions(), &response);
  EXPECT_TRUE(status.ok());
  EXPECT_EQ(response.message(), kRequestMessage);

  grpc_core::g_test_ext_proc_metadata_modifier = nullptr;
  grpc_core::g_test_ext_proc_client_to_server_message_modifier = nullptr;
  grpc_core::g_test_ext_proc_server_to_client_message_modifier = nullptr;
}

}  // namespace
}  // namespace testing
}  // namespace grpc

int main(int argc, char** argv) {
  grpc::testing::TestEnvironment env(&argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  // Make the backup poller poll very frequently in order to pick up
  // updates from all the subchannels's FDs.
  grpc_core::ConfigVars::Overrides overrides;
  overrides.client_channel_backup_poll_interval_ms = 1;
  grpc_core::ConfigVars::SetOverrides(overrides);
  grpc_init();
  const auto result = RUN_ALL_TESTS();
  grpc_shutdown();
  return result;
}
