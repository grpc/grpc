//
// Copyright 2026 gRPC authors.
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

#include <google/protobuf/wrappers.pb.h>
#include <grpc/support/string_util.h>

#include <map>
#include <mutex>
#include <string>
#include <vector>

#include "envoy/config/cluster/v3/cluster.pb.h"
#include "envoy/config/common/mutation_rules/v3/mutation_rules.pb.h"
#include "envoy/extensions/filters/http/ext_proc/v3/ext_proc.pb.h"
#include "envoy/extensions/filters/http/router/v3/router.pb.h"
#include "envoy/extensions/filters/network/http_connection_manager/v3/http_connection_manager.pb.h"
#include "envoy/extensions/grpc_service/call_credentials/access_token/v3/access_token_credentials.pb.h"
#include "envoy/extensions/grpc_service/channel_credentials/google_default/v3/google_default_credentials.pb.h"
#include "envoy/extensions/grpc_service/channel_credentials/insecure/v3/insecure_credentials.pb.h"
#include "envoy/service/ext_proc/v3/external_processor.grpc.pb.h"
#include "envoy/type/v3/http_status.pb.h"
#include "src/core/client_channel/backup_poller.h"
#include "src/core/config/config_vars.h"
#include "src/core/ext/filters/ext_proc/ext_proc_filter.h"
#include "src/core/lib/experiments/config.h"
#include "test/core/test_util/scoped_env_var.h"
#include "test/core/test_util/test_config.h"
#include "test/cpp/end2end/xds/xds_end2end_test_lib.h"
#include "test/cpp/end2end/xds/xds_utils.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/log/log.h"
#include "absl/synchronization/mutex.h"

namespace grpc {
namespace testing {
namespace {

using ::envoy::extensions::filters::http::ext_proc::v3::ExternalProcessor;
using ::envoy::extensions::filters::network::http_connection_manager::v3::
    HttpFilter;

class ExternalProcessorBuilder {
 public:
  ExternalProcessorBuilder() {}

  ExternalProcessorBuilder& SetTargetUri(const std::string& target_uri) {
    auto* google_grpc = ext_proc_.mutable_grpc_service()->mutable_google_grpc();
    google_grpc->set_target_uri(target_uri);
    return *this;
  }

  ExternalProcessorBuilder& SetInsecureChannelCredentials() {
    auto* google_grpc = ext_proc_.mutable_grpc_service()->mutable_google_grpc();
    google_grpc->clear_channel_credentials_plugin();
    google_grpc->add_channel_credentials_plugin()->PackFrom(
        envoy::extensions::grpc_service::channel_credentials::insecure::v3::
            InsecureCredentials());
    return *this;
  }

  ExternalProcessorBuilder& SetGoogleDefaultChannelCredentials() {
    auto* google_grpc = ext_proc_.mutable_grpc_service()->mutable_google_grpc();
    google_grpc->clear_channel_credentials_plugin();
    google_grpc->add_channel_credentials_plugin()->PackFrom(
        envoy::extensions::grpc_service::channel_credentials::google_default::
            v3::GoogleDefaultCredentials());
    return *this;
  }

  ExternalProcessorBuilder& SetAccessTokenCallCredentials(
      const std::string& token) {
    auto* google_grpc = ext_proc_.mutable_grpc_service()->mutable_google_grpc();
    google_grpc->clear_call_credentials_plugin();
    envoy::extensions::grpc_service::call_credentials::access_token::v3::
        AccessTokenCredentials call_creds;
    call_creds.set_token(token);
    google_grpc->add_call_credentials_plugin()->PackFrom(call_creds);
    return *this;
  }

  ExternalProcessorBuilder& SetFailureModeAllow(bool allow) {
    ext_proc_.set_failure_mode_allow(allow);
    return *this;
  }

  ExternalProcessorBuilder& SetProcessingMode(
      const envoy::extensions::filters::http::ext_proc::v3::ProcessingMode&
          mode) {
    *ext_proc_.mutable_processing_mode() = mode;
    return *this;
  }

  ExternalProcessorBuilder& SetRequestHeaderMode(
      envoy::extensions::filters::http::ext_proc::v3::ProcessingMode::
          HeaderSendMode mode) {
    ext_proc_.mutable_processing_mode()->set_request_header_mode(mode);
    return *this;
  }

  ExternalProcessorBuilder& SetResponseHeaderMode(
      envoy::extensions::filters::http::ext_proc::v3::ProcessingMode::
          HeaderSendMode mode) {
    ext_proc_.mutable_processing_mode()->set_response_header_mode(mode);
    return *this;
  }

  ExternalProcessorBuilder& SetRequestBodyMode(
      envoy::extensions::filters::http::ext_proc::v3::ProcessingMode::
          BodySendMode mode) {
    ext_proc_.mutable_processing_mode()->set_request_body_mode(mode);
    return *this;
  }

  ExternalProcessorBuilder& SetResponseBodyMode(
      envoy::extensions::filters::http::ext_proc::v3::ProcessingMode::
          BodySendMode mode) {
    ext_proc_.mutable_processing_mode()->set_response_body_mode(mode);
    return *this;
  }

  ExternalProcessorBuilder& SetResponseTrailerMode(
      envoy::extensions::filters::http::ext_proc::v3::ProcessingMode::
          HeaderSendMode mode) {
    ext_proc_.mutable_processing_mode()->set_response_trailer_mode(mode);
    return *this;
  }

  ExternalProcessorBuilder& AddRequestAttribute(const std::string& attribute) {
    ext_proc_.add_request_attributes(attribute);
    return *this;
  }

  ExternalProcessorBuilder& AddResponseAttribute(const std::string& attribute) {
    ext_proc_.add_response_attributes(attribute);
    return *this;
  }

  ExternalProcessorBuilder& SetMutationRules(
      const envoy::config::common::mutation_rules::v3::HeaderMutationRules&
          rules) {
    *ext_proc_.mutable_mutation_rules() = rules;
    return *this;
  }

  ExternalProcessorBuilder& SetForwardingRules(
      const envoy::extensions::filters::http::ext_proc::v3::
          HeaderForwardingRules& rules) {
    *ext_proc_.mutable_forward_rules() = rules;
    return *this;
  }

  ExternalProcessorBuilder& SetDisableImmediateResponse(bool disable) {
    ext_proc_.set_disable_immediate_response(disable);
    return *this;
  }

  ExternalProcessorBuilder& SetObservabilityMode(bool observability_mode) {
    ext_proc_.set_observability_mode(observability_mode);
    return *this;
  }

  ExternalProcessorBuilder& SetDeferredCloseTimeout(
      const google::protobuf::Duration& timeout) {
    *ext_proc_.mutable_deferred_close_timeout() = timeout;
    return *this;
  }

  envoy::extensions::filters::http::ext_proc::v3::ExternalProcessor Build() {
    return ext_proc_;
  }

 private:
  envoy::extensions::filters::http::ext_proc::v3::ExternalProcessor ext_proc_;
};

constexpr absl::string_view kFilterInstanceName = "ext_proc_instance";
constexpr absl::string_view kExtProcClusterName = "ext_proc_cluster";

class XdsExtProcEnd2endTest : public XdsEnd2endTest {
 public:
  void SetUp() override {
    grpc_core::SetEnv("GRPC_EXPERIMENTAL_XDS_EXT_PROC_ON_CLIENT", "true");
    grpc_core::SetEnv("GRPC_TRACE", "ext_proc_filter,promise_primitives");
    InitClient(MakeBootstrapBuilder().SetTrustedXdsServer(),
               /*lb_expected_authority=*/"",
               /*xds_resource_does_not_exist_timeout_ms=*/0,
               /*balancer_authority_override=*/"", /*args=*/nullptr);
  }

  void TearDown() override { XdsEnd2endTest::TearDown(); }

  Listener BuildListenerWithExtProcFilter(const ExternalProcessor& ext_proc) {
    Listener listener = default_listener_;
    HttpConnectionManager hcm = ClientHcmAccessor().Unpack(listener);
    HttpFilter* filter0 = hcm.mutable_http_filters(0);
    *hcm.add_http_filters() = *filter0;
    filter0->set_name(kFilterInstanceName);
    filter0->mutable_typed_config()->PackFrom(ext_proc);
    ClientHcmAccessor().Pack(hcm, &listener);
    return listener;
  }

  Status SendRpcGetTrailers(
      const RpcOptions& rpc_options, EchoResponse* response,
      std::multimap<std::string, std::string>* server_initial_metadata,
      std::multimap<std::string, std::string>* server_trailing_metadata) {
    EchoResponse local_response;
    if (response == nullptr) response = &local_response;
    ClientContext context;
    EchoRequest request;
    if (rpc_options.server_expected_error != StatusCode::OK) {
      auto* error = request.mutable_param()->mutable_expected_error();
      error->set_code(rpc_options.server_expected_error);
    }
    rpc_options.SetupRpc(&context, &request);
    Status status;
    switch (rpc_options.service) {
      case SERVICE_ECHO:
        status = SendRpcMethod(stub_.get(), rpc_options, &context, request,
                               response);
        break;
      case SERVICE_ECHO1:
        status = SendRpcMethod(stub1_.get(), rpc_options, &context, request,
                               response);
        break;
      case SERVICE_ECHO2:
        status = SendRpcMethod(stub2_.get(), rpc_options, &context, request,
                               response);
        break;
    }
    if (server_initial_metadata != nullptr) {
      for (const auto& [key, value] : context.GetServerInitialMetadata()) {
        std::string header(key.data(), key.size());
        absl::AsciiStrToLower(&header);
        server_initial_metadata->emplace(
            header, std::string(value.data(), value.size()));
      }
    }
    if (server_trailing_metadata != nullptr) {
      for (const auto& [key, value] : context.GetServerTrailingMetadata()) {
        std::string header(key.data(), key.size());
        absl::AsciiStrToLower(&header);
        server_trailing_metadata->emplace(
            header, std::string(value.data(), value.size()));
      }
    }
    return status;
  }
};

INSTANTIATE_TEST_SUITE_P(XdsTest, XdsExtProcEnd2endTest,
                         ::testing::Values(XdsTestType()), &XdsTestType::Name);

using MockExternalProcessorBase =
    ::envoy::service::ext_proc::v3::ExternalProcessor::Service;

class MockRequestHeadersExternalProcessorService
    : public MockExternalProcessorBase {
 public:
  enum class Behavior {
    kContinue,
    kFail,
    kMutateHeaders,
    kObservabilityMode,
    kRemoveHeaders,
    kMutateReservedHeader,
    kMutateTrailers,
  };

  void SetBehavior(Behavior behavior) { behavior_ = behavior; }

  std::map<std::string, std::string> last_received_attributes() {
    absl::MutexLock lock(&mu_);
    return last_received_attributes_;
  }

  bool has_protocol_config_in_request_headers() {
    absl::MutexLock lock(&mu_);
    return has_protocol_config_in_request_headers_;
  }

  bool has_protocol_config_in_response_headers() {
    absl::MutexLock lock(&mu_);
    return has_protocol_config_in_response_headers_;
  }

  ::grpc::Status Process(
      ::grpc::ServerContext* /*context*/,
      ::grpc::ServerReaderWriter<
          ::envoy::service::ext_proc::v3::ProcessingResponse,
          ::envoy::service::ext_proc::v3::ProcessingRequest>* stream) override {
    LOG(INFO) << "MockRequestHeadersExternalProcessorService::Process started";
    ::envoy::service::ext_proc::v3::ProcessingRequest request;
    while (stream->Read(&request)) {
      LOG(INFO) << "MockRequestHeadersExternalProcessorService::Process read "
                   "request: "
                << request.DebugString();
      if (request.has_protocol_config()) {
        absl::MutexLock lock(&mu_);
        if (request.has_request_headers()) {
          has_protocol_config_in_request_headers_ = true;
        } else if (request.has_response_headers()) {
          has_protocol_config_in_response_headers_ = true;
        }
      }
      ::envoy::service::ext_proc::v3::ProcessingResponse response;
      if (request.has_request_headers()) {
        {
          absl::MutexLock lock(&mu_);
          last_received_attributes_.clear();
          auto it = request.attributes().find("envoy.filters.http.ext_proc");
          if (it != request.attributes().end()) {
            const auto& fields = it->second.fields();
            for (const auto& [key, val] : fields) {
              if (val.has_string_value()) {
                last_received_attributes_[key] = val.string_value();
              }
            }
          }
        }
        if (behavior_ == Behavior::kFail) {
          auto* immediate = response.mutable_immediate_response();
          immediate->mutable_status()->set_code(
              envoy::type::v3::StatusCode::Forbidden);
          immediate->mutable_grpc_status()->set_status(
              static_cast<uint32_t>(::grpc::StatusCode::PERMISSION_DENIED));
          immediate->set_details("rejected by ext_proc");
        } else if (behavior_ == Behavior::kMutateHeaders) {
          auto* header_mutation = response.mutable_request_headers()
                                      ->mutable_response()
                                      ->mutable_header_mutation();
          auto* header = header_mutation->add_set_headers();
          header->mutable_header()->set_key("x-ext-proc-test");
          header->mutable_header()->set_value("e2e-modified");
        } else if (behavior_ == Behavior::kRemoveHeaders) {
          auto* header_mutation = response.mutable_request_headers()
                                      ->mutable_response()
                                      ->mutable_header_mutation();
          header_mutation->add_remove_headers("x-to-be-removed");
        } else if (behavior_ == Behavior::kMutateReservedHeader) {
          auto* header_mutation = response.mutable_request_headers()
                                      ->mutable_response()
                                      ->mutable_header_mutation();
          auto* header = header_mutation->add_set_headers();
          header->mutable_header()->set_key(":path");
          header->mutable_header()->set_value("/mutated-path");
        } else {
          response.mutable_request_headers()
              ->mutable_response()
              ->mutable_header_mutation();  // just continue without mutations
        }
      } else if (request.has_response_headers()) {
        if (behavior_ == Behavior::kMutateHeaders) {
          auto* header_mutation = response.mutable_response_headers()
                                      ->mutable_response()
                                      ->mutable_header_mutation();
          auto* header = header_mutation->add_set_headers();
          header->mutable_header()->set_key("x-ext-proc-test-response");
          header->mutable_header()->set_value("e2e-modified-response");
        } else if (behavior_ == Behavior::kRemoveHeaders) {
          auto* header_mutation = response.mutable_response_headers()
                                      ->mutable_response()
                                      ->mutable_header_mutation();
          header_mutation->add_remove_headers("x-to-be-removed-response");
        } else if (behavior_ == Behavior::kMutateReservedHeader) {
          auto* header_mutation = response.mutable_response_headers()
                                      ->mutable_response()
                                      ->mutable_header_mutation();
          auto* header = header_mutation->add_set_headers();
          header->mutable_header()->set_key(":status");
          header->mutable_header()->set_value("500");
        } else {
          response.mutable_response_headers()
              ->mutable_response()
              ->mutable_header_mutation();  // just continue without mutations
        }
      } else if (request.has_response_trailers()) {
        if (behavior_ == Behavior::kMutateTrailers) {
          auto* header_mutation =
              response.mutable_response_trailers()->mutable_header_mutation();
          auto* header = header_mutation->add_set_headers();
          header->mutable_header()->set_key("x-ext-proc-test-trailer");
          header->mutable_header()->set_value("e2e-modified-trailer");
          header->set_append_action(envoy::config::core::v3::HeaderValueOption::
                                        OVERWRITE_IF_EXISTS_OR_ADD);
        } else if (behavior_ == Behavior::kRemoveHeaders) {
          auto* header_mutation =
              response.mutable_response_trailers()->mutable_header_mutation();
          header_mutation->add_remove_headers("x-to-be-removed-trailer");
        } else if (behavior_ == Behavior::kMutateReservedHeader) {
          auto* header_mutation =
              response.mutable_response_trailers()->mutable_header_mutation();
          auto* header = header_mutation->add_set_headers();
          header->mutable_header()->set_key("grpc-status");
          header->mutable_header()->set_value("1");  // Cancelled
        } else {
          response.mutable_response_trailers()
              ->mutable_header_mutation();  // just continue without mutations
        }
      }
      if (behavior_ == Behavior::kObservabilityMode) {
        continue;
      }
      LOG(INFO) << "MockRequestHeadersExternalProcessorService::Process "
                   "writing response: "
                << response.DebugString();
      bool write_ok = stream->Write(response);
      LOG(INFO) << "MockRequestHeadersExternalProcessorService::Process write "
                   "done, ok="
                << write_ok;
    }
    LOG(INFO) << "MockRequestHeadersExternalProcessorService::Process finished";
    return ::grpc::Status::OK;
  }

 private:
  Behavior behavior_ = Behavior::kContinue;
  absl::Mutex mu_;
  std::map<std::string, std::string> last_received_attributes_
      ABSL_GUARDED_BY(mu_);
  bool has_protocol_config_in_request_headers_ ABSL_GUARDED_BY(mu_) = false;
  bool has_protocol_config_in_response_headers_ ABSL_GUARDED_BY(mu_) = false;
};

TEST_P(XdsExtProcEnd2endTest,
       ClientInitialMetadataMutatedWhenRequestHeaderProcessingModeIsEnabled) {
  int ext_proc_port = grpc_pick_unused_port_or_die();
  std::string server_address = absl::StrCat("localhost:", ext_proc_port);
  MockRequestHeadersExternalProcessorService service;
  service.SetBehavior(
      MockRequestHeadersExternalProcessorService::Behavior::kMutateHeaders);
  ServerBuilder builder;
  builder.AddListeningPort(server_address, InsecureServerCredentials());
  builder.RegisterService(&service);
  std::unique_ptr<Server> server = builder.BuildAndStart();
  ASSERT_NE(server, nullptr);
  auto ext_proc =
      ExternalProcessorBuilder()
          .SetTargetUri(absl::StrCat("dns:localhost:", ext_proc_port))
          .SetInsecureChannelCredentials()
          .SetRequestHeaderMode(envoy::extensions::filters::http::ext_proc::v3::
                                    ProcessingMode::SEND)
          .Build();
  CreateAndStartBackends(1, /*xds_enabled=*/false);
  SetListenerAndRouteConfiguration(balancer_.get(),
                                   BuildListenerWithExtProcFilter(ext_proc),
                                   default_route_config_);
  Cluster ext_proc_cluster = default_cluster_;
  ext_proc_cluster.set_name(std::string(kExtProcClusterName));
  balancer_->ads_service()->SetCdsResource(ext_proc_cluster);
  EdsResourceArgs args({{"locality0", CreateEndpointsForBackends()}});
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
  std::multimap<std::string, std::string> server_initial_metadata;
  EchoResponse response;
  Status status = SendRpc(RpcOptions().set_echo_metadata_initially(true),
                          &response, &server_initial_metadata);
  EXPECT_TRUE(status.ok()) << status.error_message() << " ("
                           << status.error_details() << ")";
  auto it = server_initial_metadata.find("x-ext-proc-test");
  ASSERT_NE(it, server_initial_metadata.end());
  EXPECT_EQ(it->second, "e2e-modified");
  server->Shutdown();
}

TEST_P(XdsExtProcEnd2endTest,
       RequestAttributesSentToExtProcWhenRequestHeaderProcessingModeIsEnabled) {
  int ext_proc_port = grpc_pick_unused_port_or_die();
  std::string server_address = absl::StrCat("localhost:", ext_proc_port);
  MockRequestHeadersExternalProcessorService service;
  service.SetBehavior(
      MockRequestHeadersExternalProcessorService::Behavior::kContinue);
  ServerBuilder builder;
  builder.AddListeningPort(server_address, InsecureServerCredentials());
  builder.RegisterService(&service);
  std::unique_ptr<Server> server = builder.BuildAndStart();
  ASSERT_NE(server, nullptr);
  auto ext_proc =
      ExternalProcessorBuilder()
          .SetTargetUri(absl::StrCat("dns:localhost:", ext_proc_port))
          .SetInsecureChannelCredentials()
          .SetRequestHeaderMode(envoy::extensions::filters::http::ext_proc::v3::
                                    ProcessingMode::SEND)
          .AddRequestAttribute("request.path")
          .AddRequestAttribute("request.host")
          .Build();
  CreateAndStartBackends(1, /*xds_enabled=*/false);
  SetListenerAndRouteConfiguration(balancer_.get(),
                                   BuildListenerWithExtProcFilter(ext_proc),
                                   default_route_config_);
  Cluster ext_proc_cluster = default_cluster_;
  ext_proc_cluster.set_name(std::string(kExtProcClusterName));
  balancer_->ads_service()->SetCdsResource(ext_proc_cluster);
  EdsResourceArgs args({{"locality0", CreateEndpointsForBackends()}});
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
  std::multimap<std::string, std::string> server_initial_metadata;
  EchoResponse response;
  Status status = SendRpc(RpcOptions(), &response, &server_initial_metadata);
  EXPECT_TRUE(status.ok()) << status.error_message();
  auto attrs = service.last_received_attributes();
  EXPECT_EQ(attrs.size(), 2);
  auto it = attrs.find("request.path");
  ASSERT_NE(it, attrs.end());
  EXPECT_EQ(it->second, "/grpc.testing.EchoTestService/Echo");
  it = attrs.find("request.host");
  ASSERT_NE(it, attrs.end());
  EXPECT_EQ(it->second, "server.example.com");
  server->Shutdown();
}

TEST_P(XdsExtProcEnd2endTest,
       ClientInitialMetadataRemovedWhenRequestHeaderProcessingModeIsEnabled) {
  int ext_proc_port = grpc_pick_unused_port_or_die();
  std::string server_address = absl::StrCat("localhost:", ext_proc_port);
  MockRequestHeadersExternalProcessorService service;
  service.SetBehavior(
      MockRequestHeadersExternalProcessorService::Behavior::kRemoveHeaders);
  ServerBuilder builder;
  builder.AddListeningPort(server_address, InsecureServerCredentials());
  builder.RegisterService(&service);
  std::unique_ptr<Server> server = builder.BuildAndStart();
  ASSERT_NE(server, nullptr);
  auto ext_proc =
      ExternalProcessorBuilder()
          .SetTargetUri(absl::StrCat("dns:localhost:", ext_proc_port))
          .SetInsecureChannelCredentials()
          .SetRequestHeaderMode(envoy::extensions::filters::http::ext_proc::v3::
                                    ProcessingMode::SEND)
          .Build();
  CreateAndStartBackends(1, /*xds_enabled=*/false);
  SetListenerAndRouteConfiguration(balancer_.get(),
                                   BuildListenerWithExtProcFilter(ext_proc),
                                   default_route_config_);
  Cluster ext_proc_cluster = default_cluster_;
  ext_proc_cluster.set_name(std::string(kExtProcClusterName));
  balancer_->ads_service()->SetCdsResource(ext_proc_cluster);
  EdsResourceArgs args({{"locality0", CreateEndpointsForBackends()}});
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
  std::multimap<std::string, std::string> server_initial_metadata;
  EchoResponse response;
  Status status =
      SendRpc(RpcOptions().set_echo_metadata_initially(true).set_metadata(
                  {{"x-to-be-removed", "present"}}),
              &response, &server_initial_metadata);
  EXPECT_TRUE(status.ok()) << status.error_message();
  auto it = server_initial_metadata.find("x-to-be-removed");
  EXPECT_EQ(it, server_initial_metadata.end());
  server->Shutdown();
}

TEST_P(XdsExtProcEnd2endTest,
       ClientInitialMetadataRequestWhenObservabilityModeIsEnabled) {
  int ext_proc_port = grpc_pick_unused_port_or_die();
  std::string server_address = absl::StrCat("localhost:", ext_proc_port);
  MockRequestHeadersExternalProcessorService service;
  service.SetBehavior(
      MockRequestHeadersExternalProcessorService::Behavior::kObservabilityMode);
  ServerBuilder builder;
  builder.AddListeningPort(server_address, InsecureServerCredentials());
  builder.RegisterService(&service);
  std::unique_ptr<Server> server = builder.BuildAndStart();
  ASSERT_NE(server, nullptr);
  auto ext_proc =
      ExternalProcessorBuilder()
          .SetTargetUri(absl::StrCat("dns:localhost:", ext_proc_port))
          .SetInsecureChannelCredentials()
          .SetRequestHeaderMode(envoy::extensions::filters::http::ext_proc::v3::
                                    ProcessingMode::SEND)
          .SetObservabilityMode(true)
          .Build();
  CreateAndStartBackends(1, /*xds_enabled=*/false);
  SetListenerAndRouteConfiguration(balancer_.get(),
                                   BuildListenerWithExtProcFilter(ext_proc),
                                   default_route_config_);
  Cluster ext_proc_cluster = default_cluster_;
  ext_proc_cluster.set_name(std::string(kExtProcClusterName));
  balancer_->ads_service()->SetCdsResource(ext_proc_cluster);
  EdsResourceArgs args({{"locality0", CreateEndpointsForBackends()}});
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
  std::multimap<std::string, std::string> server_initial_metadata;
  EchoResponse response;
  Status status = SendRpc(RpcOptions(), &response, &server_initial_metadata);
  EXPECT_TRUE(status.ok()) << status.error_message();
  server->Shutdown();
}

TEST_P(
    XdsExtProcEnd2endTest,
    ClientInitialMetadataMutationRejectedByRulesIsSilentlyIgnoredWhenDisallowIsErrorIsFalse) {
  int ext_proc_port = grpc_pick_unused_port_or_die();
  std::string server_address = absl::StrCat("localhost:", ext_proc_port);
  MockRequestHeadersExternalProcessorService service;
  service.SetBehavior(
      MockRequestHeadersExternalProcessorService::Behavior::kMutateHeaders);
  ServerBuilder builder;
  builder.AddListeningPort(server_address, InsecureServerCredentials());
  builder.RegisterService(&service);
  std::unique_ptr<Server> server = builder.BuildAndStart();
  ASSERT_NE(server, nullptr);
  envoy::config::common::mutation_rules::v3::HeaderMutationRules rules;
  rules.mutable_disallow_all()->set_value(true);
  rules.mutable_disallow_is_error()->set_value(false);
  auto ext_proc =
      ExternalProcessorBuilder()
          .SetTargetUri(absl::StrCat("dns:localhost:", ext_proc_port))
          .SetInsecureChannelCredentials()
          .SetRequestHeaderMode(envoy::extensions::filters::http::ext_proc::v3::
                                    ProcessingMode::SEND)
          .SetMutationRules(rules)
          .Build();
  CreateAndStartBackends(1, /*xds_enabled=*/false);
  SetListenerAndRouteConfiguration(balancer_.get(),
                                   BuildListenerWithExtProcFilter(ext_proc),
                                   default_route_config_);
  Cluster ext_proc_cluster = default_cluster_;
  ext_proc_cluster.set_name(std::string(kExtProcClusterName));
  balancer_->ads_service()->SetCdsResource(ext_proc_cluster);
  EdsResourceArgs args({{"locality0", CreateEndpointsForBackends()}});
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
  std::multimap<std::string, std::string> server_initial_metadata;
  EchoResponse response;
  Status status = SendRpc(RpcOptions(), &response, &server_initial_metadata);
  EXPECT_TRUE(status.ok()) << status.error_message();
  auto it = server_initial_metadata.find("x-ext-proc-test");
  EXPECT_EQ(it, server_initial_metadata.end());
  server->Shutdown();
}

TEST_P(
    XdsExtProcEnd2endTest,
    ClientInitialMetadataMutationRejectedByRulesFailsCallWhenDisallowIsErrorIsTrue) {
  int ext_proc_port = grpc_pick_unused_port_or_die();
  std::string server_address = absl::StrCat("localhost:", ext_proc_port);
  MockRequestHeadersExternalProcessorService service;
  service.SetBehavior(
      MockRequestHeadersExternalProcessorService::Behavior::kMutateHeaders);
  ServerBuilder builder;
  builder.AddListeningPort(server_address, InsecureServerCredentials());
  builder.RegisterService(&service);
  std::unique_ptr<Server> server = builder.BuildAndStart();
  ASSERT_NE(server, nullptr);
  envoy::config::common::mutation_rules::v3::HeaderMutationRules rules;
  rules.mutable_disallow_all()->set_value(true);
  rules.mutable_disallow_is_error()->set_value(true);
  auto ext_proc =
      ExternalProcessorBuilder()
          .SetTargetUri(absl::StrCat("dns:localhost:", ext_proc_port))
          .SetInsecureChannelCredentials()
          .SetRequestHeaderMode(envoy::extensions::filters::http::ext_proc::v3::
                                    ProcessingMode::SEND)
          .SetMutationRules(rules)
          .Build();
  CreateAndStartBackends(1, /*xds_enabled=*/false);
  SetListenerAndRouteConfiguration(balancer_.get(),
                                   BuildListenerWithExtProcFilter(ext_proc),
                                   default_route_config_);
  Cluster ext_proc_cluster = default_cluster_;
  ext_proc_cluster.set_name(std::string(kExtProcClusterName));
  balancer_->ads_service()->SetCdsResource(ext_proc_cluster);
  EdsResourceArgs args({{"locality0", CreateEndpointsForBackends()}});
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
  std::multimap<std::string, std::string> server_initial_metadata;
  EchoResponse response;
  Status status = SendRpc(RpcOptions(), &response, &server_initial_metadata);
  EXPECT_FALSE(status.ok());
  EXPECT_EQ(status.error_code(), ::grpc::StatusCode::INTERNAL);
  EXPECT_EQ(status.error_message(),
            "Forbidden header mutation: x-ext-proc-test");
  server->Shutdown();
}

TEST_P(
    XdsExtProcEnd2endTest,
    ClientInitialMetadataMutationOfReservedHeaderFailsCallWithInternalError) {
  int ext_proc_port = grpc_pick_unused_port_or_die();
  std::string server_address = absl::StrCat("localhost:", ext_proc_port);
  MockRequestHeadersExternalProcessorService service;
  service.SetBehavior(MockRequestHeadersExternalProcessorService::Behavior::
                          kMutateReservedHeader);
  ServerBuilder builder;
  builder.AddListeningPort(server_address, InsecureServerCredentials());
  builder.RegisterService(&service);
  std::unique_ptr<Server> server = builder.BuildAndStart();
  ASSERT_NE(server, nullptr);
  auto ext_proc =
      ExternalProcessorBuilder()
          .SetTargetUri(absl::StrCat("dns:localhost:", ext_proc_port))
          .SetInsecureChannelCredentials()
          .SetRequestHeaderMode(envoy::extensions::filters::http::ext_proc::v3::
                                    ProcessingMode::SEND)
          .Build();
  CreateAndStartBackends(1, /*xds_enabled=*/false);
  SetListenerAndRouteConfiguration(balancer_.get(),
                                   BuildListenerWithExtProcFilter(ext_proc),
                                   default_route_config_);
  Cluster ext_proc_cluster = default_cluster_;
  ext_proc_cluster.set_name(std::string(kExtProcClusterName));
  balancer_->ads_service()->SetCdsResource(ext_proc_cluster);
  EdsResourceArgs args({{"locality0", CreateEndpointsForBackends()}});
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
  std::multimap<std::string, std::string> server_initial_metadata;
  EchoResponse response;
  Status status = SendRpc(RpcOptions(), &response, &server_initial_metadata);
  EXPECT_FALSE(status.ok());
  EXPECT_EQ(status.error_code(), ::grpc::StatusCode::INTERNAL);
  EXPECT_EQ(status.error_message(),
            "validation failed: [field:header.key error:header \":path\" not "
            "allowed]");
  server->Shutdown();
}

TEST_P(XdsExtProcEnd2endTest,
       ServerInitialMetadataMutatedWhenResponseHeaderProcessingModeIsEnabled) {
  int ext_proc_port = grpc_pick_unused_port_or_die();
  std::string server_address = absl::StrCat("localhost:", ext_proc_port);
  MockRequestHeadersExternalProcessorService service;
  service.SetBehavior(
      MockRequestHeadersExternalProcessorService::Behavior::kMutateHeaders);
  ServerBuilder builder;
  builder.AddListeningPort(server_address, InsecureServerCredentials());
  builder.RegisterService(&service);
  std::unique_ptr<Server> server = builder.BuildAndStart();
  ASSERT_NE(server, nullptr);
  auto ext_proc =
      ExternalProcessorBuilder()
          .SetTargetUri(absl::StrCat("dns:localhost:", ext_proc_port))
          .SetInsecureChannelCredentials()
          .SetResponseHeaderMode(envoy::extensions::filters::http::ext_proc::
                                     v3::ProcessingMode::SEND)
          .Build();
  CreateAndStartBackends(1, /*xds_enabled=*/false);
  SetListenerAndRouteConfiguration(balancer_.get(),
                                   BuildListenerWithExtProcFilter(ext_proc),
                                   default_route_config_);
  Cluster ext_proc_cluster = default_cluster_;
  ext_proc_cluster.set_name(std::string(kExtProcClusterName));
  balancer_->ads_service()->SetCdsResource(ext_proc_cluster);
  EdsResourceArgs args({{"locality0", CreateEndpointsForBackends()}});
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
  std::multimap<std::string, std::string> server_initial_metadata;
  EchoResponse response;
  Status status = SendRpc(RpcOptions(), &response, &server_initial_metadata);
  EXPECT_TRUE(status.ok()) << status.error_message();
  auto it = server_initial_metadata.find("x-ext-proc-test-response");
  ASSERT_NE(it, server_initial_metadata.end());
  EXPECT_EQ(it->second, "e2e-modified-response");
  server->Shutdown();
}

TEST_P(XdsExtProcEnd2endTest,
       ServerInitialMetadataRemovedWhenResponseHeaderProcessingModeIsEnabled) {
  int ext_proc_port = grpc_pick_unused_port_or_die();
  std::string server_address = absl::StrCat("localhost:", ext_proc_port);
  MockRequestHeadersExternalProcessorService service;
  service.SetBehavior(
      MockRequestHeadersExternalProcessorService::Behavior::kRemoveHeaders);
  ServerBuilder builder;
  builder.AddListeningPort(server_address, InsecureServerCredentials());
  builder.RegisterService(&service);
  std::unique_ptr<Server> server = builder.BuildAndStart();
  ASSERT_NE(server, nullptr);
  auto ext_proc =
      ExternalProcessorBuilder()
          .SetTargetUri(absl::StrCat("dns:localhost:", ext_proc_port))
          .SetInsecureChannelCredentials()
          .SetResponseHeaderMode(envoy::extensions::filters::http::ext_proc::
                                     v3::ProcessingMode::SEND)
          .Build();
  CreateAndStartBackends(1, /*xds_enabled=*/false);
  SetListenerAndRouteConfiguration(balancer_.get(),
                                   BuildListenerWithExtProcFilter(ext_proc),
                                   default_route_config_);
  Cluster ext_proc_cluster = default_cluster_;
  ext_proc_cluster.set_name(std::string(kExtProcClusterName));
  balancer_->ads_service()->SetCdsResource(ext_proc_cluster);
  EdsResourceArgs args({{"locality0", CreateEndpointsForBackends()}});
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
  std::multimap<std::string, std::string> server_initial_metadata;
  EchoResponse response;
  Status status =
      SendRpc(RpcOptions().set_echo_metadata_initially(true).set_metadata(
                  {{"x-to-be-removed-response", "present"}}),
              &response, &server_initial_metadata);
  EXPECT_TRUE(status.ok()) << status.error_message();
  auto it = server_initial_metadata.find("x-to-be-removed-response");
  EXPECT_EQ(it, server_initial_metadata.end());
  server->Shutdown();
}

TEST_P(XdsExtProcEnd2endTest,
       ServerInitialMetadataRequestWhenObservabilityModeIsEnabled) {
  int ext_proc_port = grpc_pick_unused_port_or_die();
  std::string server_address = absl::StrCat("localhost:", ext_proc_port);
  MockRequestHeadersExternalProcessorService service;
  service.SetBehavior(
      MockRequestHeadersExternalProcessorService::Behavior::kObservabilityMode);
  ServerBuilder builder;
  builder.AddListeningPort(server_address, InsecureServerCredentials());
  builder.RegisterService(&service);
  std::unique_ptr<Server> server = builder.BuildAndStart();
  ASSERT_NE(server, nullptr);
  auto ext_proc =
      ExternalProcessorBuilder()
          .SetTargetUri(absl::StrCat("dns:localhost:", ext_proc_port))
          .SetInsecureChannelCredentials()
          .SetResponseHeaderMode(envoy::extensions::filters::http::ext_proc::
                                     v3::ProcessingMode::SEND)
          .SetObservabilityMode(true)
          .Build();
  CreateAndStartBackends(1, /*xds_enabled=*/false);
  SetListenerAndRouteConfiguration(balancer_.get(),
                                   BuildListenerWithExtProcFilter(ext_proc),
                                   default_route_config_);
  Cluster ext_proc_cluster = default_cluster_;
  ext_proc_cluster.set_name(std::string(kExtProcClusterName));
  balancer_->ads_service()->SetCdsResource(ext_proc_cluster);
  EdsResourceArgs args({{"locality0", CreateEndpointsForBackends()}});
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
  std::multimap<std::string, std::string> server_initial_metadata;
  EchoResponse response;
  Status status = SendRpc(RpcOptions(), &response, &server_initial_metadata);
  EXPECT_TRUE(status.ok()) << status.error_message();
  server->Shutdown();
}

TEST_P(
    XdsExtProcEnd2endTest,
    ServerInitialMetadataMutationRejectedByRulesIsSilentlyIgnoredWhenDisallowIsErrorIsFalse) {
  int ext_proc_port = grpc_pick_unused_port_or_die();
  std::string server_address = absl::StrCat("localhost:", ext_proc_port);
  MockRequestHeadersExternalProcessorService service;
  service.SetBehavior(
      MockRequestHeadersExternalProcessorService::Behavior::kMutateHeaders);
  ServerBuilder builder;
  builder.AddListeningPort(server_address, InsecureServerCredentials());
  builder.RegisterService(&service);
  std::unique_ptr<Server> server = builder.BuildAndStart();
  ASSERT_NE(server, nullptr);
  envoy::config::common::mutation_rules::v3::HeaderMutationRules rules;
  rules.mutable_disallow_all()->set_value(true);
  rules.mutable_disallow_is_error()->set_value(false);
  auto ext_proc =
      ExternalProcessorBuilder()
          .SetTargetUri(absl::StrCat("dns:localhost:", ext_proc_port))
          .SetInsecureChannelCredentials()
          .SetRequestHeaderMode(envoy::extensions::filters::http::ext_proc::v3::
                                    ProcessingMode::SKIP)
          .SetResponseHeaderMode(envoy::extensions::filters::http::ext_proc::
                                     v3::ProcessingMode::SEND)
          .SetMutationRules(rules)
          .Build();
  CreateAndStartBackends(1, /*xds_enabled=*/false);
  SetListenerAndRouteConfiguration(balancer_.get(),
                                   BuildListenerWithExtProcFilter(ext_proc),
                                   default_route_config_);
  Cluster ext_proc_cluster = default_cluster_;
  ext_proc_cluster.set_name(std::string(kExtProcClusterName));
  balancer_->ads_service()->SetCdsResource(ext_proc_cluster);
  EdsResourceArgs args({{"locality0", CreateEndpointsForBackends()}});
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
  std::multimap<std::string, std::string> server_initial_metadata;
  EchoResponse response;
  Status status = SendRpc(RpcOptions(), &response, &server_initial_metadata);
  EXPECT_TRUE(status.ok()) << status.error_message();
  auto it = server_initial_metadata.find("x-ext-proc-test-response");
  EXPECT_EQ(it, server_initial_metadata.end());
  server->Shutdown();
}

TEST_P(
    XdsExtProcEnd2endTest,
    ServerInitialMetadataMutationRejectedByRulesFailsCallWhenDisallowIsErrorIsTrue) {
  int ext_proc_port = grpc_pick_unused_port_or_die();
  std::string server_address = absl::StrCat("localhost:", ext_proc_port);
  MockRequestHeadersExternalProcessorService service;
  service.SetBehavior(
      MockRequestHeadersExternalProcessorService::Behavior::kMutateHeaders);
  ServerBuilder builder;
  builder.AddListeningPort(server_address, InsecureServerCredentials());
  builder.RegisterService(&service);
  std::unique_ptr<Server> server = builder.BuildAndStart();
  ASSERT_NE(server, nullptr);
  envoy::config::common::mutation_rules::v3::HeaderMutationRules rules;
  rules.mutable_disallow_all()->set_value(true);
  rules.mutable_disallow_is_error()->set_value(true);
  auto ext_proc =
      ExternalProcessorBuilder()
          .SetTargetUri(absl::StrCat("dns:localhost:", ext_proc_port))
          .SetInsecureChannelCredentials()
          .SetRequestHeaderMode(envoy::extensions::filters::http::ext_proc::v3::
                                    ProcessingMode::SKIP)
          .SetResponseHeaderMode(envoy::extensions::filters::http::ext_proc::
                                     v3::ProcessingMode::SEND)
          .SetMutationRules(rules)
          .Build();
  CreateAndStartBackends(1, /*xds_enabled=*/false);
  SetListenerAndRouteConfiguration(balancer_.get(),
                                   BuildListenerWithExtProcFilter(ext_proc),
                                   default_route_config_);
  Cluster ext_proc_cluster = default_cluster_;
  ext_proc_cluster.set_name(std::string(kExtProcClusterName));
  balancer_->ads_service()->SetCdsResource(ext_proc_cluster);
  EdsResourceArgs args({{"locality0", CreateEndpointsForBackends()}});
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
  std::multimap<std::string, std::string> server_initial_metadata;
  EchoResponse response;
  Status status = SendRpc(RpcOptions(), &response, &server_initial_metadata);
  EXPECT_FALSE(status.ok());
  EXPECT_EQ(status.error_code(), ::grpc::StatusCode::INTERNAL);
  EXPECT_EQ(status.error_message(),
            "Forbidden header mutation: x-ext-proc-test-response");
  server->Shutdown();
}

TEST_P(
    XdsExtProcEnd2endTest,
    ServerInitialMetadataMutationOfReservedHeaderFailsCallWithInternalError) {
  int ext_proc_port = grpc_pick_unused_port_or_die();
  std::string server_address = absl::StrCat("localhost:", ext_proc_port);
  MockRequestHeadersExternalProcessorService service;
  service.SetBehavior(MockRequestHeadersExternalProcessorService::Behavior::
                          kMutateReservedHeader);
  ServerBuilder builder;
  builder.AddListeningPort(server_address, InsecureServerCredentials());
  builder.RegisterService(&service);
  std::unique_ptr<Server> server = builder.BuildAndStart();
  ASSERT_NE(server, nullptr);
  auto ext_proc =
      ExternalProcessorBuilder()
          .SetTargetUri(absl::StrCat("dns:localhost:", ext_proc_port))
          .SetInsecureChannelCredentials()
          .SetRequestHeaderMode(envoy::extensions::filters::http::ext_proc::v3::
                                    ProcessingMode::SKIP)
          .SetResponseHeaderMode(envoy::extensions::filters::http::ext_proc::
                                     v3::ProcessingMode::SEND)
          .Build();
  CreateAndStartBackends(1, /*xds_enabled=*/false);
  SetListenerAndRouteConfiguration(balancer_.get(),
                                   BuildListenerWithExtProcFilter(ext_proc),
                                   default_route_config_);
  Cluster ext_proc_cluster = default_cluster_;
  ext_proc_cluster.set_name(std::string(kExtProcClusterName));
  balancer_->ads_service()->SetCdsResource(ext_proc_cluster);
  EdsResourceArgs args({{"locality0", CreateEndpointsForBackends()}});
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
  std::multimap<std::string, std::string> server_initial_metadata;
  EchoResponse response;
  Status status = SendRpc(RpcOptions(), &response, &server_initial_metadata);
  EXPECT_FALSE(status.ok());
  EXPECT_EQ(status.error_code(), ::grpc::StatusCode::INTERNAL);
  EXPECT_EQ(status.error_message(),
            "validation failed: [field:header.key error:header \":status\" not "
            "allowed]");
  server->Shutdown();
}

TEST_P(XdsExtProcEnd2endTest,
       ProtocolConfigSentOnlyInFirstMessageWhenBothHeaderModesAreEnabled) {
  int ext_proc_port = grpc_pick_unused_port_or_die();
  std::string server_address = absl::StrCat("localhost:", ext_proc_port);
  MockRequestHeadersExternalProcessorService service;
  service.SetBehavior(
      MockRequestHeadersExternalProcessorService::Behavior::kContinue);
  ServerBuilder builder;
  builder.AddListeningPort(server_address, InsecureServerCredentials());
  builder.RegisterService(&service);
  std::unique_ptr<Server> server = builder.BuildAndStart();
  ASSERT_NE(server, nullptr);
  auto ext_proc =
      ExternalProcessorBuilder()
          .SetTargetUri(absl::StrCat("dns:localhost:", ext_proc_port))
          .SetInsecureChannelCredentials()
          .SetRequestHeaderMode(envoy::extensions::filters::http::ext_proc::v3::
                                    ProcessingMode::SEND)
          .SetResponseHeaderMode(envoy::extensions::filters::http::ext_proc::
                                     v3::ProcessingMode::SEND)
          .Build();
  CreateAndStartBackends(1, /*xds_enabled=*/false);
  SetListenerAndRouteConfiguration(balancer_.get(),
                                   BuildListenerWithExtProcFilter(ext_proc),
                                   default_route_config_);
  Cluster ext_proc_cluster = default_cluster_;
  ext_proc_cluster.set_name(std::string(kExtProcClusterName));
  balancer_->ads_service()->SetCdsResource(ext_proc_cluster);
  EdsResourceArgs args({{"locality0", CreateEndpointsForBackends()}});
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
  std::multimap<std::string, std::string> server_initial_metadata;
  EchoResponse response;
  Status status = SendRpc(RpcOptions(), &response, &server_initial_metadata);
  EXPECT_TRUE(status.ok()) << status.error_message();
  EXPECT_TRUE(service.has_protocol_config_in_request_headers());
  EXPECT_FALSE(service.has_protocol_config_in_response_headers());
  server->Shutdown();
}

TEST_P(
    XdsExtProcEnd2endTest,
    ServerTrailingMetadataMutatedWhenResponseTrailerProcessingModeIsEnabled) {
  int ext_proc_port = grpc_pick_unused_port_or_die();
  std::string server_address = absl::StrCat("localhost:", ext_proc_port);
  MockRequestHeadersExternalProcessorService service;
  service.SetBehavior(
      MockRequestHeadersExternalProcessorService::Behavior::kMutateTrailers);
  ServerBuilder builder;
  builder.AddListeningPort(server_address, InsecureServerCredentials());
  builder.RegisterService(&service);
  std::unique_ptr<Server> server = builder.BuildAndStart();
  ASSERT_NE(server, nullptr);
  auto ext_proc =
      ExternalProcessorBuilder()
          .SetTargetUri(absl::StrCat("dns:localhost:", ext_proc_port))
          .SetInsecureChannelCredentials()
          .SetRequestHeaderMode(envoy::extensions::filters::http::ext_proc::v3::
                                    ProcessingMode::SKIP)
          .SetResponseHeaderMode(envoy::extensions::filters::http::ext_proc::
                                     v3::ProcessingMode::SKIP)
          .SetResponseTrailerMode(envoy::extensions::filters::http::ext_proc::
                                      v3::ProcessingMode::SEND)
          .Build();
  CreateAndStartBackends(1, /*xds_enabled=*/false);
  SetListenerAndRouteConfiguration(balancer_.get(),
                                   BuildListenerWithExtProcFilter(ext_proc),
                                   default_route_config_);
  Cluster ext_proc_cluster = default_cluster_;
  ext_proc_cluster.set_name(std::string(kExtProcClusterName));
  balancer_->ads_service()->SetCdsResource(ext_proc_cluster);
  EdsResourceArgs args({{"locality0", CreateEndpointsForBackends()}});
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
  std::multimap<std::string, std::string> server_initial_metadata;
  std::multimap<std::string, std::string> server_trailing_metadata;
  EchoResponse response;
  Status status = SendRpcGetTrailers(
      RpcOptions().set_echo_metadata(true).set_metadata(
          {{"x-ext-proc-test-trailer", "original-value"}}),
      &response, &server_initial_metadata, &server_trailing_metadata);
  EXPECT_TRUE(status.ok()) << status.error_message();
  auto it = server_trailing_metadata.find("x-ext-proc-test-trailer");
  ASSERT_NE(it, server_trailing_metadata.end());
  EXPECT_EQ(it->second, "e2e-modified-trailer");
  server->Shutdown();
}

TEST_P(
    XdsExtProcEnd2endTest,
    ServerTrailingMetadataMutationRejectedByRulesFailsCallWhenDisallowIsErrorIsTrue) {
  int ext_proc_port = grpc_pick_unused_port_or_die();
  std::string server_address = absl::StrCat("localhost:", ext_proc_port);
  MockRequestHeadersExternalProcessorService service;
  service.SetBehavior(
      MockRequestHeadersExternalProcessorService::Behavior::kMutateTrailers);
  ServerBuilder builder;
  builder.AddListeningPort(server_address, InsecureServerCredentials());
  builder.RegisterService(&service);
  std::unique_ptr<Server> server = builder.BuildAndStart();
  ASSERT_NE(server, nullptr);
  auto ext_proc =
      ExternalProcessorBuilder()
          .SetTargetUri(absl::StrCat("dns:localhost:", ext_proc_port))
          .SetInsecureChannelCredentials()
          .SetRequestHeaderMode(envoy::extensions::filters::http::ext_proc::v3::
                                    ProcessingMode::SKIP)
          .SetResponseHeaderMode(envoy::extensions::filters::http::ext_proc::
                                     v3::ProcessingMode::SKIP)
          .SetResponseTrailerMode(envoy::extensions::filters::http::ext_proc::
                                      v3::ProcessingMode::SEND)
          .Build();
  auto* rules = ext_proc.mutable_mutation_rules();
  rules->mutable_allow_all_routing()->set_value(false);
  rules->mutable_disallow_all()->set_value(true);
  rules->mutable_disallow_is_error()->set_value(true);
  CreateAndStartBackends(1, /*xds_enabled=*/false);
  SetListenerAndRouteConfiguration(balancer_.get(),
                                   BuildListenerWithExtProcFilter(ext_proc),
                                   default_route_config_);
  Cluster ext_proc_cluster = default_cluster_;
  ext_proc_cluster.set_name(std::string(kExtProcClusterName));
  balancer_->ads_service()->SetCdsResource(ext_proc_cluster);
  EdsResourceArgs args({{"locality0", CreateEndpointsForBackends()}});
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
  std::multimap<std::string, std::string> server_initial_metadata;
  std::multimap<std::string, std::string> server_trailing_metadata;
  EchoResponse response;
  Status status = SendRpcGetTrailers(
      RpcOptions().set_echo_metadata(true).set_metadata(
          {{"x-ext-proc-test-trailer", "original-value"}}),
      &response, &server_initial_metadata, &server_trailing_metadata);
  EXPECT_FALSE(status.ok());
  EXPECT_EQ(status.error_code(), StatusCode::INTERNAL);
  EXPECT_EQ(status.error_message(),
            "Forbidden header mutation: x-ext-proc-test-trailer");
  server->Shutdown();
}

TEST_P(
    XdsExtProcEnd2endTest,
    ServerTrailingMetadataMutationOfReservedHeaderFailsCallWithInternalError) {
  int ext_proc_port = grpc_pick_unused_port_or_die();
  std::string server_address = absl::StrCat("localhost:", ext_proc_port);
  MockRequestHeadersExternalProcessorService service;
  service.SetBehavior(MockRequestHeadersExternalProcessorService::Behavior::
                          kMutateReservedHeader);
  ServerBuilder builder;
  builder.AddListeningPort(server_address, InsecureServerCredentials());
  builder.RegisterService(&service);
  std::unique_ptr<Server> server = builder.BuildAndStart();
  ASSERT_NE(server, nullptr);
  auto ext_proc =
      ExternalProcessorBuilder()
          .SetTargetUri(absl::StrCat("dns:localhost:", ext_proc_port))
          .SetInsecureChannelCredentials()
          .SetRequestHeaderMode(envoy::extensions::filters::http::ext_proc::v3::
                                    ProcessingMode::SKIP)
          .SetResponseHeaderMode(envoy::extensions::filters::http::ext_proc::
                                     v3::ProcessingMode::SKIP)
          .SetResponseTrailerMode(envoy::extensions::filters::http::ext_proc::
                                      v3::ProcessingMode::SEND)
          .Build();
  CreateAndStartBackends(1, /*xds_enabled=*/false);
  SetListenerAndRouteConfiguration(balancer_.get(),
                                   BuildListenerWithExtProcFilter(ext_proc),
                                   default_route_config_);
  Cluster ext_proc_cluster = default_cluster_;
  ext_proc_cluster.set_name(std::string(kExtProcClusterName));
  balancer_->ads_service()->SetCdsResource(ext_proc_cluster);
  EdsResourceArgs args({{"locality0", CreateEndpointsForBackends()}});
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
  std::multimap<std::string, std::string> server_initial_metadata;
  std::multimap<std::string, std::string> server_trailing_metadata;
  EchoResponse response;
  Status status = SendRpcGetTrailers(
      RpcOptions().set_echo_metadata(true).set_metadata(
          {{"x-ext-proc-test-trailer", "original-value"}}),
      &response, &server_initial_metadata, &server_trailing_metadata);
  EXPECT_FALSE(status.ok());
  EXPECT_EQ(status.error_code(), StatusCode::INTERNAL);
  EXPECT_EQ(status.error_message(),
            "validation failed: [field:header.key error:header \"grpc-status\" "
            "not allowed]");
  server->Shutdown();
}

TEST_P(
    XdsExtProcEnd2endTest,
    ServerTrailingMetadataRemovedWhenResponseTrailerProcessingModeIsEnabled) {
  int ext_proc_port = grpc_pick_unused_port_or_die();
  std::string server_address = absl::StrCat("localhost:", ext_proc_port);
  MockRequestHeadersExternalProcessorService service;
  service.SetBehavior(
      MockRequestHeadersExternalProcessorService::Behavior::kRemoveHeaders);
  ServerBuilder builder;
  builder.AddListeningPort(server_address, InsecureServerCredentials());
  builder.RegisterService(&service);
  std::unique_ptr<Server> server = builder.BuildAndStart();
  ASSERT_NE(server, nullptr);
  auto ext_proc =
      ExternalProcessorBuilder()
          .SetTargetUri(absl::StrCat("dns:localhost:", ext_proc_port))
          .SetInsecureChannelCredentials()
          .SetRequestHeaderMode(envoy::extensions::filters::http::ext_proc::v3::
                                    ProcessingMode::SKIP)
          .SetResponseHeaderMode(envoy::extensions::filters::http::ext_proc::
                                     v3::ProcessingMode::SKIP)
          .SetResponseTrailerMode(envoy::extensions::filters::http::ext_proc::
                                      v3::ProcessingMode::SEND)
          .Build();
  CreateAndStartBackends(1, /*xds_enabled=*/false);
  SetListenerAndRouteConfiguration(balancer_.get(),
                                   BuildListenerWithExtProcFilter(ext_proc),
                                   default_route_config_);
  Cluster ext_proc_cluster = default_cluster_;
  ext_proc_cluster.set_name(std::string(kExtProcClusterName));
  balancer_->ads_service()->SetCdsResource(ext_proc_cluster);
  EdsResourceArgs args({{"locality0", CreateEndpointsForBackends()}});
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
  std::multimap<std::string, std::string> server_initial_metadata;
  std::multimap<std::string, std::string> server_trailing_metadata;
  EchoResponse response;
  Status status = SendRpcGetTrailers(
      RpcOptions().set_echo_metadata(true).set_metadata(
          {{"x-to-be-removed-trailer", "present"}}),
      &response, &server_initial_metadata, &server_trailing_metadata);
  EXPECT_TRUE(status.ok()) << status.error_message();
  auto it = server_trailing_metadata.find("x-to-be-removed-trailer");
  EXPECT_EQ(it, server_trailing_metadata.end());
  server->Shutdown();
}

TEST_P(XdsExtProcEnd2endTest,
       ServerTrailingMetadataRequestWhenObservabilityModeIsEnabled) {
  int ext_proc_port = grpc_pick_unused_port_or_die();
  std::string server_address = absl::StrCat("localhost:", ext_proc_port);
  MockRequestHeadersExternalProcessorService service;
  service.SetBehavior(
      MockRequestHeadersExternalProcessorService::Behavior::kObservabilityMode);
  ServerBuilder builder;
  builder.AddListeningPort(server_address, InsecureServerCredentials());
  builder.RegisterService(&service);
  std::unique_ptr<Server> server = builder.BuildAndStart();
  ASSERT_NE(server, nullptr);
  auto ext_proc =
      ExternalProcessorBuilder()
          .SetTargetUri(absl::StrCat("dns:localhost:", ext_proc_port))
          .SetInsecureChannelCredentials()
          .SetRequestHeaderMode(envoy::extensions::filters::http::ext_proc::v3::
                                    ProcessingMode::SKIP)
          .SetResponseHeaderMode(envoy::extensions::filters::http::ext_proc::
                                     v3::ProcessingMode::SKIP)
          .SetResponseTrailerMode(envoy::extensions::filters::http::ext_proc::
                                      v3::ProcessingMode::SEND)
          .SetObservabilityMode(true)
          .Build();
  CreateAndStartBackends(1, /*xds_enabled=*/false);
  SetListenerAndRouteConfiguration(balancer_.get(),
                                   BuildListenerWithExtProcFilter(ext_proc),
                                   default_route_config_);
  Cluster ext_proc_cluster = default_cluster_;
  ext_proc_cluster.set_name(std::string(kExtProcClusterName));
  balancer_->ads_service()->SetCdsResource(ext_proc_cluster);
  EdsResourceArgs args({{"locality0", CreateEndpointsForBackends()}});
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
  std::multimap<std::string, std::string> server_initial_metadata;
  std::multimap<std::string, std::string> server_trailing_metadata;
  EchoResponse response;
  Status status = SendRpcGetTrailers(
      RpcOptions().set_echo_metadata(true).set_metadata(
          {{"x-ext-proc-test-trailer", "original-value"}}),
      &response, &server_initial_metadata, &server_trailing_metadata);
  EXPECT_TRUE(status.ok()) << status.error_message();
  server->Shutdown();
}

TEST_P(
    XdsExtProcEnd2endTest,
    ServerTrailingMetadataMutationRejectedByRulesIsSilentlyIgnoredWhenDisallowIsErrorIsFalse) {
  int ext_proc_port = grpc_pick_unused_port_or_die();
  std::string server_address = absl::StrCat("localhost:", ext_proc_port);
  MockRequestHeadersExternalProcessorService service;
  service.SetBehavior(
      MockRequestHeadersExternalProcessorService::Behavior::kMutateTrailers);
  ServerBuilder builder;
  builder.AddListeningPort(server_address, InsecureServerCredentials());
  builder.RegisterService(&service);
  std::unique_ptr<Server> server = builder.BuildAndStart();
  ASSERT_NE(server, nullptr);
  envoy::config::common::mutation_rules::v3::HeaderMutationRules rules;
  rules.mutable_disallow_all()->set_value(true);
  rules.mutable_disallow_is_error()->set_value(false);
  auto ext_proc =
      ExternalProcessorBuilder()
          .SetTargetUri(absl::StrCat("dns:localhost:", ext_proc_port))
          .SetInsecureChannelCredentials()
          .SetRequestHeaderMode(envoy::extensions::filters::http::ext_proc::v3::
                                    ProcessingMode::SKIP)
          .SetResponseHeaderMode(envoy::extensions::filters::http::ext_proc::
                                     v3::ProcessingMode::SKIP)
          .SetResponseTrailerMode(envoy::extensions::filters::http::ext_proc::
                                      v3::ProcessingMode::SEND)
          .SetMutationRules(rules)
          .Build();
  CreateAndStartBackends(1, /*xds_enabled=*/false);
  SetListenerAndRouteConfiguration(balancer_.get(),
                                   BuildListenerWithExtProcFilter(ext_proc),
                                   default_route_config_);
  Cluster ext_proc_cluster = default_cluster_;
  ext_proc_cluster.set_name(std::string(kExtProcClusterName));
  balancer_->ads_service()->SetCdsResource(ext_proc_cluster);
  EdsResourceArgs args({{"locality0", CreateEndpointsForBackends()}});
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
  std::multimap<std::string, std::string> server_initial_metadata;
  std::multimap<std::string, std::string> server_trailing_metadata;
  EchoResponse response;
  Status status = SendRpcGetTrailers(
      RpcOptions().set_echo_metadata(true).set_metadata(
          {{"x-ext-proc-test-trailer", "original-value"}}),
      &response, &server_initial_metadata, &server_trailing_metadata);
  EXPECT_TRUE(status.ok()) << status.error_message();
  auto it = server_trailing_metadata.find("x-ext-proc-test-trailer");
  ASSERT_NE(it, server_trailing_metadata.end());
  EXPECT_EQ(it->second, "original-value");
  server->Shutdown();
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
  grpc_core::ForceEnableExperiment("v2_non_owning_waker_implementation", true);
  grpc_init();
  const auto result = RUN_ALL_TESTS();
  grpc_shutdown();
  return result;
}
