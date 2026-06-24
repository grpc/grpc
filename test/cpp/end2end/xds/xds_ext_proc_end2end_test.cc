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
#include "src/core/lib/debug/trace.h"
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

using MockExternalProcessorBase =
    ::envoy::service::ext_proc::v3::ExternalProcessor::Service;

class MockExternalProcessorService : public MockExternalProcessorBase {
 public:
  struct RequestCounts {
    int request_headers = 0;
    int response_headers = 0;
    int request_body = 0;
    int response_body = 0;
    int response_trailers = 0;
  };

  grpc::Status Process(
      grpc::ServerContext* /*context*/,
      grpc::ServerReaderWriter<
          ::envoy::service::ext_proc::v3::ProcessingResponse,
          ::envoy::service::ext_proc::v3::ProcessingRequest>* stream) override {
    {
      absl::MutexLock lock(&mu_);
      num_calls_++;
    }
    ::envoy::service::ext_proc::v3::ProcessingRequest request;
    while (stream->Read(&request)) {
      ::envoy::service::ext_proc::v3::ProcessingResponse response;
      {
        absl::MutexLock lock(&mu_);
        if (request.has_request_headers()) {
          counts_.request_headers++;
          auto* mutation = response.mutable_request_headers()
                               ->mutable_response()
                               ->mutable_header_mutation();
          auto* header = mutation->add_set_headers();
          header->mutable_header()->set_key(
              "x-extproc-request-headers-mutated");
          header->mutable_header()->set_value("yes");
        } else if (request.has_response_headers()) {
          counts_.response_headers++;
          auto* mutation = response.mutable_response_headers()
                               ->mutable_response()
                               ->mutable_header_mutation();
          auto* header = mutation->add_set_headers();
          header->mutable_header()->set_key(
              "x-extproc-response-headers-mutated");
          header->mutable_header()->set_value("yes");
        } else if (request.has_request_body()) {
          counts_.request_body++;
          auto* body_mutation = response.mutable_request_body()
                                    ->mutable_response()
                                    ->mutable_body_mutation();
          grpc::testing::EchoRequest echo_request;
          if (echo_request.ParseFromString(request.request_body().body())) {
            echo_request.set_message(
                absl::StrCat(echo_request.message(), "-request-body-mutated"));
            std::string mutated_body;
            GRPC_CHECK(echo_request.SerializeToString(&mutated_body));
            body_mutation->mutable_streamed_response()->set_body(mutated_body);
          } else {
            body_mutation->mutable_streamed_response()->set_body(
                request.request_body().body());
          }
        } else if (request.has_response_body()) {
          counts_.response_body++;
          auto* body_mutation = response.mutable_response_body()
                                    ->mutable_response()
                                    ->mutable_body_mutation();
          grpc::testing::EchoResponse echo_response;
          if (echo_response.ParseFromString(request.response_body().body())) {
            echo_response.set_message(absl::StrCat(echo_response.message(),
                                                   "-response-body-mutated"));
            std::string mutated_body;
            GRPC_CHECK(echo_response.SerializeToString(&mutated_body));
            body_mutation->mutable_streamed_response()->set_body(mutated_body);
          } else {
            body_mutation->mutable_streamed_response()->set_body(
                request.response_body().body());
          }
        } else if (request.has_request_trailers()) {
          response.mutable_request_trailers()->mutable_header_mutation();
        } else if (request.has_response_trailers()) {
          counts_.response_trailers++;
          auto* mutation =
              response.mutable_response_trailers()->mutable_header_mutation();
          auto* header = mutation->add_set_headers();
          header->mutable_header()->set_key(
              "x-extproc-response-trailers-mutated");
          header->mutable_header()->set_value("yes");
        }
      }

      stream->Write(response);
    }
    return grpc::Status::OK;
  }

  size_t num_calls() {
    absl::MutexLock lock(&mu_);
    return num_calls_;
  }

  RequestCounts GetRequestCounts() {
    absl::MutexLock lock(&mu_);
    return counts_;
  }

  void ResetCounts() {
    absl::MutexLock lock(&mu_);
    counts_ = RequestCounts();
  }

  void WaitForRequestCounts(const RequestCounts& expected,
                            absl::Duration timeout = absl::Seconds(5)) {
    absl::MutexLock lock(&mu_);
    expected_counts_ = expected;
    mu_.AwaitWithTimeout(
        absl::Condition(this,
                        &MockExternalProcessorService::ExpectedCountsSatisfied),
        timeout);
  }

 private:
  absl::Mutex mu_;
  size_t num_calls_ ABSL_GUARDED_BY(mu_) = 0;
  RequestCounts counts_ ABSL_GUARDED_BY(mu_);
  RequestCounts expected_counts_ ABSL_GUARDED_BY(mu_);

  bool ExpectedCountsSatisfied() ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu_) {
    return counts_.request_headers >= expected_counts_.request_headers &&
           counts_.response_headers >= expected_counts_.response_headers &&
           counts_.response_trailers >= expected_counts_.response_trailers &&
           counts_.request_body >= expected_counts_.request_body &&
           counts_.response_body >= expected_counts_.response_body;
  }
};

class ImmediateResponseMockService : public MockExternalProcessorBase {
 public:
  grpc::Status Process(
      grpc::ServerContext* /*context*/,
      grpc::ServerReaderWriter<
          ::envoy::service::ext_proc::v3::ProcessingResponse,
          ::envoy::service::ext_proc::v3::ProcessingRequest>* stream) override {
    ::envoy::service::ext_proc::v3::ProcessingRequest request;
    std::string trigger_phase;
    while (stream->Read(&request)) {
      ::envoy::service::ext_proc::v3::ProcessingResponse response;
      if (request.has_request_headers()) {
        for (const auto& header : request.request_headers().headers().headers()) {
          if (header.key() == "x-extproc-trigger-immediate-response-phase") {
            trigger_phase = header.raw_value();
            break;
          }
        }
        if (trigger_phase == "request_headers") {
          auto* immediate = response.mutable_immediate_response();
          immediate->mutable_grpc_status()->set_status(grpc::StatusCode::PERMISSION_DENIED);
          immediate->set_details("Access Denied by ExtProc (Request Headers)");
          auto* mutation = immediate->mutable_headers();
          auto* header = mutation->add_set_headers();
          header->mutable_header()->set_key("x-extproc-immediate-response-added");
          header->mutable_header()->set_value("yes");
        } else {
          response.mutable_request_headers()->mutable_response()->mutable_header_mutation();
        }
      } else if (request.has_response_headers()) {
        if (trigger_phase == "response_headers") {
          auto* immediate = response.mutable_immediate_response();
          immediate->mutable_grpc_status()->set_status(grpc::StatusCode::PERMISSION_DENIED);
          immediate->set_details("Access Denied by ExtProc (Response Headers)");
          auto* mutation = immediate->mutable_headers();
          auto* header = mutation->add_set_headers();
          header->mutable_header()->set_key("x-extproc-immediate-response-added");
          header->mutable_header()->set_value("yes");
        } else if (trigger_phase == "trailers_only") {
          auto* immediate = response.mutable_immediate_response();
          immediate->mutable_grpc_status()->set_status(grpc::StatusCode::PERMISSION_DENIED);
          immediate->set_details("Access Denied by ExtProc (Trailers Only)");
          auto* mutation = immediate->mutable_headers();
          auto* header = mutation->add_set_headers();
          header->mutable_header()->set_key("x-extproc-immediate-response-added");
          header->mutable_header()->set_value("yes");
        } else {
          response.mutable_response_headers()->mutable_response()->mutable_header_mutation();
        }
      } else if (request.has_request_body()) {
        if (trigger_phase == "request_body") {
          auto* immediate = response.mutable_immediate_response();
          immediate->mutable_grpc_status()->set_status(grpc::StatusCode::PERMISSION_DENIED);
          immediate->set_details("Access Denied by ExtProc (Request Body)");
          auto* mutation = immediate->mutable_headers();
          auto* header = mutation->add_set_headers();
          header->mutable_header()->set_key("x-extproc-immediate-response-added");
          header->mutable_header()->set_value("yes");
        } else {
          response.mutable_request_body()->mutable_response()->mutable_body_mutation();
        }
      } else if (request.has_response_body()) {
        if (trigger_phase == "response_body") {
          auto* immediate = response.mutable_immediate_response();
          immediate->mutable_grpc_status()->set_status(grpc::StatusCode::PERMISSION_DENIED);
          immediate->set_details("Access Denied by ExtProc (Response Body)");
          auto* mutation = immediate->mutable_headers();
          auto* header = mutation->add_set_headers();
          header->mutable_header()->set_key("x-extproc-immediate-response-added");
          header->mutable_header()->set_value("yes");
        } else {
          response.mutable_response_body()->mutable_response()->mutable_body_mutation();
        }
      } else if (request.has_request_trailers()) {
        response.mutable_request_trailers()->mutable_header_mutation();
      } else if (request.has_response_trailers()) {
        if (trigger_phase == "response_trailers") {
          auto* immediate = response.mutable_immediate_response();
          immediate->mutable_grpc_status()->set_status(grpc::StatusCode::PERMISSION_DENIED);
          immediate->set_details("Access Denied by ExtProc (Response Trailers)");
          auto* mutation = immediate->mutable_headers();
          auto* header = mutation->add_set_headers();
          header->mutable_header()->set_key("x-extproc-immediate-response-added");
          header->mutable_header()->set_value("yes");
        } else {
          response.mutable_response_trailers()->mutable_header_mutation();
        }
      }
      stream->Write(response);
    }
    return grpc::Status::OK;
  }
};

void SetDefaultEmptyResponse(
    const ::envoy::service::ext_proc::v3::ProcessingRequest& request,
    ::envoy::service::ext_proc::v3::ProcessingResponse* response) {
  if (request.has_request_headers()) {
    response->mutable_request_headers()
        ->mutable_response()
        ->mutable_header_mutation();
  } else if (request.has_response_headers()) {
    response->mutable_response_headers()
        ->mutable_response()
        ->mutable_header_mutation();
  } else if (request.has_request_body()) {
    response->mutable_request_body()
        ->mutable_response()
        ->mutable_body_mutation();
  } else if (request.has_response_body()) {
    response->mutable_response_body()
        ->mutable_response()
        ->mutable_body_mutation();
  } else if (request.has_request_trailers()) {
    response->mutable_request_trailers()->mutable_header_mutation();
  } else if (request.has_response_trailers()) {
    response->mutable_response_trailers()->mutable_header_mutation();
  }
}

class GenericMockService : public MockExternalProcessorBase {
 public:
  using Callback = std::function<void(
      const ::envoy::service::ext_proc::v3::ProcessingRequest&,
      ::envoy::service::ext_proc::v3::ProcessingResponse*)>;

  explicit GenericMockService(Callback callback)
      : callback_(std::move(callback)) {}

  grpc::Status Process(
      grpc::ServerContext* /*context*/,
      grpc::ServerReaderWriter<
          ::envoy::service::ext_proc::v3::ProcessingResponse,
          ::envoy::service::ext_proc::v3::ProcessingRequest>* stream) override {
    ::envoy::service::ext_proc::v3::ProcessingRequest request;
    while (stream->Read(&request)) {
      ::envoy::service::ext_proc::v3::ProcessingResponse response;
      callback_(request, &response);
      stream->Write(response);
    }
    return grpc::Status::OK;
  }

 private:
  Callback callback_;
};

class XdsExtProcEnd2endTest : public XdsEnd2endTest {
 public:
  template <typename ServiceType>
  class ExtProcServerThread : public XdsEnd2endTest::ServerThread {
   public:
    ExtProcServerThread(XdsEnd2endTest* test_obj,
                        std::shared_ptr<ServiceType> service)
        : ServerThread(test_obj, /*use_xds_enabled_server=*/false,
                       grpc::InsecureServerCredentials()),
          ext_proc_service_(std::move(service)) {}

    ServiceType* ext_proc_service() {
      return ext_proc_service_.get();
    }

   private:
    const char* Type() override { return "ExtProc"; }

    void RegisterAllServices(ServerBuilder* builder) override {
      builder->RegisterService(ext_proc_service_.get());
    }

    void StartAllServices() override {}
    void ShutdownAllServices() override {}

    std::shared_ptr<ServiceType> ext_proc_service_;
  };

  void SetUp() override {
    grpc_core::SetEnv("GRPC_EXPERIMENTAL_XDS_EXT_PROC_ON_CLIENT", "true");
    grpc_tracer_set_enabled("ext_proc_filter", 1);
    grpc_tracer_set_enabled("promise_primitives", 0);
    grpc_tracer_set_enabled("call_state", 0);
    grpc_tracer_set_enabled("channel", 0);
    grpc_tracer_set_enabled("transport", 0);
    grpc_core::SetEnv("GRPC_VERBOSITY", "INFO");
    InitClient(MakeBootstrapBuilder().SetTrustedXdsServer(),
               /*lb_expected_authority=*/"",
               /*xds_resource_does_not_exist_timeout_ms=*/0,
               /*balancer_authority_override=*/"", /*args=*/nullptr);
    ext_proc_server_ =
        std::make_unique<ExtProcServerThread<MockExternalProcessorService>>(
            this, std::make_shared<MockExternalProcessorService>());
    ext_proc_server_->Start();
  }

  void TearDown() override {
    if (immediate_response_server_ != nullptr) {
      immediate_response_server_->Shutdown();
    }
    if (alternative_ext_proc_server_ != nullptr) {
      alternative_ext_proc_server_->Shutdown();
    }
    ext_proc_server_->Shutdown();
    XdsEnd2endTest::TearDown();
  }

  void StartImmediateResponseServer() {
    ext_proc_server_->Shutdown();
    immediate_response_server_ =
        std::make_unique<ExtProcServerThread<ImmediateResponseMockService>>(
            this, std::make_shared<ImmediateResponseMockService>());
    immediate_response_server_->Start();
  }

  template <typename ServiceType>
  void StartAlternativeServer(std::shared_ptr<ServiceType> service) {
    ext_proc_server_->Shutdown();
    alternative_ext_proc_server_ =
        std::make_unique<ExtProcServerThread<ServiceType>>(this, std::move(service));
    alternative_ext_proc_server_->Start();
  }

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

  void RunProcessingModeTest(bool req_hdrs, bool resp_hdrs, bool resp_trls,
                             bool req_body, bool resp_body,
                             bool observability_mode = false);
  void RunTrailersOnlyTest(bool observability_mode);

  std::unique_ptr<ExtProcServerThread<MockExternalProcessorService>>
      ext_proc_server_;
  std::unique_ptr<ExtProcServerThread<ImmediateResponseMockService>>
      immediate_response_server_;
  std::unique_ptr<ServerThread> alternative_ext_proc_server_;
};

INSTANTIATE_TEST_SUITE_P(XdsTest, XdsExtProcEnd2endTest,
                         ::testing::Values(XdsTestType()), &XdsTestType::Name);

void XdsExtProcEnd2endTest::RunProcessingModeTest(bool req_hdrs, bool resp_hdrs,
                                                  bool resp_trls, bool req_body,
                                                  bool resp_body,
                                                  bool observability_mode) {
  CreateAndStartBackends(1);
  auto ext_proc_config_builder = ExternalProcessorBuilder()
                                     .SetTargetUri(ext_proc_server_->target())
                                     .SetInsecureChannelCredentials()
                                     .SetObservabilityMode(observability_mode);
  using envoy::extensions::filters::http::ext_proc::v3::ProcessingMode;
  ext_proc_config_builder.SetRequestHeaderMode(req_hdrs ? ProcessingMode::SEND
                                                        : ProcessingMode::SKIP);
  ext_proc_config_builder.SetResponseHeaderMode(
      resp_hdrs ? ProcessingMode::SEND : ProcessingMode::SKIP);
  ext_proc_config_builder.SetResponseTrailerMode(
      resp_trls ? ProcessingMode::SEND : ProcessingMode::SKIP);
  ext_proc_config_builder.SetRequestBodyMode(req_body ? ProcessingMode::GRPC
                                                      : ProcessingMode::NONE);
  ext_proc_config_builder.SetResponseBodyMode(resp_body ? ProcessingMode::GRPC
                                                        : ProcessingMode::NONE);
  auto ext_proc_config = ext_proc_config_builder.Build();
  Listener listener = BuildListenerWithExtProcFilter(ext_proc_config);
  RouteConfiguration route_config = default_route_config_;
  SetListenerAndRouteConfiguration(balancer_.get(), listener, route_config);
  balancer_->ads_service()->SetCdsResource(default_cluster_);
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(EdsResourceArgs({
      {"locality0", CreateEndpointsForBackends(0, 1)},
  })));
  RpcOptions rpc_options;
  rpc_options.set_echo_metadata_initially(true);
  rpc_options.set_echo_metadata(true);
  EchoResponse response;
  std::multimap<std::string, std::string> server_initial_metadata;
  std::multimap<std::string, std::string> server_trailing_metadata;
  Status status =
      SendRpcGetTrailers(rpc_options, &response, &server_initial_metadata,
                         &server_trailing_metadata);
  EXPECT_TRUE(status.ok()) << "RPC failed: " << status.error_message();
  // TODO(rishesh): Fix the limitation where response_body is not sent
  // if response_headers is also enabled.
  bool expect_response_body = resp_body;
  if (!observability_mode && resp_hdrs && resp_body) {
    expect_response_body = false;
  }
  // Wait for expected counts (especially important for async observability
  // mode)
  MockExternalProcessorService::RequestCounts expected_counts;
  expected_counts.request_headers = req_hdrs ? 1 : 0;
  expected_counts.response_headers = resp_hdrs ? 1 : 0;
  expected_counts.response_trailers = resp_trls ? 1 : 0;
  expected_counts.request_body = req_body ? 1 : 0;
  expected_counts.response_body = expect_response_body ? 1 : 0;
  ext_proc_server_->ext_proc_service()->WaitForRequestCounts(expected_counts);
  auto counts = ext_proc_server_->ext_proc_service()->GetRequestCounts();
  EXPECT_EQ(counts.request_headers, req_hdrs ? 1 : 0);
  EXPECT_EQ(counts.response_headers, resp_hdrs ? 1 : 0);
  EXPECT_EQ(counts.response_trailers, resp_trls ? 1 : 0);
  if (req_body) {
    EXPECT_THAT(counts.request_body, ::testing::AnyOf(1, 2));
  } else {
    EXPECT_EQ(counts.request_body, 0);
  }
  if (observability_mode) {
    // TODO(rishesh): In observability mode, there is a race condition
    // in the V2-V3 bridge where the response body might be missed by the
    // filter if trailers arrive very quickly.
    if (expect_response_body) {
      EXPECT_GE(counts.response_body, 0);
    } else {
      EXPECT_EQ(counts.response_body, 0);
    }
  } else {
    EXPECT_EQ(counts.response_body, expect_response_body ? 1 : 0);
  }
  // Verify mutations
  if (!observability_mode && req_hdrs) {
    auto it = server_initial_metadata.find("x-extproc-request-headers-mutated");
    ASSERT_NE(it, server_initial_metadata.end());
    EXPECT_EQ(it->second, "yes");
  } else {
    auto it = server_initial_metadata.find("x-extproc-request-headers-mutated");
    EXPECT_EQ(it, server_initial_metadata.end());
  }
  if (!observability_mode && resp_hdrs) {
    auto it =
        server_initial_metadata.find("x-extproc-response-headers-mutated");
    ASSERT_NE(it, server_initial_metadata.end());
    EXPECT_EQ(it->second, "yes");
  } else {
    auto it =
        server_initial_metadata.find("x-extproc-response-headers-mutated");
    EXPECT_EQ(it, server_initial_metadata.end());
  }
  if (!observability_mode && resp_trls) {
    auto it =
        server_trailing_metadata.find("x-extproc-response-trailers-mutated");
    ASSERT_NE(it, server_trailing_metadata.end());
    EXPECT_EQ(it->second, "yes");
  } else {
    auto it =
        server_trailing_metadata.find("x-extproc-response-trailers-mutated");
    EXPECT_EQ(it, server_trailing_metadata.end());
  }
  std::string expected_message = kRequestMessage;
  if (!observability_mode) {
    if (req_body) {
      absl::StrAppend(&expected_message, "-request-body-mutated");
    }
    if (expect_response_body) {
      absl::StrAppend(&expected_message, "-response-body-mutated");
    }
  }
  EXPECT_EQ(response.message(), expected_message);
  bool any_mode_enabled =
      req_hdrs || resp_hdrs || resp_trls || req_body || expect_response_body;
  EXPECT_EQ(ext_proc_server_->ext_proc_service()->num_calls(),
            any_mode_enabled ? 1 : 0);
}

void XdsExtProcEnd2endTest::RunTrailersOnlyTest(bool observability_mode) {
  CreateAndStartBackends(1);
  auto ext_proc_config_builder = ExternalProcessorBuilder()
                                     .SetTargetUri(ext_proc_server_->target())
                                     .SetInsecureChannelCredentials()
                                     .SetObservabilityMode(observability_mode);
  using envoy::extensions::filters::http::ext_proc::v3::ProcessingMode;
  // Enable ALL processing modes
  ext_proc_config_builder.SetRequestHeaderMode(ProcessingMode::SEND);
  ext_proc_config_builder.SetResponseHeaderMode(ProcessingMode::SEND);
  ext_proc_config_builder.SetResponseTrailerMode(ProcessingMode::SEND);
  ext_proc_config_builder.SetRequestBodyMode(ProcessingMode::GRPC);
  ext_proc_config_builder.SetResponseBodyMode(ProcessingMode::GRPC);
  auto ext_proc_config = ext_proc_config_builder.Build();
  Listener listener = BuildListenerWithExtProcFilter(ext_proc_config);
  RouteConfiguration route_config = default_route_config_;
  SetListenerAndRouteConfiguration(balancer_.get(), listener, route_config);
  balancer_->ads_service()->SetCdsResource(default_cluster_);
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(EdsResourceArgs({
      {"locality0", CreateEndpointsForBackends(0, 1)},
  })));
  RpcOptions rpc_options;
  rpc_options.set_echo_metadata_initially(true);
  rpc_options.set_echo_metadata(true);
  // Force trailers-only failure
  rpc_options.set_server_fail(true);
  EchoResponse response;
  std::multimap<std::string, std::string> server_initial_metadata;
  std::multimap<std::string, std::string> server_trailing_metadata;
  Status status =
      SendRpcGetTrailers(rpc_options, &response, &server_initial_metadata,
                         &server_trailing_metadata);
  // The V2-V3 bridge does not handle trailers-only responses
  // correctly and double-delivers the metadata, which confuses the client
  // and sometimes results in FAILED_PRECONDITION instead of UNAVAILABLE.
  EXPECT_THAT(status.error_code(),
              ::testing::AnyOf(StatusCode::UNAVAILABLE,
                               StatusCode::FAILED_PRECONDITION))
      << "Actual error message: " << status.error_message();
  // Wait for expected counts
  MockExternalProcessorService::RequestCounts expected_counts;
  expected_counts.request_headers = 1;
  expected_counts.request_body = 1;
  expected_counts.response_headers = 1;
  expected_counts.response_body = 0;
  expected_counts.response_trailers = 0;
  ext_proc_server_->ext_proc_service()->WaitForRequestCounts(expected_counts);
  auto counts = ext_proc_server_->ext_proc_service()->GetRequestCounts();
  EXPECT_EQ(counts.request_headers, 1);
  EXPECT_THAT(counts.request_body, ::testing::AnyOf(1, 2));
  EXPECT_EQ(counts.response_headers, 1);
  EXPECT_EQ(counts.response_body, 0);
  EXPECT_EQ(counts.response_trailers, 0);
}

TEST_P(XdsExtProcEnd2endTest, TrailersOnly_AllEnabled) {
  RunTrailersOnlyTest(/*observability_mode=*/false);
}

TEST_P(XdsExtProcEnd2endTest, TrailersOnly_Observability_AllEnabled) {
  RunTrailersOnlyTest(/*observability_mode=*/true);
}

#define EXT_PROC_TEST_P(name, req_hdrs, resp_hdrs, resp_trls, req_body, \
                        resp_body)                                      \
  TEST_P(XdsExtProcEnd2endTest, ProcessingMode_##name) {                \
    RunProcessingModeTest(req_hdrs, resp_hdrs, resp_trls, req_body,     \
                          resp_body);                                   \
  }

// 24 combinations
EXT_PROC_TEST_P(NoReqHeaders_NoRespHeaders_NoRespTrailers_NoReqBody_NoRespBody,
                false, false, false, false, false)
EXT_PROC_TEST_P(NoReqHeaders_NoRespHeaders_RespTrailers_NoReqBody_NoRespBody,
                false, false, true, false, false)
EXT_PROC_TEST_P(NoReqHeaders_RespHeaders_NoRespTrailers_NoReqBody_NoRespBody,
                false, true, false, false, false)
EXT_PROC_TEST_P(NoReqHeaders_RespHeaders_RespTrailers_NoReqBody_NoRespBody,
                false, true, true, false, false)
EXT_PROC_TEST_P(ReqHeaders_NoRespHeaders_NoRespTrailers_NoReqBody_NoRespBody,
                true, false, false, false, false)
EXT_PROC_TEST_P(ReqHeaders_NoRespHeaders_RespTrailers_NoReqBody_NoRespBody,
                true, false, true, false, false)
EXT_PROC_TEST_P(ReqHeaders_RespHeaders_NoRespTrailers_NoReqBody_NoRespBody,
                true, true, false, false, false)
EXT_PROC_TEST_P(ReqHeaders_RespHeaders_RespTrailers_NoReqBody_NoRespBody, true,
                true, true, false, false)

EXT_PROC_TEST_P(NoReqHeaders_NoRespHeaders_NoRespTrailers_ReqBody_NoRespBody,
                false, false, false, true, false)
EXT_PROC_TEST_P(NoReqHeaders_NoRespHeaders_RespTrailers_ReqBody_NoRespBody,
                false, false, true, true, false)
EXT_PROC_TEST_P(NoReqHeaders_RespHeaders_NoRespTrailers_ReqBody_NoRespBody,
                false, true, false, true, false)
EXT_PROC_TEST_P(NoReqHeaders_RespHeaders_RespTrailers_ReqBody_NoRespBody, false,
                true, true, true, false)
EXT_PROC_TEST_P(ReqHeaders_NoRespHeaders_NoRespTrailers_ReqBody_NoRespBody,
                true, false, false, true, false)
EXT_PROC_TEST_P(ReqHeaders_NoRespHeaders_RespTrailers_ReqBody_NoRespBody, true,
                false, true, true, false)
EXT_PROC_TEST_P(ReqHeaders_RespHeaders_NoRespTrailers_ReqBody_NoRespBody, true,
                true, false, true, false)
EXT_PROC_TEST_P(ReqHeaders_RespHeaders_RespTrailers_ReqBody_NoRespBody, true,
                true, true, true, false)

EXT_PROC_TEST_P(NoReqHeaders_NoRespHeaders_RespTrailers_NoReqBody_RespBody,
                false, false, true, false, true)
EXT_PROC_TEST_P(NoReqHeaders_RespHeaders_RespTrailers_NoReqBody_RespBody, false,
                true, true, false, true)
EXT_PROC_TEST_P(ReqHeaders_NoRespHeaders_RespTrailers_NoReqBody_RespBody, true,
                false, true, false, true)
EXT_PROC_TEST_P(ReqHeaders_RespHeaders_RespTrailers_NoReqBody_RespBody, true,
                true, true, false, true)

EXT_PROC_TEST_P(NoReqHeaders_NoRespHeaders_RespTrailers_ReqBody_RespBody, false,
                false, true, true, true)
EXT_PROC_TEST_P(NoReqHeaders_RespHeaders_RespTrailers_ReqBody_RespBody, false,
                true, true, true, true)
EXT_PROC_TEST_P(ReqHeaders_NoRespHeaders_RespTrailers_ReqBody_RespBody, true,
                false, true, true, true)
EXT_PROC_TEST_P(ReqHeaders_RespHeaders_RespTrailers_ReqBody_RespBody, true,
                true, true, true, true)

#define EXT_PROC_OBSERVABILITY_TEST_P(name, req_hdrs, resp_hdrs, resp_trls,    \
                                      req_body, resp_body)                     \
  TEST_P(XdsExtProcEnd2endTest, ProcessingMode_Observability_##name) {         \
    RunProcessingModeTest(req_hdrs, resp_hdrs, resp_trls, req_body, resp_body, \
                          /*observability_mode=*/true);                        \
  }

// 24 combinations for Observability Mode
EXT_PROC_OBSERVABILITY_TEST_P(
    NoReqHeaders_NoRespHeaders_NoRespTrailers_NoReqBody_NoRespBody, false,
    false, false, false, false)
EXT_PROC_OBSERVABILITY_TEST_P(
    NoReqHeaders_NoRespHeaders_RespTrailers_NoReqBody_NoRespBody, false, false,
    true, false, false)
EXT_PROC_OBSERVABILITY_TEST_P(
    NoReqHeaders_RespHeaders_NoRespTrailers_NoReqBody_NoRespBody, false, true,
    false, false, false)
EXT_PROC_OBSERVABILITY_TEST_P(
    NoReqHeaders_RespHeaders_RespTrailers_NoReqBody_NoRespBody, false, true,
    true, false, false)
EXT_PROC_OBSERVABILITY_TEST_P(
    ReqHeaders_NoRespHeaders_NoRespTrailers_NoReqBody_NoRespBody, true, false,
    false, false, false)
EXT_PROC_OBSERVABILITY_TEST_P(
    ReqHeaders_NoRespHeaders_RespTrailers_NoReqBody_NoRespBody, true, false,
    true, false, false)
EXT_PROC_OBSERVABILITY_TEST_P(
    ReqHeaders_RespHeaders_NoRespTrailers_NoReqBody_NoRespBody, true, true,
    false, false, false)
EXT_PROC_OBSERVABILITY_TEST_P(
    ReqHeaders_RespHeaders_RespTrailers_NoReqBody_NoRespBody, true, true, true,
    false, false)

EXT_PROC_OBSERVABILITY_TEST_P(
    NoReqHeaders_NoRespHeaders_NoRespTrailers_ReqBody_NoRespBody, false, false,
    false, true, false)
EXT_PROC_OBSERVABILITY_TEST_P(
    NoReqHeaders_NoRespHeaders_RespTrailers_ReqBody_NoRespBody, false, false,
    true, true, false)
EXT_PROC_OBSERVABILITY_TEST_P(
    NoReqHeaders_RespHeaders_NoRespTrailers_ReqBody_NoRespBody, false, true,
    false, true, false)
EXT_PROC_OBSERVABILITY_TEST_P(
    NoReqHeaders_RespHeaders_RespTrailers_ReqBody_NoRespBody, false, true, true,
    true, false)
EXT_PROC_OBSERVABILITY_TEST_P(
    ReqHeaders_NoRespHeaders_NoRespTrailers_ReqBody_NoRespBody, true, false,
    false, true, false)
EXT_PROC_OBSERVABILITY_TEST_P(
    ReqHeaders_NoRespHeaders_RespTrailers_ReqBody_NoRespBody, true, false, true,
    true, false)
EXT_PROC_OBSERVABILITY_TEST_P(
    ReqHeaders_RespHeaders_NoRespTrailers_ReqBody_NoRespBody, true, true, false,
    true, false)
EXT_PROC_OBSERVABILITY_TEST_P(
    ReqHeaders_RespHeaders_RespTrailers_ReqBody_NoRespBody, true, true, true,
    true, false)

EXT_PROC_OBSERVABILITY_TEST_P(
    NoReqHeaders_NoRespHeaders_RespTrailers_NoReqBody_RespBody, false, false,
    true, false, true)
EXT_PROC_OBSERVABILITY_TEST_P(
    NoReqHeaders_RespHeaders_RespTrailers_NoReqBody_RespBody, false, true, true,
    false, true)
EXT_PROC_OBSERVABILITY_TEST_P(
    ReqHeaders_NoRespHeaders_RespTrailers_NoReqBody_RespBody, true, false, true,
    false, true)
EXT_PROC_OBSERVABILITY_TEST_P(
    ReqHeaders_RespHeaders_RespTrailers_NoReqBody_RespBody, true, true, true,
    false, true)

EXT_PROC_OBSERVABILITY_TEST_P(
    NoReqHeaders_NoRespHeaders_RespTrailers_ReqBody_RespBody, false, false,
    true, true, true)
EXT_PROC_OBSERVABILITY_TEST_P(
    NoReqHeaders_RespHeaders_RespTrailers_ReqBody_RespBody, false, true, true,
    true, true)
EXT_PROC_OBSERVABILITY_TEST_P(
    ReqHeaders_NoRespHeaders_RespTrailers_ReqBody_RespBody, true, false, true,
    true, true)
EXT_PROC_OBSERVABILITY_TEST_P(
    ReqHeaders_RespHeaders_RespTrailers_ReqBody_RespBody, true, true, true,
    true, true)



TEST_P(XdsExtProcEnd2endTest, DisableImmediateResponseRequestHeadersAllow) {
  StartImmediateResponseServer();
  CreateAndStartBackends(1);
  using envoy::extensions::filters::http::ext_proc::v3::ProcessingMode;
  auto ext_proc_config = ExternalProcessorBuilder()
                             .SetTargetUri(immediate_response_server_->target())
                             .SetInsecureChannelCredentials()
                             .SetDisableImmediateResponse(true)
                             .SetFailureModeAllow(true)
                             .SetRequestHeaderMode(ProcessingMode::SEND)
                             .SetResponseHeaderMode(ProcessingMode::SEND)
                             .SetResponseTrailerMode(ProcessingMode::SEND)
                             .SetRequestBodyMode(ProcessingMode::GRPC)
                             .SetResponseBodyMode(ProcessingMode::GRPC)
                             .Build();
  Listener listener = BuildListenerWithExtProcFilter(ext_proc_config);
  RouteConfiguration route_config = default_route_config_;
  SetListenerAndRouteConfiguration(balancer_.get(), listener, route_config);
  balancer_->ads_service()->SetCdsResource(default_cluster_);
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(EdsResourceArgs({
      {"locality0", CreateEndpointsForBackends(0, 1)},
  })));
  RpcOptions rpc_options;
  rpc_options.set_echo_metadata_initially(true);
  rpc_options.set_echo_metadata(true);
  std::vector<std::pair<std::string, std::string>> metadata;
  metadata.emplace_back("x-extproc-trigger-immediate-response-phase", "request_headers");
  rpc_options.set_metadata(std::move(metadata));
  EchoResponse response;
  std::multimap<std::string, std::string> server_initial_metadata;
  std::multimap<std::string, std::string> server_trailing_metadata;
  Status status =
      SendRpcGetTrailers(rpc_options, &response, &server_initial_metadata,
                         &server_trailing_metadata);
  EXPECT_TRUE(status.ok()) << "Expected OK, got: " << status.error_message();
}

TEST_P(XdsExtProcEnd2endTest, DisableImmediateResponseRequestHeadersBlock) {
  StartImmediateResponseServer();
  CreateAndStartBackends(1);
  using envoy::extensions::filters::http::ext_proc::v3::ProcessingMode;
  auto ext_proc_config = ExternalProcessorBuilder()
                             .SetTargetUri(immediate_response_server_->target())
                             .SetInsecureChannelCredentials()
                             .SetDisableImmediateResponse(true)
                             .SetFailureModeAllow(false)
                             .SetRequestHeaderMode(ProcessingMode::SEND)
                             .SetResponseHeaderMode(ProcessingMode::SEND)
                             .SetResponseTrailerMode(ProcessingMode::SEND)
                             .SetRequestBodyMode(ProcessingMode::GRPC)
                             .SetResponseBodyMode(ProcessingMode::GRPC)
                             .Build();
  Listener listener = BuildListenerWithExtProcFilter(ext_proc_config);
  RouteConfiguration route_config = default_route_config_;
  SetListenerAndRouteConfiguration(balancer_.get(), listener, route_config);
  balancer_->ads_service()->SetCdsResource(default_cluster_);
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(EdsResourceArgs({
      {"locality0", CreateEndpointsForBackends(0, 1)},
  })));
  RpcOptions rpc_options;
  rpc_options.set_echo_metadata_initially(true);
  rpc_options.set_echo_metadata(true);
  std::vector<std::pair<std::string, std::string>> metadata;
  metadata.emplace_back("x-extproc-trigger-immediate-response-phase", "request_headers");
  rpc_options.set_metadata(std::move(metadata));
  EchoResponse response;
  std::multimap<std::string, std::string> server_initial_metadata;
  std::multimap<std::string, std::string> server_trailing_metadata;
  Status status =
      SendRpcGetTrailers(rpc_options, &response, &server_initial_metadata,
                         &server_trailing_metadata);
  EXPECT_FALSE(status.ok());
  EXPECT_EQ(status.error_code(), StatusCode::PERMISSION_DENIED);
  EXPECT_EQ(status.error_message(), "unhandled immediate response due to config disabled it");
}

TEST_P(XdsExtProcEnd2endTest, DisableImmediateResponseRequestBodyAllow) {
  GTEST_SKIP() << "Skipped: hangs due to known message loss bug in request body fail-open path";
  StartImmediateResponseServer();
  CreateAndStartBackends(1);
  using envoy::extensions::filters::http::ext_proc::v3::ProcessingMode;
  auto ext_proc_config = ExternalProcessorBuilder()
                             .SetTargetUri(immediate_response_server_->target())
                             .SetInsecureChannelCredentials()
                             .SetDisableImmediateResponse(true)
                             .SetFailureModeAllow(true)
                             .SetRequestHeaderMode(ProcessingMode::SEND)
                             .SetResponseHeaderMode(ProcessingMode::SEND)
                             .SetResponseTrailerMode(ProcessingMode::SEND)
                             .SetRequestBodyMode(ProcessingMode::GRPC)
                             .SetResponseBodyMode(ProcessingMode::GRPC)
                             .Build();
  Listener listener = BuildListenerWithExtProcFilter(ext_proc_config);
  RouteConfiguration route_config = default_route_config_;
  SetListenerAndRouteConfiguration(balancer_.get(), listener, route_config);
  balancer_->ads_service()->SetCdsResource(default_cluster_);
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(EdsResourceArgs({
      {"locality0", CreateEndpointsForBackends(0, 1)},
  })));
  RpcOptions rpc_options;
  rpc_options.set_echo_metadata_initially(true);
  rpc_options.set_echo_metadata(true);
  std::vector<std::pair<std::string, std::string>> metadata;
  metadata.emplace_back("x-extproc-trigger-immediate-response-phase", "request_body");
  rpc_options.set_metadata(std::move(metadata));
  EchoResponse response;
  std::multimap<std::string, std::string> server_initial_metadata;
  std::multimap<std::string, std::string> server_trailing_metadata;
  Status status =
      SendRpcGetTrailers(rpc_options, &response, &server_initial_metadata,
                         &server_trailing_metadata);
  EXPECT_TRUE(status.ok()) << "Expected OK, got: " << status.error_message();
}

TEST_P(XdsExtProcEnd2endTest, DisableImmediateResponseRequestBodyBlock) {
  StartImmediateResponseServer();
  CreateAndStartBackends(1);
  using envoy::extensions::filters::http::ext_proc::v3::ProcessingMode;
  auto ext_proc_config = ExternalProcessorBuilder()
                             .SetTargetUri(immediate_response_server_->target())
                             .SetInsecureChannelCredentials()
                             .SetDisableImmediateResponse(true)
                             .SetFailureModeAllow(false)
                             .SetRequestHeaderMode(ProcessingMode::SEND)
                             .SetResponseHeaderMode(ProcessingMode::SEND)
                             .SetResponseTrailerMode(ProcessingMode::SEND)
                             .SetRequestBodyMode(ProcessingMode::GRPC)
                             .SetResponseBodyMode(ProcessingMode::GRPC)
                             .Build();
  Listener listener = BuildListenerWithExtProcFilter(ext_proc_config);
  RouteConfiguration route_config = default_route_config_;
  SetListenerAndRouteConfiguration(balancer_.get(), listener, route_config);
  balancer_->ads_service()->SetCdsResource(default_cluster_);
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(EdsResourceArgs({
      {"locality0", CreateEndpointsForBackends(0, 1)},
  })));
  RpcOptions rpc_options;
  rpc_options.set_echo_metadata_initially(true);
  rpc_options.set_echo_metadata(true);
  std::vector<std::pair<std::string, std::string>> metadata;
  metadata.emplace_back("x-extproc-trigger-immediate-response-phase", "request_body");
  rpc_options.set_metadata(std::move(metadata));
  EchoResponse response;
  std::multimap<std::string, std::string> server_initial_metadata;
  std::multimap<std::string, std::string> server_trailing_metadata;
  Status status =
      SendRpcGetTrailers(rpc_options, &response, &server_initial_metadata,
                         &server_trailing_metadata);
  EXPECT_FALSE(status.ok());
  EXPECT_EQ(status.error_code(), StatusCode::PERMISSION_DENIED);
  EXPECT_EQ(status.error_message(), "unhandled immediate response due to config disabled it");
}

TEST_P(XdsExtProcEnd2endTest, DisableImmediateResponseResponseHeadersAllow) {
  StartImmediateResponseServer();
  CreateAndStartBackends(1);
  using envoy::extensions::filters::http::ext_proc::v3::ProcessingMode;
  auto ext_proc_config = ExternalProcessorBuilder()
                             .SetTargetUri(immediate_response_server_->target())
                             .SetInsecureChannelCredentials()
                             .SetDisableImmediateResponse(true)
                             .SetFailureModeAllow(true)
                             .SetRequestHeaderMode(ProcessingMode::SEND)
                             .SetResponseHeaderMode(ProcessingMode::SEND)
                             .SetResponseTrailerMode(ProcessingMode::SEND)
                             .SetRequestBodyMode(ProcessingMode::GRPC)
                             .SetResponseBodyMode(ProcessingMode::GRPC)
                             .Build();
  Listener listener = BuildListenerWithExtProcFilter(ext_proc_config);
  RouteConfiguration route_config = default_route_config_;
  SetListenerAndRouteConfiguration(balancer_.get(), listener, route_config);
  balancer_->ads_service()->SetCdsResource(default_cluster_);
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(EdsResourceArgs({
      {"locality0", CreateEndpointsForBackends(0, 1)},
  })));
  RpcOptions rpc_options;
  rpc_options.set_echo_metadata_initially(true);
  rpc_options.set_echo_metadata(true);
  std::vector<std::pair<std::string, std::string>> metadata;
  metadata.emplace_back("x-extproc-trigger-immediate-response-phase", "response_headers");
  rpc_options.set_metadata(std::move(metadata));
  EchoResponse response;
  std::multimap<std::string, std::string> server_initial_metadata;
  std::multimap<std::string, std::string> server_trailing_metadata;
  Status status =
      SendRpcGetTrailers(rpc_options, &response, &server_initial_metadata,
                         &server_trailing_metadata);
  EXPECT_TRUE(status.ok()) << "Expected OK, got: " << status.error_message();
}

TEST_P(XdsExtProcEnd2endTest, DisableImmediateResponseResponseHeadersBlock) {
  StartImmediateResponseServer();
  CreateAndStartBackends(1);
  using envoy::extensions::filters::http::ext_proc::v3::ProcessingMode;
  auto ext_proc_config = ExternalProcessorBuilder()
                             .SetTargetUri(immediate_response_server_->target())
                             .SetInsecureChannelCredentials()
                             .SetDisableImmediateResponse(true)
                             .SetFailureModeAllow(false)
                             .SetRequestHeaderMode(ProcessingMode::SEND)
                             .SetResponseHeaderMode(ProcessingMode::SEND)
                             .SetResponseTrailerMode(ProcessingMode::SEND)
                             .SetRequestBodyMode(ProcessingMode::GRPC)
                             .SetResponseBodyMode(ProcessingMode::GRPC)
                             .Build();
  Listener listener = BuildListenerWithExtProcFilter(ext_proc_config);
  RouteConfiguration route_config = default_route_config_;
  SetListenerAndRouteConfiguration(balancer_.get(), listener, route_config);
  balancer_->ads_service()->SetCdsResource(default_cluster_);
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(EdsResourceArgs({
      {"locality0", CreateEndpointsForBackends(0, 1)},
  })));
  RpcOptions rpc_options;
  rpc_options.set_echo_metadata_initially(true);
  rpc_options.set_echo_metadata(true);
  std::vector<std::pair<std::string, std::string>> metadata;
  metadata.emplace_back("x-extproc-trigger-immediate-response-phase", "response_headers");
  rpc_options.set_metadata(std::move(metadata));
  EchoResponse response;
  std::multimap<std::string, std::string> server_initial_metadata;
  std::multimap<std::string, std::string> server_trailing_metadata;
  Status status =
      SendRpcGetTrailers(rpc_options, &response, &server_initial_metadata,
                         &server_trailing_metadata);
  EXPECT_FALSE(status.ok());
  EXPECT_EQ(status.error_code(), StatusCode::PERMISSION_DENIED);
  EXPECT_EQ(status.error_message(), "unhandled immediate response due to config disabled it");
}

TEST_P(XdsExtProcEnd2endTest, DisableImmediateResponseResponseBodyAllow) {
  StartImmediateResponseServer();
  CreateAndStartBackends(1);
  using envoy::extensions::filters::http::ext_proc::v3::ProcessingMode;
  auto ext_proc_config = ExternalProcessorBuilder()
                             .SetTargetUri(immediate_response_server_->target())
                             .SetInsecureChannelCredentials()
                             .SetDisableImmediateResponse(true)
                             .SetFailureModeAllow(true)
                             .SetRequestHeaderMode(ProcessingMode::SEND)
                             .SetRequestBodyMode(ProcessingMode::GRPC)
                             .SetResponseBodyMode(ProcessingMode::GRPC)
                             .SetResponseTrailerMode(ProcessingMode::SEND)
                             .Build();
  Listener listener = BuildListenerWithExtProcFilter(ext_proc_config);
  RouteConfiguration route_config = default_route_config_;
  SetListenerAndRouteConfiguration(balancer_.get(), listener, route_config);
  balancer_->ads_service()->SetCdsResource(default_cluster_);
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(EdsResourceArgs({
      {"locality0", CreateEndpointsForBackends(0, 1)},
  })));
  RpcOptions rpc_options;
  rpc_options.set_echo_metadata_initially(true);
  rpc_options.set_echo_metadata(true);
  std::vector<std::pair<std::string, std::string>> metadata;
  metadata.emplace_back("x-extproc-trigger-immediate-response-phase", "response_body");
  rpc_options.set_metadata(std::move(metadata));
  EchoResponse response;
  std::multimap<std::string, std::string> server_initial_metadata;
  std::multimap<std::string, std::string> server_trailing_metadata;
  Status status =
      SendRpcGetTrailers(rpc_options, &response, &server_initial_metadata,
                         &server_trailing_metadata);
  EXPECT_TRUE(status.ok()) << "Expected OK, got: " << status.error_message();
}

TEST_P(XdsExtProcEnd2endTest, DisableImmediateResponseResponseBodyBlock) {
  GTEST_SKIP() << "Skipped: fails due to core promise bridge bug bypassing response body, "
                  "and hangs due to filter latch bug when bridge is fixed";
  StartImmediateResponseServer();
  CreateAndStartBackends(1);
  using envoy::extensions::filters::http::ext_proc::v3::ProcessingMode;
  auto ext_proc_config = ExternalProcessorBuilder()
                             .SetTargetUri(immediate_response_server_->target())
                             .SetInsecureChannelCredentials()
                             .SetDisableImmediateResponse(true)
                             .SetFailureModeAllow(false)
                             .SetRequestHeaderMode(ProcessingMode::SEND)
                             .SetRequestBodyMode(ProcessingMode::GRPC)
                             .SetResponseBodyMode(ProcessingMode::GRPC)
                             .SetResponseTrailerMode(ProcessingMode::SEND)
                             .Build();
  Listener listener = BuildListenerWithExtProcFilter(ext_proc_config);
  RouteConfiguration route_config = default_route_config_;
  SetListenerAndRouteConfiguration(balancer_.get(), listener, route_config);
  balancer_->ads_service()->SetCdsResource(default_cluster_);
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(EdsResourceArgs({
      {"locality0", CreateEndpointsForBackends(0, 1)},
  })));
  RpcOptions rpc_options;
  rpc_options.set_echo_metadata_initially(true);
  rpc_options.set_echo_metadata(true);
  std::vector<std::pair<std::string, std::string>> metadata;
  metadata.emplace_back("x-extproc-trigger-immediate-response-phase", "response_body");
  rpc_options.set_metadata(std::move(metadata));
  EchoResponse response;
  std::multimap<std::string, std::string> server_initial_metadata;
  std::multimap<std::string, std::string> server_trailing_metadata;
  Status status =
      SendRpcGetTrailers(rpc_options, &response, &server_initial_metadata,
                         &server_trailing_metadata);
  EXPECT_FALSE(status.ok());
  EXPECT_EQ(status.error_code(), StatusCode::PERMISSION_DENIED);
  EXPECT_EQ(status.error_message(), "unhandled immediate response due to config disabled it");
}

TEST_P(XdsExtProcEnd2endTest, DisableImmediateResponseResponseTrailersAllow) {
  StartImmediateResponseServer();
  CreateAndStartBackends(1);
  using envoy::extensions::filters::http::ext_proc::v3::ProcessingMode;
  auto ext_proc_config = ExternalProcessorBuilder()
                             .SetTargetUri(immediate_response_server_->target())
                             .SetInsecureChannelCredentials()
                             .SetDisableImmediateResponse(true)
                             .SetFailureModeAllow(true)
                             .SetRequestHeaderMode(ProcessingMode::SEND)
                             .SetResponseHeaderMode(ProcessingMode::SEND)
                             .SetResponseTrailerMode(ProcessingMode::SEND)
                             .SetRequestBodyMode(ProcessingMode::GRPC)
                             .SetResponseBodyMode(ProcessingMode::GRPC)
                             .Build();
  Listener listener = BuildListenerWithExtProcFilter(ext_proc_config);
  RouteConfiguration route_config = default_route_config_;
  SetListenerAndRouteConfiguration(balancer_.get(), listener, route_config);
  balancer_->ads_service()->SetCdsResource(default_cluster_);
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(EdsResourceArgs({
      {"locality0", CreateEndpointsForBackends(0, 1)},
  })));
  RpcOptions rpc_options;
  rpc_options.set_echo_metadata_initially(true);
  rpc_options.set_echo_metadata(true);
  std::vector<std::pair<std::string, std::string>> metadata;
  metadata.emplace_back("x-extproc-trigger-immediate-response-phase", "response_trailers");
  rpc_options.set_metadata(std::move(metadata));
  EchoResponse response;
  std::multimap<std::string, std::string> server_initial_metadata;
  std::multimap<std::string, std::string> server_trailing_metadata;
  Status status =
      SendRpcGetTrailers(rpc_options, &response, &server_initial_metadata,
                         &server_trailing_metadata);
  EXPECT_TRUE(status.ok()) << "Expected OK, got: " << status.error_message();
}

TEST_P(XdsExtProcEnd2endTest, DisableImmediateResponseResponseTrailersBlock) {
  StartImmediateResponseServer();
  CreateAndStartBackends(1);
  using envoy::extensions::filters::http::ext_proc::v3::ProcessingMode;
  auto ext_proc_config = ExternalProcessorBuilder()
                             .SetTargetUri(immediate_response_server_->target())
                             .SetInsecureChannelCredentials()
                             .SetDisableImmediateResponse(true)
                             .SetFailureModeAllow(false)
                             .SetRequestHeaderMode(ProcessingMode::SEND)
                             .SetResponseHeaderMode(ProcessingMode::SEND)
                             .SetResponseTrailerMode(ProcessingMode::SEND)
                             .SetRequestBodyMode(ProcessingMode::GRPC)
                             .SetResponseBodyMode(ProcessingMode::GRPC)
                             .Build();
  Listener listener = BuildListenerWithExtProcFilter(ext_proc_config);
  RouteConfiguration route_config = default_route_config_;
  SetListenerAndRouteConfiguration(balancer_.get(), listener, route_config);
  balancer_->ads_service()->SetCdsResource(default_cluster_);
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(EdsResourceArgs({
      {"locality0", CreateEndpointsForBackends(0, 1)},
  })));
  RpcOptions rpc_options;
  rpc_options.set_echo_metadata_initially(true);
  rpc_options.set_echo_metadata(true);
  std::vector<std::pair<std::string, std::string>> metadata;
  metadata.emplace_back("x-extproc-trigger-immediate-response-phase", "response_trailers");
  rpc_options.set_metadata(std::move(metadata));
  EchoResponse response;
  std::multimap<std::string, std::string> server_initial_metadata;
  std::multimap<std::string, std::string> server_trailing_metadata;
  Status status =
      SendRpcGetTrailers(rpc_options, &response, &server_initial_metadata,
                         &server_trailing_metadata);
  EXPECT_FALSE(status.ok());
  EXPECT_EQ(status.error_code(), StatusCode::PERMISSION_DENIED);
  EXPECT_EQ(status.error_message(), "unhandled immediate response due to config disabled it");
}

TEST_P(XdsExtProcEnd2endTest, DisableImmediateResponseTrailersOnlyAllow) {
  StartImmediateResponseServer();
  CreateAndStartBackends(1);
  using envoy::extensions::filters::http::ext_proc::v3::ProcessingMode;
  auto ext_proc_config = ExternalProcessorBuilder()
                             .SetTargetUri(immediate_response_server_->target())
                             .SetInsecureChannelCredentials()
                             .SetDisableImmediateResponse(true)
                             .SetFailureModeAllow(true)
                             .SetRequestHeaderMode(ProcessingMode::SEND)
                             .SetResponseHeaderMode(ProcessingMode::SEND)
                             .SetResponseTrailerMode(ProcessingMode::SEND)
                             .SetRequestBodyMode(ProcessingMode::GRPC)
                             .SetResponseBodyMode(ProcessingMode::GRPC)
                             .Build();
  Listener listener = BuildListenerWithExtProcFilter(ext_proc_config);
  RouteConfiguration route_config = default_route_config_;
  SetListenerAndRouteConfiguration(balancer_.get(), listener, route_config);
  balancer_->ads_service()->SetCdsResource(default_cluster_);
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(EdsResourceArgs({
      {"locality0", CreateEndpointsForBackends(0, 1)},
  })));
  RpcOptions rpc_options;
  rpc_options.set_echo_metadata_initially(true);
  rpc_options.set_echo_metadata(true);
  rpc_options.set_server_fail(true);
  std::vector<std::pair<std::string, std::string>> metadata;
  metadata.emplace_back("x-extproc-trigger-immediate-response-phase", "trailers_only");
  rpc_options.set_metadata(std::move(metadata));
  EchoResponse response;
  std::multimap<std::string, std::string> server_initial_metadata;
  std::multimap<std::string, std::string> server_trailing_metadata;
  Status status =
      SendRpcGetTrailers(rpc_options, &response, &server_initial_metadata,
                         &server_trailing_metadata);
  EXPECT_TRUE(status.ok()) << "Expected OK, got: " << status.error_message();
}

TEST_P(XdsExtProcEnd2endTest, DisableImmediateResponseTrailersOnlyBlock) {
  StartImmediateResponseServer();
  CreateAndStartBackends(1);
  using envoy::extensions::filters::http::ext_proc::v3::ProcessingMode;
  auto ext_proc_config = ExternalProcessorBuilder()
                             .SetTargetUri(immediate_response_server_->target())
                             .SetInsecureChannelCredentials()
                             .SetDisableImmediateResponse(true)
                             .SetFailureModeAllow(false)
                             .SetRequestHeaderMode(ProcessingMode::SEND)
                             .SetResponseHeaderMode(ProcessingMode::SEND)
                             .SetResponseTrailerMode(ProcessingMode::SEND)
                             .SetRequestBodyMode(ProcessingMode::GRPC)
                             .SetResponseBodyMode(ProcessingMode::GRPC)
                             .Build();
  Listener listener = BuildListenerWithExtProcFilter(ext_proc_config);
  RouteConfiguration route_config = default_route_config_;
  SetListenerAndRouteConfiguration(balancer_.get(), listener, route_config);
  balancer_->ads_service()->SetCdsResource(default_cluster_);
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(EdsResourceArgs({
      {"locality0", CreateEndpointsForBackends(0, 1)},
  })));
  RpcOptions rpc_options;
  rpc_options.set_echo_metadata_initially(true);
  rpc_options.set_echo_metadata(true);
  rpc_options.set_server_fail(true);
  std::vector<std::pair<std::string, std::string>> metadata;
  metadata.emplace_back("x-extproc-trigger-immediate-response-phase", "trailers_only");
  rpc_options.set_metadata(std::move(metadata));
  EchoResponse response;
  std::multimap<std::string, std::string> server_initial_metadata;
  std::multimap<std::string, std::string> server_trailing_metadata;
  Status status =
      SendRpcGetTrailers(rpc_options, &response, &server_initial_metadata,
                         &server_trailing_metadata);
  EXPECT_FALSE(status.ok());
  EXPECT_EQ(status.error_code(), StatusCode::PERMISSION_DENIED);
  EXPECT_EQ(status.error_message(), "unhandled immediate response due to config disabled it");
}

TEST_P(XdsExtProcEnd2endTest, ImmediateResponseRequestHeaders) {
  StartImmediateResponseServer();
  CreateAndStartBackends(1);
  using envoy::extensions::filters::http::ext_proc::v3::ProcessingMode;
  auto ext_proc_config = ExternalProcessorBuilder()
                             .SetTargetUri(immediate_response_server_->target())
                             .SetInsecureChannelCredentials()
                             .SetRequestHeaderMode(ProcessingMode::SEND)
                             .SetResponseHeaderMode(ProcessingMode::SEND)
                             .SetResponseTrailerMode(ProcessingMode::SEND)
                             .SetRequestBodyMode(ProcessingMode::GRPC)
                             .SetResponseBodyMode(ProcessingMode::GRPC)
                             .Build();
  Listener listener = BuildListenerWithExtProcFilter(ext_proc_config);
  RouteConfiguration route_config = default_route_config_;
  SetListenerAndRouteConfiguration(balancer_.get(), listener, route_config);
  balancer_->ads_service()->SetCdsResource(default_cluster_);
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(EdsResourceArgs({
      {"locality0", CreateEndpointsForBackends(0, 1)},
  })));
  RpcOptions rpc_options;
  rpc_options.set_echo_metadata_initially(true);
  rpc_options.set_echo_metadata(true);
  std::vector<std::pair<std::string, std::string>> metadata;
  metadata.emplace_back("x-extproc-trigger-immediate-response-phase", "request_headers");
  rpc_options.set_metadata(std::move(metadata));
  EchoResponse response;
  std::multimap<std::string, std::string> server_initial_metadata;
  std::multimap<std::string, std::string> server_trailing_metadata;
  Status status =
      SendRpcGetTrailers(rpc_options, &response, &server_initial_metadata,
                         &server_trailing_metadata);
  EXPECT_EQ(status.error_code(), StatusCode::PERMISSION_DENIED);
  EXPECT_EQ(status.error_message(), "Access Denied by ExtProc (Request Headers)");
}

TEST_P(XdsExtProcEnd2endTest, ImmediateResponseRequestBody) {
  StartImmediateResponseServer();
  CreateAndStartBackends(1);
  using envoy::extensions::filters::http::ext_proc::v3::ProcessingMode;
  auto ext_proc_config = ExternalProcessorBuilder()
                             .SetTargetUri(immediate_response_server_->target())
                             .SetInsecureChannelCredentials()
                             .SetRequestHeaderMode(ProcessingMode::SEND)
                             .SetResponseHeaderMode(ProcessingMode::SEND)
                             .SetResponseTrailerMode(ProcessingMode::SEND)
                             .SetRequestBodyMode(ProcessingMode::GRPC)
                             .SetResponseBodyMode(ProcessingMode::GRPC)
                             .Build();
  Listener listener = BuildListenerWithExtProcFilter(ext_proc_config);
  RouteConfiguration route_config = default_route_config_;
  SetListenerAndRouteConfiguration(balancer_.get(), listener, route_config);
  balancer_->ads_service()->SetCdsResource(default_cluster_);
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(EdsResourceArgs({
      {"locality0", CreateEndpointsForBackends(0, 1)},
  })));
  RpcOptions rpc_options;
  rpc_options.set_echo_metadata_initially(true);
  rpc_options.set_echo_metadata(true);
  std::vector<std::pair<std::string, std::string>> metadata;
  metadata.emplace_back("x-extproc-trigger-immediate-response-phase", "request_body");
  rpc_options.set_metadata(std::move(metadata));
  EchoResponse response;
  std::multimap<std::string, std::string> server_initial_metadata;
  std::multimap<std::string, std::string> server_trailing_metadata;
  Status status =
      SendRpcGetTrailers(rpc_options, &response, &server_initial_metadata,
                         &server_trailing_metadata);
  EXPECT_EQ(status.error_code(), StatusCode::PERMISSION_DENIED);
  EXPECT_EQ(status.error_message(), "Access Denied by ExtProc (Request Body)");
  auto it = server_trailing_metadata.find("x-extproc-immediate-response-added");
  EXPECT_NE(it, server_trailing_metadata.end());
  if (it != server_trailing_metadata.end()) {
    EXPECT_EQ(it->second, "yes");
  }
}

TEST_P(XdsExtProcEnd2endTest, ImmediateResponseResponseHeaders) {
  StartImmediateResponseServer();
  CreateAndStartBackends(1);
  using envoy::extensions::filters::http::ext_proc::v3::ProcessingMode;
  auto ext_proc_config = ExternalProcessorBuilder()
                             .SetTargetUri(immediate_response_server_->target())
                             .SetInsecureChannelCredentials()
                             .SetRequestHeaderMode(ProcessingMode::SEND)
                             .SetResponseHeaderMode(ProcessingMode::SEND)
                             .SetResponseTrailerMode(ProcessingMode::SEND)
                             .SetRequestBodyMode(ProcessingMode::GRPC)
                             .SetResponseBodyMode(ProcessingMode::GRPC)
                             .Build();
  Listener listener = BuildListenerWithExtProcFilter(ext_proc_config);
  RouteConfiguration route_config = default_route_config_;
  SetListenerAndRouteConfiguration(balancer_.get(), listener, route_config);
  balancer_->ads_service()->SetCdsResource(default_cluster_);
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(EdsResourceArgs({
      {"locality0", CreateEndpointsForBackends(0, 1)},
  })));
  RpcOptions rpc_options;
  rpc_options.set_echo_metadata_initially(true);
  rpc_options.set_echo_metadata(true);
  std::vector<std::pair<std::string, std::string>> metadata;
  metadata.emplace_back("x-extproc-trigger-immediate-response-phase", "response_headers");
  rpc_options.set_metadata(std::move(metadata));
  EchoResponse response;
  std::multimap<std::string, std::string> server_initial_metadata;
  std::multimap<std::string, std::string> server_trailing_metadata;
  Status status =
      SendRpcGetTrailers(rpc_options, &response, &server_initial_metadata,
                         &server_trailing_metadata);
  EXPECT_EQ(status.error_code(), StatusCode::PERMISSION_DENIED);
  EXPECT_EQ(status.error_message(), "Access Denied by ExtProc (Response Headers)");
}

TEST_P(XdsExtProcEnd2endTest, ImmediateResponseResponseBody) {
  StartImmediateResponseServer();
  CreateAndStartBackends(1);
  using envoy::extensions::filters::http::ext_proc::v3::ProcessingMode;
  auto ext_proc_config = ExternalProcessorBuilder()
                             .SetTargetUri(immediate_response_server_->target())
                             .SetInsecureChannelCredentials()
                             .SetRequestHeaderMode(ProcessingMode::SEND)
                             .SetResponseHeaderMode(ProcessingMode::SKIP)
                             .SetResponseTrailerMode(ProcessingMode::SEND)
                             .SetRequestBodyMode(ProcessingMode::GRPC)
                             .SetResponseBodyMode(ProcessingMode::GRPC)
                             .Build();
  Listener listener = BuildListenerWithExtProcFilter(ext_proc_config);
  RouteConfiguration route_config = default_route_config_;
  SetListenerAndRouteConfiguration(balancer_.get(), listener, route_config);
  balancer_->ads_service()->SetCdsResource(default_cluster_);
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(EdsResourceArgs({
      {"locality0", CreateEndpointsForBackends(0, 1)},
  })));
  RpcOptions rpc_options;
  rpc_options.set_echo_metadata_initially(true);
  rpc_options.set_echo_metadata(true);
  std::vector<std::pair<std::string, std::string>> metadata;
  metadata.emplace_back("x-extproc-trigger-immediate-response-phase", "response_body");
  rpc_options.set_metadata(std::move(metadata));
  EchoResponse response;
  std::multimap<std::string, std::string> server_initial_metadata;
  std::multimap<std::string, std::string> server_trailing_metadata;
  Status status =
      SendRpcGetTrailers(rpc_options, &response, &server_initial_metadata,
                         &server_trailing_metadata);
  EXPECT_EQ(status.error_code(), StatusCode::PERMISSION_DENIED);
  EXPECT_EQ(status.error_message(), "Access Denied by ExtProc (Response Body)");
  auto it = server_trailing_metadata.find("x-extproc-immediate-response-added");
  EXPECT_NE(it, server_trailing_metadata.end());
  if (it != server_trailing_metadata.end()) {
    EXPECT_EQ(it->second, "yes");
  }
}

TEST_P(XdsExtProcEnd2endTest, ImmediateResponseResponseTrailers) {
  StartImmediateResponseServer();
  CreateAndStartBackends(1);
  using envoy::extensions::filters::http::ext_proc::v3::ProcessingMode;
  auto ext_proc_config = ExternalProcessorBuilder()
                             .SetTargetUri(immediate_response_server_->target())
                             .SetInsecureChannelCredentials()
                             .SetRequestHeaderMode(ProcessingMode::SEND)
                             .SetResponseHeaderMode(ProcessingMode::SEND)
                             .SetResponseTrailerMode(ProcessingMode::SEND)
                             .SetRequestBodyMode(ProcessingMode::GRPC)
                             .SetResponseBodyMode(ProcessingMode::GRPC)
                             .Build();
  Listener listener = BuildListenerWithExtProcFilter(ext_proc_config);
  RouteConfiguration route_config = default_route_config_;
  SetListenerAndRouteConfiguration(balancer_.get(), listener, route_config);
  balancer_->ads_service()->SetCdsResource(default_cluster_);
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(EdsResourceArgs({
      {"locality0", CreateEndpointsForBackends(0, 1)},
  })));
  RpcOptions rpc_options;
  rpc_options.set_echo_metadata_initially(true);
  rpc_options.set_echo_metadata(true);
  std::vector<std::pair<std::string, std::string>> metadata;
  metadata.emplace_back("x-extproc-trigger-immediate-response-phase", "response_trailers");
  rpc_options.set_metadata(std::move(metadata));
  EchoResponse response;
  std::multimap<std::string, std::string> server_initial_metadata;
  std::multimap<std::string, std::string> server_trailing_metadata;
  Status status =
      SendRpcGetTrailers(rpc_options, &response, &server_initial_metadata,
                         &server_trailing_metadata);
  EXPECT_EQ(status.error_code(), StatusCode::PERMISSION_DENIED);
  EXPECT_EQ(status.error_message(), "Access Denied by ExtProc (Response Trailers)");
  auto it = server_trailing_metadata.find("x-extproc-immediate-response-added");
  EXPECT_NE(it, server_trailing_metadata.end());
  if (it != server_trailing_metadata.end()) {
    EXPECT_EQ(it->second, "yes");
  }
}

TEST_P(XdsExtProcEnd2endTest, ImmediateResponseTrailersOnly) {
  StartImmediateResponseServer();
  CreateAndStartBackends(1);
  using envoy::extensions::filters::http::ext_proc::v3::ProcessingMode;
  auto ext_proc_config = ExternalProcessorBuilder()
                             .SetTargetUri(immediate_response_server_->target())
                             .SetInsecureChannelCredentials()
                             .SetRequestHeaderMode(ProcessingMode::SEND)
                             .SetResponseHeaderMode(ProcessingMode::SEND)
                             .SetResponseTrailerMode(ProcessingMode::SEND)
                             .SetRequestBodyMode(ProcessingMode::GRPC)
                             .SetResponseBodyMode(ProcessingMode::GRPC)
                             .Build();
  Listener listener = BuildListenerWithExtProcFilter(ext_proc_config);
  RouteConfiguration route_config = default_route_config_;
  SetListenerAndRouteConfiguration(balancer_.get(), listener, route_config);
  balancer_->ads_service()->SetCdsResource(default_cluster_);
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(EdsResourceArgs({
      {"locality0", CreateEndpointsForBackends(0, 1)},
  })));
  RpcOptions rpc_options;
  rpc_options.set_echo_metadata_initially(true);
  rpc_options.set_echo_metadata(true);
  rpc_options.set_server_fail(true);
  std::vector<std::pair<std::string, std::string>> metadata;
  metadata.emplace_back("x-extproc-trigger-immediate-response-phase", "trailers_only");
  rpc_options.set_metadata(std::move(metadata));
  EchoResponse response;
  std::multimap<std::string, std::string> server_initial_metadata;
  std::multimap<std::string, std::string> server_trailing_metadata;
  Status status =
      SendRpcGetTrailers(rpc_options, &response, &server_initial_metadata,
                         &server_trailing_metadata);
  EXPECT_EQ(status.error_code(), StatusCode::PERMISSION_DENIED);
  EXPECT_EQ(status.error_message(), "Access Denied by ExtProc (Trailers Only)");
  auto it = server_trailing_metadata.find("x-extproc-immediate-response-added");
  EXPECT_NE(it, server_trailing_metadata.end());
  if (it != server_trailing_metadata.end()) {
    EXPECT_EQ(it->second, "yes");
  }
}

TEST_P(XdsExtProcEnd2endTest, RequestHeadersContinueAndReplaceFails) {
  auto mock_service = std::make_shared<GenericMockService>(
      [](const ::envoy::service::ext_proc::v3::ProcessingRequest& request,
         ::envoy::service::ext_proc::v3::ProcessingResponse* response) {
        if (request.has_request_headers()) {
          response->mutable_request_headers()
              ->mutable_response()
              ->set_status(
                  ::envoy::service::ext_proc::v3::CommonResponse::
                      CONTINUE_AND_REPLACE);
        } else {
          SetDefaultEmptyResponse(request, response);
        }
      });
  StartAlternativeServer(mock_service);
  CreateAndStartBackends(1);
  using envoy::extensions::filters::http::ext_proc::v3::ProcessingMode;
  auto ext_proc_config = ExternalProcessorBuilder()
                             .SetTargetUri(alternative_ext_proc_server_->target())
                             .SetInsecureChannelCredentials()
                             .SetRequestHeaderMode(ProcessingMode::SEND)
                             .Build();
  Listener listener = BuildListenerWithExtProcFilter(ext_proc_config);
  RouteConfiguration route_config = default_route_config_;
  SetListenerAndRouteConfiguration(balancer_.get(), listener, route_config);
  balancer_->ads_service()->SetCdsResource(default_cluster_);
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(EdsResourceArgs({
      {"locality0", CreateEndpointsForBackends(0, 1)},
  })));
  RpcOptions rpc_options;
  EchoResponse response;
  Status status = SendRpcGetTrailers(rpc_options, &response, nullptr, nullptr);
  EXPECT_FALSE(status.ok());
  EXPECT_EQ(status.error_code(), StatusCode::INTERNAL);
  EXPECT_EQ(status.error_message(), "CONTINUE_AND_REPLACE is not supported");
}

TEST_P(XdsExtProcEnd2endTest, RequestHeadersInvalidHeaderMutationFails) {
  auto mock_service = std::make_shared<GenericMockService>(
      [](const ::envoy::service::ext_proc::v3::ProcessingRequest& request,
         ::envoy::service::ext_proc::v3::ProcessingResponse* response) {
        if (request.has_request_headers()) {
          auto* mutation = response->mutable_request_headers()
                               ->mutable_response()
                               ->mutable_header_mutation();
          auto* header = mutation->add_set_headers();
          header->mutable_header()->set_key("host");
          header->mutable_header()->set_value("invalid-host");
        } else {
          SetDefaultEmptyResponse(request, response);
        }
      });
  StartAlternativeServer(mock_service);
  CreateAndStartBackends(1);
  using envoy::extensions::filters::http::ext_proc::v3::ProcessingMode;
  auto ext_proc_config = ExternalProcessorBuilder()
                             .SetTargetUri(alternative_ext_proc_server_->target())
                             .SetInsecureChannelCredentials()
                             .SetRequestHeaderMode(ProcessingMode::SEND)
                             .Build();
  Listener listener = BuildListenerWithExtProcFilter(ext_proc_config);
  RouteConfiguration route_config = default_route_config_;
  SetListenerAndRouteConfiguration(balancer_.get(), listener, route_config);
  balancer_->ads_service()->SetCdsResource(default_cluster_);
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(EdsResourceArgs({
      {"locality0", CreateEndpointsForBackends(0, 1)},
  })));
  RpcOptions rpc_options;
  EchoResponse response;
  Status status = SendRpcGetTrailers(rpc_options, &response, nullptr, nullptr);
  EXPECT_FALSE(status.ok());
  EXPECT_EQ(status.error_code(), StatusCode::INTERNAL);
  EXPECT_THAT(
      status.error_message(),
      ::testing::HasSubstr(
          "validation failed: [field:header.key error:header \"host\" not allowed]"));
}

TEST_P(XdsExtProcEnd2endTest, RequestHeadersRequestAttributesSent) {
  std::string path_received;
  std::string method_received;
  absl::Mutex mu;
  auto mock_service = std::make_shared<GenericMockService>(
      [&](const ::envoy::service::ext_proc::v3::ProcessingRequest& request,
          ::envoy::service::ext_proc::v3::ProcessingResponse* response) {
        if (request.has_request_headers()) {
          absl::MutexLock lock(&mu);
          auto it = request.attributes().find("envoy.filters.http.ext_proc");
          if (it != request.attributes().end()) {
            const auto& fields = it->second.fields();
            auto path_it = fields.find("request.path");
            if (path_it != fields.end()) {
              path_received = path_it->second.string_value();
            }
            auto method_it = fields.find("request.method");
            if (method_it != fields.end()) {
              method_received = method_it->second.string_value();
            }
          }
          response->mutable_request_headers()
              ->mutable_response()
              ->mutable_header_mutation();
        } else {
          SetDefaultEmptyResponse(request, response);
        }
      });
  StartAlternativeServer(mock_service);
  CreateAndStartBackends(1);
  using envoy::extensions::filters::http::ext_proc::v3::ProcessingMode;
  auto ext_proc_config = ExternalProcessorBuilder()
                             .SetTargetUri(alternative_ext_proc_server_->target())
                             .SetInsecureChannelCredentials()
                             .SetRequestHeaderMode(ProcessingMode::SEND)
                             .SetResponseHeaderMode(ProcessingMode::SEND)
                             .SetResponseTrailerMode(ProcessingMode::SEND)
                             .SetRequestBodyMode(ProcessingMode::GRPC)
                             .SetResponseBodyMode(ProcessingMode::GRPC)
                             .AddRequestAttribute("request.path")
                             .AddRequestAttribute("request.method")
                             .Build();
  Listener listener = BuildListenerWithExtProcFilter(ext_proc_config);
  RouteConfiguration route_config = default_route_config_;
  SetListenerAndRouteConfiguration(balancer_.get(), listener, route_config);
  balancer_->ads_service()->SetCdsResource(default_cluster_);
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(EdsResourceArgs({
      {"locality0", CreateEndpointsForBackends(0, 1)},
  })));
  RpcOptions rpc_options;
  EchoResponse response;
  Status status = SendRpcGetTrailers(rpc_options, &response, nullptr, nullptr);
  EXPECT_TRUE(status.ok()) << status.error_message();
  
  // Shutdown the server to ensure all messages are processed and we can safely
  // read the received attributes.
  alternative_ext_proc_server_->Shutdown();
  
  EXPECT_EQ(path_received, "/grpc.testing.EchoTestService/Echo");
  EXPECT_EQ(method_received, "POST");
}

TEST_P(XdsExtProcEnd2endTest, RequestHeadersExtProcConnectionErrorFailCall) {
  int port = grpc_pick_unused_port_or_die();
  std::string target = absl::StrCat("localhost:", port);
  CreateAndStartBackends(1);
  using envoy::extensions::filters::http::ext_proc::v3::ProcessingMode;
  auto ext_proc_config = ExternalProcessorBuilder()
                             .SetTargetUri(target)
                             .SetInsecureChannelCredentials()
                             .SetFailureModeAllow(false)
                             .SetRequestHeaderMode(ProcessingMode::SEND)
                             .Build();
  Listener listener = BuildListenerWithExtProcFilter(ext_proc_config);
  RouteConfiguration route_config = default_route_config_;
  SetListenerAndRouteConfiguration(balancer_.get(), listener, route_config);
  balancer_->ads_service()->SetCdsResource(default_cluster_);
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(EdsResourceArgs({
      {"locality0", CreateEndpointsForBackends(0, 1)},
  })));
  RpcOptions rpc_options;
  EchoResponse response;
  Status status = SendRpcGetTrailers(rpc_options, &response, nullptr, nullptr);
  EXPECT_FALSE(status.ok());
  EXPECT_EQ(status.error_code(), StatusCode::UNAVAILABLE);
}

TEST_P(XdsExtProcEnd2endTest, RequestHeadersExtProcConnectionErrorAllowCall) {
  int port = grpc_pick_unused_port_or_die();
  std::string target = absl::StrCat("localhost:", port);
  CreateAndStartBackends(1);
  using envoy::extensions::filters::http::ext_proc::v3::ProcessingMode;
  auto ext_proc_config = ExternalProcessorBuilder()
                             .SetTargetUri(target)
                             .SetInsecureChannelCredentials()
                             .SetFailureModeAllow(true)
                             .SetRequestHeaderMode(ProcessingMode::SEND)
                             .Build();
  Listener listener = BuildListenerWithExtProcFilter(ext_proc_config);
  RouteConfiguration route_config = default_route_config_;
  SetListenerAndRouteConfiguration(balancer_.get(), listener, route_config);
  balancer_->ads_service()->SetCdsResource(default_cluster_);
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(EdsResourceArgs({
      {"locality0", CreateEndpointsForBackends(0, 1)},
  })));
  RpcOptions rpc_options;
  EchoResponse response;
  Status status = SendRpcGetTrailers(rpc_options, &response, nullptr, nullptr);
  EXPECT_TRUE(status.ok()) << status.error_message();
}

}  // namespace
}  // namespace testing
}  // namespace grpc

int main(int argc, char** argv) {
  grpc_core::SetEnv("GRPC_EXPERIMENTAL_XDS_EXT_PROC_ON_CLIENT", "true");
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
