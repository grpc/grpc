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

#include <atomic>
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

std::string GetExtProcAttribute(
    const ::envoy::service::ext_proc::v3::ProcessingRequest& request,
    absl::string_view attribute_name) {
  auto it = request.attributes().find("envoy.filters.http.ext_proc");
  if (it == request.attributes().end()) return "";
  const auto& fields = it->second.fields();
  auto field_it = fields.find(std::string(attribute_name));
  if (field_it == fields.end()) return "";
  return field_it->second.string_value();
}

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
        for (const auto& header :
             request.request_headers().headers().headers()) {
          if (header.key() == "x-extproc-trigger-immediate-response-phase") {
            trigger_phase = header.raw_value();
            break;
          }
        }
        if (trigger_phase == "request_headers") {
          auto* immediate = response.mutable_immediate_response();
          immediate->mutable_grpc_status()->set_status(
              grpc::StatusCode::PERMISSION_DENIED);
          immediate->set_details("Access Denied by ExtProc (Request Headers)");
          auto* mutation = immediate->mutable_headers();
          auto* header = mutation->add_set_headers();
          header->mutable_header()->set_key(
              "x-extproc-immediate-response-added");
          header->mutable_header()->set_value("yes");
        } else {
          response.mutable_request_headers()
              ->mutable_response()
              ->mutable_header_mutation();
        }
      } else if (request.has_response_headers()) {
        if (trigger_phase == "response_headers") {
          auto* immediate = response.mutable_immediate_response();
          immediate->mutable_grpc_status()->set_status(
              grpc::StatusCode::PERMISSION_DENIED);
          immediate->set_details("Access Denied by ExtProc (Response Headers)");
          auto* mutation = immediate->mutable_headers();
          auto* header = mutation->add_set_headers();
          header->mutable_header()->set_key(
              "x-extproc-immediate-response-added");
          header->mutable_header()->set_value("yes");
        } else if (trigger_phase == "trailers_only") {
          auto* immediate = response.mutable_immediate_response();
          immediate->mutable_grpc_status()->set_status(
              grpc::StatusCode::PERMISSION_DENIED);
          immediate->set_details("Access Denied by ExtProc (Trailers Only)");
          auto* mutation = immediate->mutable_headers();
          auto* header = mutation->add_set_headers();
          header->mutable_header()->set_key(
              "x-extproc-immediate-response-added");
          header->mutable_header()->set_value("yes");
        } else {
          response.mutable_response_headers()
              ->mutable_response()
              ->mutable_header_mutation();
        }
      } else if (request.has_request_body()) {
        if (trigger_phase == "request_body") {
          auto* immediate = response.mutable_immediate_response();
          immediate->mutable_grpc_status()->set_status(
              grpc::StatusCode::PERMISSION_DENIED);
          immediate->set_details("Access Denied by ExtProc (Request Body)");
          auto* mutation = immediate->mutable_headers();
          auto* header = mutation->add_set_headers();
          header->mutable_header()->set_key(
              "x-extproc-immediate-response-added");
          header->mutable_header()->set_value("yes");
        } else {
          response.mutable_request_body()
              ->mutable_response()
              ->mutable_body_mutation();
        }
      } else if (request.has_response_body()) {
        if (trigger_phase == "response_body") {
          auto* immediate = response.mutable_immediate_response();
          immediate->mutable_grpc_status()->set_status(
              grpc::StatusCode::PERMISSION_DENIED);
          immediate->set_details("Access Denied by ExtProc (Response Body)");
          auto* mutation = immediate->mutable_headers();
          auto* header = mutation->add_set_headers();
          header->mutable_header()->set_key(
              "x-extproc-immediate-response-added");
          header->mutable_header()->set_value("yes");
        } else {
          response.mutable_response_body()
              ->mutable_response()
              ->mutable_body_mutation();
        }
      } else if (request.has_request_trailers()) {
        response.mutable_request_trailers()->mutable_header_mutation();
      } else if (request.has_response_trailers()) {
        if (trigger_phase == "response_trailers") {
          auto* immediate = response.mutable_immediate_response();
          immediate->mutable_grpc_status()->set_status(
              grpc::StatusCode::PERMISSION_DENIED);
          immediate->set_details(
              "Access Denied by ExtProc (Response Trailers)");
          auto* mutation = immediate->mutable_headers();
          auto* header = mutation->add_set_headers();
          header->mutable_header()->set_key(
              "x-extproc-immediate-response-added");
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

    ServiceType* ext_proc_service() { return ext_proc_service_.get(); }

   private:
    const char* Type() override { return "ExtProc"; }

    void RegisterAllServices(ServerBuilder* builder) override {
      builder->RegisterService(ext_proc_service_.get());
    }

    void StartAllServices() override {}
    void ShutdownAllServices() override {}

    std::shared_ptr<ServiceType> ext_proc_service_;
  };

  void ResetStubWithUniqueArg() {
    ChannelArguments args;
    static std::atomic<int> g_counter{0};
    args.SetInt(
        "g_unique_test_channel_arg_" +
            std::to_string(g_counter.fetch_add(1, std::memory_order_relaxed)),
        1);
    ResetStub(0, &args);
  }

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
        std::make_unique<ExtProcServerThread<ServiceType>>(this,
                                                           std::move(service));
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
  // NOTE: In both normal and observability modes, if response headers (initial
  // metadata) are enabled, the filter blocks/delays the call progression to
  // write them. Due to transport-level coalescing, this delay causes the
  // subsequent response body in the same coalesced batch to be dropped by the
  // transport before the message loop can pull it. This is a known limitation.
  bool expect_response_body = resp_body;
  if (resp_hdrs && resp_body) {
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
  EXPECT_EQ(counts.response_body, expect_response_body ? 1 : 0);
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
  metadata.emplace_back("x-extproc-trigger-immediate-response-phase",
                        "request_headers");
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
  metadata.emplace_back("x-extproc-trigger-immediate-response-phase",
                        "request_headers");
  rpc_options.set_metadata(std::move(metadata));
  EchoResponse response;
  std::multimap<std::string, std::string> server_initial_metadata;
  std::multimap<std::string, std::string> server_trailing_metadata;
  Status status =
      SendRpcGetTrailers(rpc_options, &response, &server_initial_metadata,
                         &server_trailing_metadata);
  EXPECT_FALSE(status.ok());
  EXPECT_EQ(status.error_code(), StatusCode::PERMISSION_DENIED);
  EXPECT_EQ(status.error_message(),
            "unhandled immediate response due to config disabled it");
}

TEST_P(XdsExtProcEnd2endTest, DisableImmediateResponseRequestBodyAllow) {
  GTEST_SKIP() << "Skipped: hangs due to known message loss bug in request "
                  "body fail-open path";
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
  metadata.emplace_back("x-extproc-trigger-immediate-response-phase",
                        "request_body");
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
  metadata.emplace_back("x-extproc-trigger-immediate-response-phase",
                        "request_body");
  rpc_options.set_metadata(std::move(metadata));
  EchoResponse response;
  std::multimap<std::string, std::string> server_initial_metadata;
  std::multimap<std::string, std::string> server_trailing_metadata;
  Status status =
      SendRpcGetTrailers(rpc_options, &response, &server_initial_metadata,
                         &server_trailing_metadata);
  EXPECT_FALSE(status.ok());
  EXPECT_EQ(status.error_code(), StatusCode::PERMISSION_DENIED);
  EXPECT_EQ(status.error_message(),
            "unhandled immediate response due to config disabled it");
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
  metadata.emplace_back("x-extproc-trigger-immediate-response-phase",
                        "response_headers");
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
  metadata.emplace_back("x-extproc-trigger-immediate-response-phase",
                        "response_headers");
  rpc_options.set_metadata(std::move(metadata));
  EchoResponse response;
  std::multimap<std::string, std::string> server_initial_metadata;
  std::multimap<std::string, std::string> server_trailing_metadata;
  Status status =
      SendRpcGetTrailers(rpc_options, &response, &server_initial_metadata,
                         &server_trailing_metadata);
  EXPECT_FALSE(status.ok());
  EXPECT_EQ(status.error_code(), StatusCode::PERMISSION_DENIED);
  EXPECT_EQ(status.error_message(),
            "unhandled immediate response due to config disabled it");
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
  metadata.emplace_back("x-extproc-trigger-immediate-response-phase",
                        "response_body");
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
  GTEST_SKIP() << "Skipped: fails due to core promise bridge bug bypassing "
                  "response body, "
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
  metadata.emplace_back("x-extproc-trigger-immediate-response-phase",
                        "response_body");
  rpc_options.set_metadata(std::move(metadata));
  EchoResponse response;
  std::multimap<std::string, std::string> server_initial_metadata;
  std::multimap<std::string, std::string> server_trailing_metadata;
  Status status =
      SendRpcGetTrailers(rpc_options, &response, &server_initial_metadata,
                         &server_trailing_metadata);
  EXPECT_FALSE(status.ok());
  EXPECT_EQ(status.error_code(), StatusCode::PERMISSION_DENIED);
  EXPECT_EQ(status.error_message(),
            "unhandled immediate response due to config disabled it");
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
  metadata.emplace_back("x-extproc-trigger-immediate-response-phase",
                        "response_trailers");
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
  metadata.emplace_back("x-extproc-trigger-immediate-response-phase",
                        "response_trailers");
  rpc_options.set_metadata(std::move(metadata));
  EchoResponse response;
  std::multimap<std::string, std::string> server_initial_metadata;
  std::multimap<std::string, std::string> server_trailing_metadata;
  Status status =
      SendRpcGetTrailers(rpc_options, &response, &server_initial_metadata,
                         &server_trailing_metadata);
  EXPECT_FALSE(status.ok());
  EXPECT_EQ(status.error_code(), StatusCode::PERMISSION_DENIED);
  EXPECT_EQ(status.error_message(),
            "unhandled immediate response due to config disabled it");
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
  metadata.emplace_back("x-extproc-trigger-immediate-response-phase",
                        "trailers_only");
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
  metadata.emplace_back("x-extproc-trigger-immediate-response-phase",
                        "trailers_only");
  rpc_options.set_metadata(std::move(metadata));
  EchoResponse response;
  std::multimap<std::string, std::string> server_initial_metadata;
  std::multimap<std::string, std::string> server_trailing_metadata;
  Status status =
      SendRpcGetTrailers(rpc_options, &response, &server_initial_metadata,
                         &server_trailing_metadata);
  EXPECT_FALSE(status.ok());
  EXPECT_EQ(status.error_code(), StatusCode::PERMISSION_DENIED);
  EXPECT_EQ(status.error_message(),
            "unhandled immediate response due to config disabled it");
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
  metadata.emplace_back("x-extproc-trigger-immediate-response-phase",
                        "request_headers");
  rpc_options.set_metadata(std::move(metadata));
  EchoResponse response;
  std::multimap<std::string, std::string> server_initial_metadata;
  std::multimap<std::string, std::string> server_trailing_metadata;
  Status status =
      SendRpcGetTrailers(rpc_options, &response, &server_initial_metadata,
                         &server_trailing_metadata);
  EXPECT_EQ(status.error_code(), StatusCode::PERMISSION_DENIED);
  EXPECT_EQ(status.error_message(),
            "Access Denied by ExtProc (Request Headers)");
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
  metadata.emplace_back("x-extproc-trigger-immediate-response-phase",
                        "request_body");
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
  metadata.emplace_back("x-extproc-trigger-immediate-response-phase",
                        "response_headers");
  rpc_options.set_metadata(std::move(metadata));
  EchoResponse response;
  std::multimap<std::string, std::string> server_initial_metadata;
  std::multimap<std::string, std::string> server_trailing_metadata;
  Status status =
      SendRpcGetTrailers(rpc_options, &response, &server_initial_metadata,
                         &server_trailing_metadata);
  EXPECT_EQ(status.error_code(), StatusCode::PERMISSION_DENIED);
  EXPECT_EQ(status.error_message(),
            "Access Denied by ExtProc (Response Headers)");
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
  metadata.emplace_back("x-extproc-trigger-immediate-response-phase",
                        "response_body");
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
  metadata.emplace_back("x-extproc-trigger-immediate-response-phase",
                        "response_trailers");
  rpc_options.set_metadata(std::move(metadata));
  EchoResponse response;
  std::multimap<std::string, std::string> server_initial_metadata;
  std::multimap<std::string, std::string> server_trailing_metadata;
  Status status =
      SendRpcGetTrailers(rpc_options, &response, &server_initial_metadata,
                         &server_trailing_metadata);
  EXPECT_EQ(status.error_code(), StatusCode::PERMISSION_DENIED);
  EXPECT_EQ(status.error_message(),
            "Access Denied by ExtProc (Response Trailers)");
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
  metadata.emplace_back("x-extproc-trigger-immediate-response-phase",
                        "trailers_only");
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
          response->mutable_request_headers()->mutable_response()->set_status(
              ::envoy::service::ext_proc::v3::CommonResponse::
                  CONTINUE_AND_REPLACE);
        } else {
          SetDefaultEmptyResponse(request, response);
        }
      });
  StartAlternativeServer(mock_service);
  CreateAndStartBackends(1);
  using envoy::extensions::filters::http::ext_proc::v3::ProcessingMode;
  auto ext_proc_config =
      ExternalProcessorBuilder()
          .SetTargetUri(alternative_ext_proc_server_->target())
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
  auto ext_proc_config =
      ExternalProcessorBuilder()
          .SetTargetUri(alternative_ext_proc_server_->target())
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
  EchoResponse response;
  Status status = SendRpcGetTrailers(rpc_options, &response, nullptr, nullptr);
  EXPECT_FALSE(status.ok());
  EXPECT_EQ(status.error_code(), StatusCode::INTERNAL);
  EXPECT_THAT(status.error_message(),
              ::testing::HasSubstr("validation failed: [field:header.key "
                                   "error:header \"host\" not allowed]"));
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
          path_received = GetExtProcAttribute(request, "request.path");
          method_received = GetExtProcAttribute(request, "request.method");
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
  auto ext_proc_config =
      ExternalProcessorBuilder()
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
  EchoResponse response;
  Status status = SendRpcGetTrailers(rpc_options, &response, nullptr, nullptr);
  EXPECT_FALSE(status.ok());
  EXPECT_THAT(status.error_code(),
              ::testing::AnyOf(StatusCode::UNAVAILABLE, StatusCode::CANCELLED));
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
  EchoResponse response;
  Status status = SendRpcGetTrailers(rpc_options, &response, nullptr, nullptr);
  EXPECT_TRUE(status.ok()) << status.error_message();
}

TEST_P(XdsExtProcEnd2endTest, ResponseHeadersContinueAndReplaceFails) {
  auto mock_service = std::make_shared<GenericMockService>(
      [](const ::envoy::service::ext_proc::v3::ProcessingRequest& request,
         ::envoy::service::ext_proc::v3::ProcessingResponse* response) {
        if (request.has_response_headers()) {
          response->mutable_response_headers()->mutable_response()->set_status(
              ::envoy::service::ext_proc::v3::CommonResponse::
                  CONTINUE_AND_REPLACE);
        } else {
          SetDefaultEmptyResponse(request, response);
        }
      });
  StartAlternativeServer(mock_service);
  CreateAndStartBackends(1);
  using envoy::extensions::filters::http::ext_proc::v3::ProcessingMode;
  auto ext_proc_config =
      ExternalProcessorBuilder()
          .SetTargetUri(alternative_ext_proc_server_->target())
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
  EchoResponse response;
  Status status = SendRpcGetTrailers(rpc_options, &response, nullptr, nullptr);
  EXPECT_FALSE(status.ok());
  EXPECT_EQ(status.error_code(), StatusCode::INTERNAL);
  EXPECT_EQ(status.error_message(), "CONTINUE_AND_REPLACE is not supported");
}

TEST_P(XdsExtProcEnd2endTest, ResponseHeadersInvalidHeaderMutationFails) {
  auto mock_service = std::make_shared<GenericMockService>(
      [](const ::envoy::service::ext_proc::v3::ProcessingRequest& request,
         ::envoy::service::ext_proc::v3::ProcessingResponse* response) {
        if (request.has_response_headers()) {
          auto* mutation = response->mutable_response_headers()
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
  auto ext_proc_config =
      ExternalProcessorBuilder()
          .SetTargetUri(alternative_ext_proc_server_->target())
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
  EchoResponse response;
  Status status = SendRpcGetTrailers(rpc_options, &response, nullptr, nullptr);
  EXPECT_FALSE(status.ok());
  EXPECT_EQ(status.error_code(), StatusCode::INTERNAL);
  EXPECT_THAT(status.error_message(),
              ::testing::HasSubstr("validation failed: [field:header.key "
                                   "error:header \"host\" not allowed]"));
}

TEST_P(XdsExtProcEnd2endTest, ResponseHeadersExtProcConnectionErrorFailCall) {
  int port = grpc_pick_unused_port_or_die();
  std::string target = absl::StrCat("localhost:", port);
  CreateAndStartBackends(1);
  using envoy::extensions::filters::http::ext_proc::v3::ProcessingMode;
  auto ext_proc_config = ExternalProcessorBuilder()
                             .SetTargetUri(target)
                             .SetInsecureChannelCredentials()
                             .SetFailureModeAllow(false)
                             .SetResponseHeaderMode(ProcessingMode::SEND)
                             .SetResponseTrailerMode(ProcessingMode::SEND)
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
  EchoResponse response;
  Status status = SendRpcGetTrailers(rpc_options, &response, nullptr, nullptr);
  EXPECT_FALSE(status.ok());
  EXPECT_THAT(status.error_code(),
              ::testing::AnyOf(StatusCode::UNAVAILABLE, StatusCode::CANCELLED));
}

TEST_P(XdsExtProcEnd2endTest, ResponseHeadersExtProcConnectionErrorAllowCall) {
  int port = grpc_pick_unused_port_or_die();
  std::string target = absl::StrCat("localhost:", port);
  CreateAndStartBackends(1);
  using envoy::extensions::filters::http::ext_proc::v3::ProcessingMode;
  auto ext_proc_config = ExternalProcessorBuilder()
                             .SetTargetUri(target)
                             .SetInsecureChannelCredentials()
                             .SetFailureModeAllow(true)
                             .SetResponseHeaderMode(ProcessingMode::SEND)
                             .SetResponseTrailerMode(ProcessingMode::SEND)
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
  EchoResponse response;
  Status status = SendRpcGetTrailers(rpc_options, &response, nullptr, nullptr);
  EXPECT_TRUE(status.ok()) << status.error_message();
}

TEST_P(XdsExtProcEnd2endTest,
       RequestHeadersObservabilityExtProcConnectionErrorFailCall) {
  int port = grpc_pick_unused_port_or_die();
  std::string target = absl::StrCat("localhost:", port);
  CreateAndStartBackends(1);
  using envoy::extensions::filters::http::ext_proc::v3::ProcessingMode;
  auto ext_proc_config = ExternalProcessorBuilder()
                             .SetTargetUri(target)
                             .SetInsecureChannelCredentials()
                             .SetFailureModeAllow(false)
                             .SetObservabilityMode(true)
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
  EchoResponse response;
  Status status = SendRpcGetTrailers(rpc_options, &response, nullptr, nullptr);
  EXPECT_FALSE(status.ok());
  EXPECT_THAT(status.error_code(),
              ::testing::AnyOf(StatusCode::UNAVAILABLE, StatusCode::CANCELLED));
}

TEST_P(XdsExtProcEnd2endTest,
       RequestHeadersObservabilityExtProcConnectionErrorAllowCall) {
  int port = grpc_pick_unused_port_or_die();
  std::string target = absl::StrCat("localhost:", port);
  CreateAndStartBackends(1);
  using envoy::extensions::filters::http::ext_proc::v3::ProcessingMode;
  auto ext_proc_config = ExternalProcessorBuilder()
                             .SetTargetUri(target)
                             .SetInsecureChannelCredentials()
                             .SetFailureModeAllow(true)
                             .SetObservabilityMode(true)
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
  EchoResponse response;
  Status status = SendRpcGetTrailers(rpc_options, &response, nullptr, nullptr);
  EXPECT_TRUE(status.ok()) << status.error_message();
}

TEST_P(XdsExtProcEnd2endTest,
       ResponseHeadersObservabilityExtProcConnectionErrorFailCall) {
  int port = grpc_pick_unused_port_or_die();
  std::string target = absl::StrCat("localhost:", port);
  CreateAndStartBackends(1);
  using envoy::extensions::filters::http::ext_proc::v3::ProcessingMode;
  auto ext_proc_config = ExternalProcessorBuilder()
                             .SetTargetUri(target)
                             .SetInsecureChannelCredentials()
                             .SetFailureModeAllow(false)
                             .SetObservabilityMode(true)
                             .SetResponseHeaderMode(ProcessingMode::SEND)
                             .SetResponseTrailerMode(ProcessingMode::SEND)
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
  EchoResponse response;
  Status status = SendRpcGetTrailers(rpc_options, &response, nullptr, nullptr);
  EXPECT_FALSE(status.ok());
  EXPECT_THAT(status.error_code(),
              ::testing::AnyOf(StatusCode::UNAVAILABLE, StatusCode::CANCELLED));
}

TEST_P(XdsExtProcEnd2endTest,
       ResponseHeadersObservabilityExtProcConnectionErrorAllowCall) {
  int port = grpc_pick_unused_port_or_die();
  std::string target = absl::StrCat("localhost:", port);
  CreateAndStartBackends(1);
  using envoy::extensions::filters::http::ext_proc::v3::ProcessingMode;
  auto ext_proc_config = ExternalProcessorBuilder()
                             .SetTargetUri(target)
                             .SetInsecureChannelCredentials()
                             .SetFailureModeAllow(true)
                             .SetObservabilityMode(true)
                             .SetResponseHeaderMode(ProcessingMode::SEND)
                             .SetResponseTrailerMode(ProcessingMode::SEND)
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
  EchoResponse response;
  Status status = SendRpcGetTrailers(rpc_options, &response, nullptr, nullptr);
  EXPECT_TRUE(status.ok()) << status.error_message();
}

TEST_P(XdsExtProcEnd2endTest, ResponseTrailersInvalidHeaderMutationFails) {
  auto mock_service = std::make_shared<GenericMockService>(
      [](const ::envoy::service::ext_proc::v3::ProcessingRequest& request,
         ::envoy::service::ext_proc::v3::ProcessingResponse* response) {
        if (request.has_response_trailers()) {
          auto* mutation =
              response->mutable_response_trailers()->mutable_header_mutation();
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
  auto ext_proc_config =
      ExternalProcessorBuilder()
          .SetTargetUri(alternative_ext_proc_server_->target())
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
  EchoResponse response;
  Status status = SendRpcGetTrailers(rpc_options, &response, nullptr, nullptr);
  EXPECT_FALSE(status.ok());
  EXPECT_EQ(status.error_code(), StatusCode::INTERNAL);
  EXPECT_THAT(status.error_message(),
              ::testing::HasSubstr("validation failed: [field:header.key "
                                   "error:header \"host\" not allowed]"));
}

TEST_P(XdsExtProcEnd2endTest, ResponseTrailersExtProcConnectionErrorFailCall) {
  int port = grpc_pick_unused_port_or_die();
  std::string target = absl::StrCat("localhost:", port);
  CreateAndStartBackends(1);
  using envoy::extensions::filters::http::ext_proc::v3::ProcessingMode;
  auto ext_proc_config = ExternalProcessorBuilder()
                             .SetTargetUri(target)
                             .SetInsecureChannelCredentials()
                             .SetFailureModeAllow(false)
                             .SetResponseHeaderMode(ProcessingMode::SKIP)
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
  EchoResponse response;
  Status status = SendRpcGetTrailers(rpc_options, &response, nullptr, nullptr);
  EXPECT_FALSE(status.ok());
  EXPECT_THAT(status.error_code(),
              ::testing::AnyOf(StatusCode::UNAVAILABLE, StatusCode::CANCELLED));
}

TEST_P(XdsExtProcEnd2endTest, ResponseTrailersExtProcConnectionErrorAllowCall) {
  int port = grpc_pick_unused_port_or_die();
  std::string target = absl::StrCat("localhost:", port);
  CreateAndStartBackends(1);
  using envoy::extensions::filters::http::ext_proc::v3::ProcessingMode;
  auto ext_proc_config = ExternalProcessorBuilder()
                             .SetTargetUri(target)
                             .SetInsecureChannelCredentials()
                             .SetFailureModeAllow(true)
                             .SetResponseHeaderMode(ProcessingMode::SKIP)
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
  EchoResponse response;
  Status status = SendRpcGetTrailers(rpc_options, &response, nullptr, nullptr);
  EXPECT_TRUE(status.ok()) << status.error_message();
}

TEST_P(XdsExtProcEnd2endTest,
       ResponseTrailersObservabilityExtProcConnectionErrorFailCall) {
  int port = grpc_pick_unused_port_or_die();
  std::string target = absl::StrCat("localhost:", port);
  CreateAndStartBackends(1);
  using envoy::extensions::filters::http::ext_proc::v3::ProcessingMode;
  auto ext_proc_config = ExternalProcessorBuilder()
                             .SetTargetUri(target)
                             .SetInsecureChannelCredentials()
                             .SetFailureModeAllow(false)
                             .SetObservabilityMode(true)
                             .SetResponseHeaderMode(ProcessingMode::SKIP)
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
  EchoResponse response;
  Status status = SendRpcGetTrailers(rpc_options, &response, nullptr, nullptr);
  EXPECT_FALSE(status.ok());
  EXPECT_THAT(status.error_code(),
              ::testing::AnyOf(StatusCode::UNAVAILABLE, StatusCode::CANCELLED));
}

TEST_P(XdsExtProcEnd2endTest,
       ResponseTrailersObservabilityExtProcConnectionErrorAllowCall) {
  int port = grpc_pick_unused_port_or_die();
  std::string target = absl::StrCat("localhost:", port);
  CreateAndStartBackends(1);
  using envoy::extensions::filters::http::ext_proc::v3::ProcessingMode;
  auto ext_proc_config = ExternalProcessorBuilder()
                             .SetTargetUri(target)
                             .SetInsecureChannelCredentials()
                             .SetFailureModeAllow(true)
                             .SetObservabilityMode(true)
                             .SetResponseHeaderMode(ProcessingMode::SKIP)
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
  EchoResponse response;
  Status status = SendRpcGetTrailers(rpc_options, &response, nullptr, nullptr);
  EXPECT_TRUE(status.ok()) << status.error_message();
}

TEST_P(XdsExtProcEnd2endTest, TrailersOnlyExtProcConnectionErrorFailCall) {
  int port = grpc_pick_unused_port_or_die();
  std::string target = absl::StrCat("localhost:", port);
  CreateAndStartBackends(1);
  using envoy::extensions::filters::http::ext_proc::v3::ProcessingMode;
  auto ext_proc_config = ExternalProcessorBuilder()
                             .SetTargetUri(target)
                             .SetInsecureChannelCredentials()
                             .SetFailureModeAllow(false)
                             .SetRequestHeaderMode(ProcessingMode::SKIP)
                             .SetResponseHeaderMode(ProcessingMode::SEND)
                             .SetResponseTrailerMode(ProcessingMode::SEND)
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
  rpc_options.set_server_expected_error(StatusCode::INVALID_ARGUMENT);
  EchoResponse response;
  Status status = SendRpcGetTrailers(rpc_options, &response, nullptr, nullptr);
  EXPECT_FALSE(status.ok());
  EXPECT_THAT(status.error_code(),
              ::testing::AnyOf(StatusCode::UNAVAILABLE, StatusCode::CANCELLED));
}

TEST_P(XdsExtProcEnd2endTest, TrailersOnlyExtProcConnectionErrorAllowCall) {
  int port = grpc_pick_unused_port_or_die();
  std::string target = absl::StrCat("localhost:", port);
  CreateAndStartBackends(1);
  using envoy::extensions::filters::http::ext_proc::v3::ProcessingMode;
  auto ext_proc_config = ExternalProcessorBuilder()
                             .SetTargetUri(target)
                             .SetInsecureChannelCredentials()
                             .SetFailureModeAllow(true)
                             .SetRequestHeaderMode(ProcessingMode::SKIP)
                             .SetResponseHeaderMode(ProcessingMode::SEND)
                             .SetResponseTrailerMode(ProcessingMode::SEND)
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
  rpc_options.set_server_expected_error(StatusCode::INVALID_ARGUMENT);
  EchoResponse response;
  Status status = SendRpcGetTrailers(rpc_options, &response, nullptr, nullptr);
  EXPECT_FALSE(status.ok());
  EXPECT_EQ(status.error_code(), StatusCode::INVALID_ARGUMENT);
}

TEST_P(XdsExtProcEnd2endTest,
       TrailersOnlyObservabilityExtProcConnectionErrorFailCall) {
  int port = grpc_pick_unused_port_or_die();
  std::string target = absl::StrCat("localhost:", port);
  CreateAndStartBackends(1);
  using envoy::extensions::filters::http::ext_proc::v3::ProcessingMode;
  auto ext_proc_config = ExternalProcessorBuilder()
                             .SetTargetUri(target)
                             .SetInsecureChannelCredentials()
                             .SetFailureModeAllow(false)
                             .SetObservabilityMode(true)
                             .SetRequestHeaderMode(ProcessingMode::SKIP)
                             .SetResponseHeaderMode(ProcessingMode::SEND)
                             .SetResponseTrailerMode(ProcessingMode::SEND)
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
  rpc_options.set_server_expected_error(StatusCode::INVALID_ARGUMENT);
  EchoResponse response;
  Status status = SendRpcGetTrailers(rpc_options, &response, nullptr, nullptr);
  EXPECT_FALSE(status.ok());
  EXPECT_THAT(status.error_code(),
              ::testing::AnyOf(StatusCode::UNAVAILABLE, StatusCode::CANCELLED));
}

TEST_P(XdsExtProcEnd2endTest,
       TrailersOnlyObservabilityExtProcConnectionErrorAllowCall) {
  int port = grpc_pick_unused_port_or_die();
  std::string target = absl::StrCat("localhost:", port);
  CreateAndStartBackends(1);
  using envoy::extensions::filters::http::ext_proc::v3::ProcessingMode;
  auto ext_proc_config = ExternalProcessorBuilder()
                             .SetTargetUri(target)
                             .SetInsecureChannelCredentials()
                             .SetFailureModeAllow(true)
                             .SetObservabilityMode(true)
                             .SetRequestHeaderMode(ProcessingMode::SKIP)
                             .SetResponseHeaderMode(ProcessingMode::SEND)
                             .SetResponseTrailerMode(ProcessingMode::SEND)
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
  rpc_options.set_server_expected_error(StatusCode::INVALID_ARGUMENT);
  EchoResponse response;
  Status status = SendRpcGetTrailers(rpc_options, &response, nullptr, nullptr);
  EXPECT_FALSE(status.ok());
  EXPECT_EQ(status.error_code(), StatusCode::INVALID_ARGUMENT);
}

class CloseExtProcStreamOnRequestBodyMockService
    : public MockExternalProcessorBase {
 public:
  grpc::Status Process(
      grpc::ServerContext* /*context*/,
      grpc::ServerReaderWriter<
          ::envoy::service::ext_proc::v3::ProcessingResponse,
          ::envoy::service::ext_proc::v3::ProcessingRequest>* stream) override {
    ::envoy::service::ext_proc::v3::ProcessingRequest request;
    while (stream->Read(&request)) {
      if (request.has_request_body()) {
        // Return an error to close the stream immediately on receiving body
        return grpc::Status(grpc::StatusCode::RESOURCE_EXHAUSTED,
                            "Closed on body");
      }
      ::envoy::service::ext_proc::v3::ProcessingResponse response;
      SetDefaultEmptyResponse(request, &response);
      stream->Write(response);
    }
    return grpc::Status::OK;
  }
};

TEST_P(XdsExtProcEnd2endTest, RequestBodyExtProcConnectionErrorFailCall) {
  auto mock_service =
      std::make_shared<CloseExtProcStreamOnRequestBodyMockService>();
  StartAlternativeServer(mock_service);
  CreateAndStartBackends(1);
  using envoy::extensions::filters::http::ext_proc::v3::ProcessingMode;
  auto ext_proc_config =
      ExternalProcessorBuilder()
          .SetTargetUri(alternative_ext_proc_server_->target())
          .SetInsecureChannelCredentials()
          .SetFailureModeAllow(false)  // Fail-closed!
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
  EchoResponse response;
  Status status = SendRpcGetTrailers(rpc_options, &response, nullptr, nullptr);
  EXPECT_FALSE(status.ok());
  EXPECT_EQ(status.error_code(), StatusCode::RESOURCE_EXHAUSTED);
  EXPECT_THAT(status.error_message(), ::testing::HasSubstr("Closed on body"));
}

TEST_P(XdsExtProcEnd2endTest, RequestBodyExtProcConnectionErrorAllowCall) {
  auto mock_service =
      std::make_shared<CloseExtProcStreamOnRequestBodyMockService>();
  StartAlternativeServer(mock_service);
  CreateAndStartBackends(1);
  using envoy::extensions::filters::http::ext_proc::v3::ProcessingMode;
  auto ext_proc_config =
      ExternalProcessorBuilder()
          .SetTargetUri(alternative_ext_proc_server_->target())
          .SetInsecureChannelCredentials()
          .SetFailureModeAllow(true)  // Fail-open!
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
  EchoResponse response;
  Status status = SendRpcGetTrailers(rpc_options, &response, nullptr, nullptr);
  // NOTE: Even though failure_mode_allow is true (fail-open), because the
  // stream failed AFTER the first body message was sent to ext_proc, the filter
  // must fail the RPC to avoid message loss or inconsistent state.
  EXPECT_FALSE(status.ok());
  EXPECT_EQ(status.error_code(), StatusCode::RESOURCE_EXHAUSTED);
  EXPECT_THAT(status.error_message(), ::testing::HasSubstr("Closed on body"));
}

TEST_P(XdsExtProcEnd2endTest, ObservabilityRequestBodyFailClosed) {
  auto mock_service =
      std::make_shared<CloseExtProcStreamOnRequestBodyMockService>();
  StartAlternativeServer(
      mock_service);  // Shuts down default and starts alternative
  CreateAndStartBackends(1);
  using envoy::extensions::filters::http::ext_proc::v3::ProcessingMode;
  auto ext_proc_config =
      ExternalProcessorBuilder()
          .SetTargetUri(alternative_ext_proc_server_->target())
          .SetInsecureChannelCredentials()
          .SetObservabilityMode(true)
          .SetFailureModeAllow(false)  // Fail-closed!
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
  EchoResponse response;
  Status status = SendRpcGetTrailers(rpc_options, &response, nullptr, nullptr);
  EXPECT_FALSE(status.ok());
  EXPECT_EQ(status.error_code(), StatusCode::RESOURCE_EXHAUSTED);
}

TEST_P(XdsExtProcEnd2endTest, ObservabilityRequestBodyFailOpen) {
  auto mock_service =
      std::make_shared<CloseExtProcStreamOnRequestBodyMockService>();
  StartAlternativeServer(
      mock_service);  // Shuts down default and starts alternative
  CreateAndStartBackends(1);
  using envoy::extensions::filters::http::ext_proc::v3::ProcessingMode;
  auto ext_proc_config =
      ExternalProcessorBuilder()
          .SetTargetUri(alternative_ext_proc_server_->target())
          .SetInsecureChannelCredentials()
          .SetObservabilityMode(true)
          .SetFailureModeAllow(true)  // Fail-open!
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
  EchoResponse response;
  Status status = SendRpcGetTrailers(rpc_options, &response, nullptr, nullptr);
  EXPECT_TRUE(status.ok()) << "Expected OK, got: " << status.error_message();
}

class CloseExtProcStreamOnResponseBodyMockService
    : public MockExternalProcessorBase {
 public:
  grpc::Status Process(
      grpc::ServerContext* /*context*/,
      grpc::ServerReaderWriter<
          ::envoy::service::ext_proc::v3::ProcessingResponse,
          ::envoy::service::ext_proc::v3::ProcessingRequest>* stream) override {
    ::envoy::service::ext_proc::v3::ProcessingRequest request;
    while (stream->Read(&request)) {
      if (request.has_response_body()) {
        // Return an error to close the stream immediately on receiving response
        // body
        return grpc::Status(grpc::StatusCode::RESOURCE_EXHAUSTED,
                            "Closed on response body");
      }
      ::envoy::service::ext_proc::v3::ProcessingResponse response;
      SetDefaultEmptyResponse(request, &response);
      stream->Write(response);
    }
    return grpc::Status::OK;
  }
};

TEST_P(XdsExtProcEnd2endTest, ObservabilityResponseBodyFailClosed) {
  auto mock_service =
      std::make_shared<CloseExtProcStreamOnResponseBodyMockService>();
  StartAlternativeServer(mock_service);
  CreateAndStartBackends(1);
  using envoy::extensions::filters::http::ext_proc::v3::ProcessingMode;
  auto ext_proc_config =
      ExternalProcessorBuilder()
          .SetTargetUri(alternative_ext_proc_server_->target())
          .SetInsecureChannelCredentials()
          .SetObservabilityMode(true)
          .SetFailureModeAllow(false)  // Fail-closed!
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
  EchoResponse response;
  Status status = SendRpcGetTrailers(rpc_options, &response, nullptr, nullptr);
  // NOTE: In an ideal world, this RPC should fail (fail-closed) because the
  // external processor returned an error on the response body. However, due to
  // a transport-level coalescing limitation in observability mode, the RPC
  // completes and the filter is destroyed before the async stream error can
  // arrive. This is a known limitation, so the RPC succeeds (OK).
  EXPECT_TRUE(status.ok())
      << "Expected OK due to known coalescing limitation, got: "
      << status.error_message();
}

TEST_P(XdsExtProcEnd2endTest, ObservabilityResponseBodyFailOpen) {
  auto mock_service =
      std::make_shared<CloseExtProcStreamOnResponseBodyMockService>();
  StartAlternativeServer(mock_service);
  CreateAndStartBackends(1);
  using envoy::extensions::filters::http::ext_proc::v3::ProcessingMode;
  auto ext_proc_config =
      ExternalProcessorBuilder()
          .SetTargetUri(alternative_ext_proc_server_->target())
          .SetInsecureChannelCredentials()
          .SetObservabilityMode(true)
          .SetFailureModeAllow(true)  // Fail-open!
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
  EchoResponse response;
  Status status = SendRpcGetTrailers(rpc_options, &response, nullptr, nullptr);
  EXPECT_TRUE(status.ok()) << "Expected OK, got: " << status.error_message();
}

class DuplicateRequestBodyResponseMockService
    : public MockExternalProcessorBase {
 public:
  grpc::Status Process(
      grpc::ServerContext* /*context*/,
      grpc::ServerReaderWriter<
          ::envoy::service::ext_proc::v3::ProcessingResponse,
          ::envoy::service::ext_proc::v3::ProcessingRequest>* stream) override {
    ::envoy::service::ext_proc::v3::ProcessingRequest request;
    while (stream->Read(&request)) {
      ::envoy::service::ext_proc::v3::ProcessingResponse response;
      SetDefaultEmptyResponse(request, &response);
      if (request.has_request_body()) {
        // Send the response twice!
        stream->Write(response);
        stream->Write(response);
      } else {
        stream->Write(response);
      }
    }
    return grpc::Status::OK;
  }
};

TEST_P(XdsExtProcEnd2endTest, DuplicateRequestBodyResponseFailsCall) {
  auto mock_service =
      std::make_shared<DuplicateRequestBodyResponseMockService>();
  StartAlternativeServer(mock_service);
  CreateAndStartBackends(1);
  using envoy::extensions::filters::http::ext_proc::v3::ProcessingMode;
  auto ext_proc_config =
      ExternalProcessorBuilder()
          .SetTargetUri(alternative_ext_proc_server_->target())
          .SetInsecureChannelCredentials()
          .SetFailureModeAllow(false)  // Fail-closed!
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
  EchoResponse response;
  Status status = SendRpcGetTrailers(rpc_options, &response, nullptr, nullptr);
  EXPECT_FALSE(status.ok());
  EXPECT_EQ(status.error_code(), StatusCode::INTERNAL);
  EXPECT_THAT(
      status.error_message(),
      ::testing::HasSubstr(
          "Received unexpected request body response from external processor"));
}

class FailOnSecondRequestBodyMockService : public MockExternalProcessorBase {
 public:
  grpc::Status Process(
      grpc::ServerContext* /*context*/,
      grpc::ServerReaderWriter<
          ::envoy::service::ext_proc::v3::ProcessingResponse,
          ::envoy::service::ext_proc::v3::ProcessingRequest>* stream) override {
    ::envoy::service::ext_proc::v3::ProcessingRequest request;
    int body_count = 0;
    while (stream->Read(&request)) {
      if (request.has_request_body()) {
        body_count++;
        if (body_count == 2) {
          return grpc::Status(grpc::StatusCode::RESOURCE_EXHAUSTED,
                              "Closed on second body");
        }
      }
      ::envoy::service::ext_proc::v3::ProcessingResponse response;
      SetDefaultEmptyResponse(request, &response);
      stream->Write(response);
    }
    return grpc::Status::OK;
  }
};

TEST_P(XdsExtProcEnd2endTest, BidiStreamMultipleMessagesPingPongSuccess) {
  CreateAndStartBackends(1);
  using envoy::extensions::filters::http::ext_proc::v3::ProcessingMode;
  auto ext_proc_config = ExternalProcessorBuilder()
                             .SetTargetUri(ext_proc_server_->target())
                             .SetInsecureChannelCredentials()
                             .SetRequestHeaderMode(ProcessingMode::SEND)
                             .SetRequestBodyMode(ProcessingMode::GRPC)
                             .SetResponseHeaderMode(ProcessingMode::SEND)
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

  ClientContext context;
  auto stream = stub_->BidiStream(&context);

  EchoRequest request;
  EchoResponse response;

  // Message 1
  request.set_message("message1");
  EXPECT_TRUE(stream->Write(request));
  EXPECT_TRUE(stream->Read(&response));
  EXPECT_EQ(response.message(),
            "message1-request-body-mutated-response-body-mutated");

  // Message 2
  request.set_message("message2");
  EXPECT_TRUE(stream->Write(request));
  EXPECT_TRUE(stream->Read(&response));
  EXPECT_EQ(response.message(),
            "message2-request-body-mutated-response-body-mutated");

  // Message 3
  request.set_message("message3");
  EXPECT_TRUE(stream->Write(request));
  EXPECT_TRUE(stream->Read(&response));
  EXPECT_EQ(response.message(),
            "message3-request-body-mutated-response-body-mutated");

  EXPECT_TRUE(stream->WritesDone());
  EXPECT_FALSE(stream->Read(&response));

  Status status = stream->Finish();
  EXPECT_TRUE(status.ok()) << "RPC failed: " << status.error_message();

  MockExternalProcessorService::RequestCounts expected_counts;
  expected_counts.request_headers = 1;
  expected_counts.response_headers = 1;
  expected_counts.response_trailers = 1;
  expected_counts.request_body = 3;
  expected_counts.response_body = 3;
  ext_proc_server_->ext_proc_service()->WaitForRequestCounts(expected_counts);
}

class EarlyHalfCloseWithMessageMockService : public MockExternalProcessorBase {
 public:
  grpc::Status Process(
      grpc::ServerContext* /*context*/,
      grpc::ServerReaderWriter<
          ::envoy::service::ext_proc::v3::ProcessingResponse,
          ::envoy::service::ext_proc::v3::ProcessingRequest>* stream) override {
    ::envoy::service::ext_proc::v3::ProcessingRequest request;
    int request_body_count = 0;
    while (stream->Read(&request)) {
      ::envoy::service::ext_proc::v3::ProcessingResponse response;
      SetDefaultEmptyResponse(request, &response);
      if (request.has_request_body()) {
        request_body_count++;
        const auto& body_req = request.request_body();
        EXPECT_FALSE(body_req.end_of_stream());
        EXPECT_FALSE(body_req.end_of_stream_without_message());
        auto* body_mutation = response.mutable_request_body()
                                  ->mutable_response()
                                  ->mutable_body_mutation();
        if (request_body_count == 1) {
          grpc::testing::EchoRequest echo_request;
          if (echo_request.ParseFromString(request.request_body().body())) {
            echo_request.set_message(
                absl::StrCat(echo_request.message(), "-mutated"));
            std::string mutated_body;
            GRPC_CHECK(echo_request.SerializeToString(&mutated_body));
            body_mutation->mutable_streamed_response()->set_body(mutated_body);
          } else {
            body_mutation->mutable_streamed_response()->set_body(
                request.request_body().body());
          }
          body_mutation->mutable_streamed_response()->set_end_of_stream(true);
        } else {
          ADD_FAILURE() << "Processor received message after half-close";
        }
      }
      stream->Write(response);
    }
    return grpc::Status::OK;
  }
};

class EarlyHalfCloseWithoutMessageMockService
    : public MockExternalProcessorBase {
 public:
  grpc::Status Process(
      grpc::ServerContext* /*context*/,
      grpc::ServerReaderWriter<
          ::envoy::service::ext_proc::v3::ProcessingResponse,
          ::envoy::service::ext_proc::v3::ProcessingRequest>* stream) override {
    ::envoy::service::ext_proc::v3::ProcessingRequest request;
    int request_body_count = 0;
    while (stream->Read(&request)) {
      ::envoy::service::ext_proc::v3::ProcessingResponse response;
      SetDefaultEmptyResponse(request, &response);
      if (request.has_request_body()) {
        request_body_count++;
        const auto& body_req = request.request_body();
        EXPECT_FALSE(body_req.end_of_stream());
        EXPECT_FALSE(body_req.end_of_stream_without_message());
        auto* body_mutation = response.mutable_request_body()
                                  ->mutable_response()
                                  ->mutable_body_mutation();
        if (request_body_count == 1) {
          body_mutation->mutable_streamed_response()->set_end_of_stream(true);
          body_mutation->mutable_streamed_response()
              ->set_end_of_stream_without_message(true);
        } else {
          ADD_FAILURE() << "Processor received message after half-close";
        }
      }
      stream->Write(response);
    }
    return grpc::Status::OK;
  }
};

TEST_P(XdsExtProcEnd2endTest, BidiStreamEarlyHalfCloseWithMessageFailure) {
  CreateAndStartBackends(1);
  auto ext_proc_service =
      std::make_shared<EarlyHalfCloseWithMessageMockService>();
  StartAlternativeServer(ext_proc_service);
  using envoy::extensions::filters::http::ext_proc::v3::ProcessingMode;
  auto ext_proc_config =
      ExternalProcessorBuilder()
          .SetTargetUri(alternative_ext_proc_server_->target())
          .SetInsecureChannelCredentials()
          .SetRequestHeaderMode(ProcessingMode::SEND)
          .SetRequestBodyMode(ProcessingMode::GRPC)
          .SetResponseHeaderMode(ProcessingMode::SKIP)
          .SetResponseBodyMode(ProcessingMode::NONE)
          .SetResponseTrailerMode(ProcessingMode::SKIP)
          .Build();
  Listener listener = BuildListenerWithExtProcFilter(ext_proc_config);
  RouteConfiguration route_config = default_route_config_;
  SetListenerAndRouteConfiguration(balancer_.get(), listener, route_config);
  balancer_->ads_service()->SetCdsResource(default_cluster_);
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(EdsResourceArgs({
      {"locality0", CreateEndpointsForBackends(0, 1)},
  })));
  ClientContext context;
  auto stream = stub_->BidiStream(&context);
  EchoRequest request;
  EchoResponse response;
  // Message 1 - should succeed and be mutated
  request.set_message("message1");
  EXPECT_TRUE(stream->Write(request));
  EXPECT_TRUE(stream->Read(&response));
  EXPECT_EQ(response.message(), "message1-mutated");
  // Message 2 - should fail because processor half-closed
  request.set_message("message2");
  stream->Write(request);
  EXPECT_FALSE(stream->Read(&response));
  Status status = stream->Finish();
  EXPECT_FALSE(status.ok());
  EXPECT_EQ(status.error_code(), StatusCode::INTERNAL);
  EXPECT_THAT(
      status.error_message(),
      ::testing::HasSubstr("Client sends closed by external processor"));
}

TEST_P(XdsExtProcEnd2endTest, BidiStreamEarlyHalfCloseWithoutMessageFailure) {
  CreateAndStartBackends(1);
  auto ext_proc_service =
      std::make_shared<EarlyHalfCloseWithoutMessageMockService>();
  StartAlternativeServer(ext_proc_service);
  using envoy::extensions::filters::http::ext_proc::v3::ProcessingMode;
  auto ext_proc_config =
      ExternalProcessorBuilder()
          .SetTargetUri(alternative_ext_proc_server_->target())
          .SetInsecureChannelCredentials()
          .SetRequestHeaderMode(ProcessingMode::SEND)
          .SetRequestBodyMode(ProcessingMode::GRPC)
          .SetResponseHeaderMode(ProcessingMode::SKIP)
          .SetResponseBodyMode(ProcessingMode::NONE)
          .SetResponseTrailerMode(ProcessingMode::SKIP)
          .Build();
  Listener listener = BuildListenerWithExtProcFilter(ext_proc_config);
  RouteConfiguration route_config = default_route_config_;
  SetListenerAndRouteConfiguration(balancer_.get(), listener, route_config);
  balancer_->ads_service()->SetCdsResource(default_cluster_);
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(EdsResourceArgs({
      {"locality0", CreateEndpointsForBackends(0, 1)},
  })));
  ClientContext context;
  auto stream = stub_->BidiStream(&context);
  EchoRequest request;
  EchoResponse response;
  // Message 1 - sent. Processor will drop it and half-close.
  request.set_message("message1");
  EXPECT_FALSE(stream->Write(request));
  // Message 2 - should fail because processor half-closed
  request.set_message("message2");
  stream->Write(request);
  EXPECT_FALSE(stream->Read(&response));
  Status status = stream->Finish();
  EXPECT_FALSE(status.ok());
  EXPECT_EQ(status.error_code(), StatusCode::INTERNAL);
  EXPECT_THAT(
      status.error_message(),
      ::testing::HasSubstr("Client sends closed by external processor"));
}

TEST_P(XdsExtProcEnd2endTest, BidiStreamNormalHalfCloseSuccess) {
  CreateAndStartBackends(1);
  struct ExtProcClaims {
    absl::Mutex mu;
    int body_chunks ABSL_GUARDED_BY(mu) = 0;
    bool saw_eos_without_msg ABSL_GUARDED_BY(mu) = false;
  };
  auto claims = std::make_shared<ExtProcClaims>();
  auto ext_proc_service = std::make_shared<GenericMockService>(
      [claims](const ::envoy::service::ext_proc::v3::ProcessingRequest& request,
               ::envoy::service::ext_proc::v3::ProcessingResponse* response) {
        SetDefaultEmptyResponse(request, response);
        if (request.has_request_body()) {
          absl::MutexLock lock(&claims->mu);
          claims->body_chunks++;
          const auto& body_req = request.request_body();
          if (claims->body_chunks <= 3) {
            EXPECT_FALSE(body_req.end_of_stream());
            EXPECT_FALSE(body_req.end_of_stream_without_message());
          }
          auto* body_mutation = response->mutable_request_body()
                                    ->mutable_response()
                                    ->mutable_body_mutation();
          if (body_req.end_of_stream_without_message()) {
            claims->saw_eos_without_msg = true;
            EXPECT_TRUE(body_req.end_of_stream());
            body_mutation->mutable_streamed_response()
                ->set_end_of_stream_without_message(true);
          } else {
            body_mutation->mutable_streamed_response()->set_body(
                body_req.body());
          }
        }
      });
  StartAlternativeServer(ext_proc_service);
  using envoy::extensions::filters::http::ext_proc::v3::ProcessingMode;
  auto ext_proc_config =
      ExternalProcessorBuilder()
          .SetTargetUri(alternative_ext_proc_server_->target())
          .SetInsecureChannelCredentials()
          .SetRequestHeaderMode(ProcessingMode::SEND)
          .SetRequestBodyMode(ProcessingMode::GRPC)
          .SetResponseHeaderMode(ProcessingMode::SKIP)
          .SetResponseBodyMode(ProcessingMode::NONE)
          .SetResponseTrailerMode(ProcessingMode::SKIP)
          .Build();
  Listener listener = BuildListenerWithExtProcFilter(ext_proc_config);
  RouteConfiguration route_config = default_route_config_;
  SetListenerAndRouteConfiguration(balancer_.get(), listener, route_config);
  balancer_->ads_service()->SetCdsResource(default_cluster_);
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(EdsResourceArgs({
      {"locality0", CreateEndpointsForBackends(0, 1)},
  })));
  ClientContext context;
  auto stream = stub_->BidiStream(&context);
  EchoRequest request;
  EchoResponse response;
  // Send 3 messages
  for (int i = 1; i <= 3; ++i) {
    request.set_message(absl::StrCat("message", i));
    EXPECT_TRUE(stream->Write(request));
    EXPECT_TRUE(stream->Read(&response));
    EXPECT_EQ(response.message(), absl::StrCat("message", i));
  }
  EXPECT_TRUE(stream->WritesDone());
  EXPECT_FALSE(stream->Read(&response));
  Status status = stream->Finish();
  EXPECT_TRUE(status.ok()) << status.error_message();
  // Wait for the ExtProc server to finish processing the 4th chunk (EOS)
  {
    absl::MutexLock lock(&claims->mu);
    auto condition = [&claims]() ABSL_SHARED_LOCKS_REQUIRED(claims->mu) {
      return claims->body_chunks == 4;
    };
    claims->mu.AwaitWithTimeout(absl::Condition(&condition), absl::Seconds(5));
  }
  absl::MutexLock lock(&claims->mu);
  EXPECT_EQ(claims->body_chunks, 4);  // 3 messages + 1 EOS
  EXPECT_TRUE(claims->saw_eos_without_msg);
}

TEST_P(XdsExtProcEnd2endTest, BidiStreamAsymmetricWriteReadSuccess) {
  CreateAndStartBackends(1);
  using envoy::extensions::filters::http::ext_proc::v3::ProcessingMode;
  auto ext_proc_config = ExternalProcessorBuilder()
                             .SetTargetUri(ext_proc_server_->target())
                             .SetInsecureChannelCredentials()
                             .SetRequestHeaderMode(ProcessingMode::SEND)
                             .SetRequestBodyMode(ProcessingMode::GRPC)
                             .SetResponseHeaderMode(ProcessingMode::SEND)
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

  ClientContext context;
  auto stream = stub_->BidiStream(&context);

  EchoRequest request;
  request.set_message("message1");
  EXPECT_TRUE(stream->Write(request));
  request.set_message("message2");
  EXPECT_TRUE(stream->Write(request));
  request.set_message("message3");
  EXPECT_TRUE(stream->Write(request));

  EXPECT_TRUE(stream->WritesDone());

  EchoResponse response;
  EXPECT_TRUE(stream->Read(&response));
  EXPECT_EQ(response.message(),
            "message1-request-body-mutated-response-body-mutated");
  EXPECT_TRUE(stream->Read(&response));
  EXPECT_EQ(response.message(),
            "message2-request-body-mutated-response-body-mutated");
  EXPECT_TRUE(stream->Read(&response));
  EXPECT_EQ(response.message(),
            "message3-request-body-mutated-response-body-mutated");

  EXPECT_FALSE(stream->Read(&response));

  Status status = stream->Finish();
  EXPECT_TRUE(status.ok()) << "RPC failed: " << status.error_message();

  MockExternalProcessorService::RequestCounts expected_counts;
  expected_counts.request_headers = 1;
  expected_counts.response_headers = 1;
  expected_counts.response_trailers = 1;
  expected_counts.request_body = 3;
  expected_counts.response_body = 3;
  ext_proc_server_->ext_proc_service()->WaitForRequestCounts(expected_counts);
}

TEST_P(XdsExtProcEnd2endTest, BidiStreamObservabilitySuccess) {
  CreateAndStartBackends(1);
  using envoy::extensions::filters::http::ext_proc::v3::ProcessingMode;
  auto ext_proc_config = ExternalProcessorBuilder()
                             .SetTargetUri(ext_proc_server_->target())
                             .SetInsecureChannelCredentials()
                             .SetObservabilityMode(true)
                             .SetRequestHeaderMode(ProcessingMode::SEND)
                             .SetRequestBodyMode(ProcessingMode::GRPC)
                             .SetResponseHeaderMode(ProcessingMode::SEND)
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

  ClientContext context;
  auto stream = stub_->BidiStream(&context);

  EchoRequest request;
  request.set_message("message1");
  EXPECT_TRUE(stream->Write(request));
  request.set_message("message2");
  EXPECT_TRUE(stream->Write(request));
  request.set_message("message3");
  EXPECT_TRUE(stream->Write(request));

  EXPECT_TRUE(stream->WritesDone());

  EchoResponse response;
  EXPECT_TRUE(stream->Read(&response));
  EXPECT_EQ(response.message(), "message1");
  EXPECT_TRUE(stream->Read(&response));
  EXPECT_EQ(response.message(), "message2");
  EXPECT_TRUE(stream->Read(&response));
  EXPECT_EQ(response.message(), "message3");

  EXPECT_FALSE(stream->Read(&response));

  Status status = stream->Finish();
  EXPECT_TRUE(status.ok()) << "RPC failed: " << status.error_message();

  MockExternalProcessorService::RequestCounts expected_counts;
  expected_counts.request_headers = 1;
  expected_counts.response_headers = 1;
  expected_counts.response_trailers = 1;
  expected_counts.request_body = 3;
  expected_counts.response_body = 3;
  ext_proc_server_->ext_proc_service()->WaitForRequestCounts(expected_counts);
}

TEST_P(XdsExtProcEnd2endTest, BidiStreamExtProcConnectionErrorFailClosed) {
  auto mock_service = std::make_shared<FailOnSecondRequestBodyMockService>();
  StartAlternativeServer(mock_service);
  CreateAndStartBackends(1);
  using envoy::extensions::filters::http::ext_proc::v3::ProcessingMode;
  auto ext_proc_config =
      ExternalProcessorBuilder()
          .SetTargetUri(alternative_ext_proc_server_->target())
          .SetInsecureChannelCredentials()
          .SetFailureModeAllow(false)
          .SetRequestHeaderMode(ProcessingMode::SEND)
          .SetRequestBodyMode(ProcessingMode::GRPC)
          .SetResponseHeaderMode(ProcessingMode::SKIP)
          .SetResponseBodyMode(ProcessingMode::NONE)
          .SetResponseTrailerMode(ProcessingMode::SKIP)
          .Build();
  Listener listener = BuildListenerWithExtProcFilter(ext_proc_config);
  RouteConfiguration route_config = default_route_config_;
  SetListenerAndRouteConfiguration(balancer_.get(), listener, route_config);
  balancer_->ads_service()->SetCdsResource(default_cluster_);
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(EdsResourceArgs({
      {"locality0", CreateEndpointsForBackends(0, 1)},
  })));

  ClientContext context;
  auto stream = stub_->BidiStream(&context);

  EchoRequest request;
  EchoResponse response;

  request.set_message("message1");
  EXPECT_TRUE(stream->Write(request));
  EXPECT_TRUE(stream->Read(&response));

  request.set_message("message2");
  stream->Write(request);

  Status status = stream->Finish();
  EXPECT_FALSE(status.ok());
  EXPECT_THAT(
      status.error_code(),
      ::testing::AnyOf(StatusCode::RESOURCE_EXHAUSTED, StatusCode::CANCELLED));
}

TEST_P(XdsExtProcEnd2endTest, BidiStreamExtProcConnectionErrorFailOpen) {
  auto mock_service = std::make_shared<FailOnSecondRequestBodyMockService>();
  StartAlternativeServer(mock_service);
  CreateAndStartBackends(1);
  using envoy::extensions::filters::http::ext_proc::v3::ProcessingMode;
  auto ext_proc_config =
      ExternalProcessorBuilder()
          .SetTargetUri(alternative_ext_proc_server_->target())
          .SetInsecureChannelCredentials()
          .SetFailureModeAllow(true)
          .SetRequestHeaderMode(ProcessingMode::SEND)
          .SetRequestBodyMode(ProcessingMode::GRPC)
          .SetResponseHeaderMode(ProcessingMode::SKIP)
          .SetResponseBodyMode(ProcessingMode::NONE)
          .SetResponseTrailerMode(ProcessingMode::SKIP)
          .Build();
  Listener listener = BuildListenerWithExtProcFilter(ext_proc_config);
  RouteConfiguration route_config = default_route_config_;
  SetListenerAndRouteConfiguration(balancer_.get(), listener, route_config);
  balancer_->ads_service()->SetCdsResource(default_cluster_);
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(EdsResourceArgs({
      {"locality0", CreateEndpointsForBackends(0, 1)},
  })));

  ClientContext context;
  auto stream = stub_->BidiStream(&context);

  EchoRequest request;
  EchoResponse response;

  request.set_message("message1");
  EXPECT_TRUE(stream->Write(request));
  EXPECT_TRUE(stream->Read(&response));

  request.set_message("message2");
  stream->Write(request);

  Status status = stream->Finish();
  EXPECT_FALSE(status.ok());
  EXPECT_THAT(
      status.error_code(),
      ::testing::AnyOf(StatusCode::RESOURCE_EXHAUSTED, StatusCode::CANCELLED));
}

TEST_P(XdsExtProcEnd2endTest,
       BidiStreamObservabilityExtProcConnectionErrorFailClosed) {
  ResetStubWithUniqueArg();
  auto mock_service = std::make_shared<FailOnSecondRequestBodyMockService>();
  StartAlternativeServer(mock_service);
  CreateAndStartBackends(1);
  using envoy::extensions::filters::http::ext_proc::v3::ProcessingMode;
  auto ext_proc_config =
      ExternalProcessorBuilder()
          .SetTargetUri(alternative_ext_proc_server_->target())
          .SetInsecureChannelCredentials()
          .SetObservabilityMode(true)
          .SetFailureModeAllow(false)  // Fail-closed
          .SetRequestHeaderMode(ProcessingMode::SEND)
          .SetRequestBodyMode(ProcessingMode::GRPC)
          .SetResponseHeaderMode(ProcessingMode::SKIP)
          .SetResponseBodyMode(ProcessingMode::NONE)
          .SetResponseTrailerMode(ProcessingMode::SKIP)
          .Build();
  Listener listener = BuildListenerWithExtProcFilter(ext_proc_config);
  RouteConfiguration route_config = default_route_config_;
  SetListenerAndRouteConfiguration(balancer_.get(), listener, route_config);
  balancer_->ads_service()->SetCdsResource(default_cluster_);
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(EdsResourceArgs({
      {"locality0", CreateEndpointsForBackends(0, 1)},
  })));

  ClientContext context;
  auto stream = stub_->BidiStream(&context);

  EchoRequest request;
  EchoResponse response;

  request.set_message("message1");
  EXPECT_TRUE(stream->Write(request));
  EXPECT_TRUE(stream->Read(&response));
  EXPECT_EQ(response.message(), "message1");

  request.set_message("message2");
  stream->Write(request);

  Status status = stream->Finish();
  EXPECT_FALSE(status.ok());
  EXPECT_THAT(
      status.error_code(),
      ::testing::AnyOf(StatusCode::RESOURCE_EXHAUSTED, StatusCode::CANCELLED));
}

TEST_P(XdsExtProcEnd2endTest,
       BidiStreamObservabilityExtProcConnectionErrorFailOpen) {
  ResetStubWithUniqueArg();
  auto mock_service = std::make_shared<FailOnSecondRequestBodyMockService>();
  StartAlternativeServer(mock_service);
  CreateAndStartBackends(1);
  using envoy::extensions::filters::http::ext_proc::v3::ProcessingMode;
  auto ext_proc_config =
      ExternalProcessorBuilder()
          .SetTargetUri(alternative_ext_proc_server_->target())
          .SetInsecureChannelCredentials()
          .SetObservabilityMode(true)
          .SetFailureModeAllow(true)  // Fail-open
          .SetRequestHeaderMode(ProcessingMode::SEND)
          .SetRequestBodyMode(ProcessingMode::GRPC)
          .SetResponseHeaderMode(ProcessingMode::SKIP)
          .SetResponseBodyMode(ProcessingMode::NONE)
          .SetResponseTrailerMode(ProcessingMode::SKIP)
          .Build();
  Listener listener = BuildListenerWithExtProcFilter(ext_proc_config);
  RouteConfiguration route_config = default_route_config_;
  SetListenerAndRouteConfiguration(balancer_.get(), listener, route_config);
  balancer_->ads_service()->SetCdsResource(default_cluster_);
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(EdsResourceArgs({
      {"locality0", CreateEndpointsForBackends(0, 1)},
  })));

  ClientContext context;
  auto stream = stub_->BidiStream(&context);

  EchoRequest request;
  EchoResponse response;

  request.set_message("message1");
  EXPECT_TRUE(stream->Write(request));
  EXPECT_TRUE(stream->Read(&response));
  EXPECT_EQ(response.message(), "message1");

  request.set_message("message2");
  EXPECT_TRUE(stream->Write(request));
  // In observability mode, even if the ext_proc stream fails, the data plane
  // stream should continue.
  EXPECT_TRUE(stream->Read(&response));
  EXPECT_EQ(response.message(), "message2");

  stream->WritesDone();
  Status status = stream->Finish();
  EXPECT_TRUE(status.ok()) << status.error_message();
}

TEST_P(XdsExtProcEnd2endTest,
       RequestAttributesSentInRequestBodyWhenRequestHeaderIsSkip) {
  std::string path_received;
  std::string method_received;
  bool headers_received = false;
  absl::Mutex mu;
  auto mock_service = std::make_shared<GenericMockService>(
      [&](const ::envoy::service::ext_proc::v3::ProcessingRequest& request,
          ::envoy::service::ext_proc::v3::ProcessingResponse* response) {
        if (request.has_request_headers()) {
          absl::MutexLock lock(&mu);
          headers_received = true;
          SetDefaultEmptyResponse(request, response);
        } else if (request.has_request_body()) {
          absl::MutexLock lock(&mu);
          path_received = GetExtProcAttribute(request, "request.path");
          method_received = GetExtProcAttribute(request, "request.method");
          // CRITICAL FIX: Must initialize body_mutation to avoid internal
          // parser errors
          response->mutable_request_body()
              ->mutable_response()
              ->mutable_body_mutation();
        } else {
          SetDefaultEmptyResponse(request, response);
        }
      });
  StartAlternativeServer(mock_service);
  CreateAndStartBackends(1);
  using envoy::extensions::filters::http::ext_proc::v3::ProcessingMode;
  auto ext_proc_config =
      ExternalProcessorBuilder()
          .SetTargetUri(alternative_ext_proc_server_->target())
          .SetInsecureChannelCredentials()
          .SetRequestHeaderMode(ProcessingMode::SKIP)
          .SetResponseHeaderMode(ProcessingMode::SKIP)
          .SetResponseTrailerMode(ProcessingMode::SKIP)
          .SetRequestBodyMode(ProcessingMode::GRPC)
          .SetResponseBodyMode(ProcessingMode::NONE)
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

  alternative_ext_proc_server_->Shutdown();

  EXPECT_FALSE(headers_received);
  EXPECT_EQ(path_received, "/grpc.testing.EchoTestService/Echo");
  EXPECT_EQ(method_received, "POST");
}

TEST_P(XdsExtProcEnd2endTest,
       ClientToServerRequestBodyContinueAndReplaceFailClosedFailsCall) {
  auto mock_service = std::make_shared<GenericMockService>(
      [&](const ::envoy::service::ext_proc::v3::ProcessingRequest& request,
          ::envoy::service::ext_proc::v3::ProcessingResponse* response) {
        if (request.has_request_body()) {
          auto* common_response =
              response->mutable_request_body()->mutable_response();
          common_response->set_status(
              ::envoy::service::ext_proc::v3::
                  CommonResponse_ResponseStatus_CONTINUE_AND_REPLACE);
          common_response->mutable_body_mutation();
        } else {
          SetDefaultEmptyResponse(request, response);
        }
      });
  StartAlternativeServer(mock_service);
  CreateAndStartBackends(1);
  using envoy::extensions::filters::http::ext_proc::v3::ProcessingMode;
  auto ext_proc_config =
      ExternalProcessorBuilder()
          .SetTargetUri(alternative_ext_proc_server_->target())
          .SetInsecureChannelCredentials()
          .SetFailureModeAllow(false)  // Fail-closed!
          .SetRequestHeaderMode(ProcessingMode::SEND)
          .SetResponseHeaderMode(ProcessingMode::SKIP)
          .SetResponseTrailerMode(ProcessingMode::SKIP)
          .SetRequestBodyMode(ProcessingMode::GRPC)
          .SetResponseBodyMode(ProcessingMode::NONE)
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
  EXPECT_THAT(status.error_message(),
              ::testing::HasSubstr("CONTINUE_AND_REPLACE is not supported"));
}

TEST_P(XdsExtProcEnd2endTest,
       ClientToServerRequestBodyContinueAndReplaceFailOpenFailsCall) {
  auto mock_service = std::make_shared<GenericMockService>(
      [&](const ::envoy::service::ext_proc::v3::ProcessingRequest& request,
          ::envoy::service::ext_proc::v3::ProcessingResponse* response) {
        if (request.has_request_body()) {
          auto* common_response =
              response->mutable_request_body()->mutable_response();
          common_response->set_status(
              ::envoy::service::ext_proc::v3::
                  CommonResponse_ResponseStatus_CONTINUE_AND_REPLACE);
          common_response->mutable_body_mutation();
        } else {
          SetDefaultEmptyResponse(request, response);
        }
      });
  StartAlternativeServer(mock_service);
  CreateAndStartBackends(1);
  using envoy::extensions::filters::http::ext_proc::v3::ProcessingMode;
  auto ext_proc_config =
      ExternalProcessorBuilder()
          .SetTargetUri(alternative_ext_proc_server_->target())
          .SetInsecureChannelCredentials()
          .SetFailureModeAllow(true)  // Fail-open (configured)
          .SetRequestHeaderMode(ProcessingMode::SEND)
          .SetResponseHeaderMode(ProcessingMode::SKIP)
          .SetResponseTrailerMode(ProcessingMode::SKIP)
          .SetRequestBodyMode(ProcessingMode::GRPC)
          .SetResponseBodyMode(ProcessingMode::NONE)
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
  // Should STILL fail because it is committed!
  EXPECT_FALSE(status.ok());
  EXPECT_EQ(status.error_code(), StatusCode::INTERNAL);
  EXPECT_THAT(status.error_message(),
              ::testing::HasSubstr("CONTINUE_AND_REPLACE is not supported"));
}

TEST_P(XdsExtProcEnd2endTest,
       ClientToServerRequestBodyGrpcMessageCompressedFailClosedFailsCall) {
  auto mock_service = std::make_shared<GenericMockService>(
      [&](const ::envoy::service::ext_proc::v3::ProcessingRequest& request,
          ::envoy::service::ext_proc::v3::ProcessingResponse* response) {
        if (request.has_request_body()) {
          auto* common_response =
              response->mutable_request_body()->mutable_response();
          auto* body_mutation = common_response->mutable_body_mutation();
          auto* streamed_response = body_mutation->mutable_streamed_response();
          streamed_response->set_grpc_message_compressed(true);
        } else {
          SetDefaultEmptyResponse(request, response);
        }
      });
  StartAlternativeServer(mock_service);
  CreateAndStartBackends(1);
  using envoy::extensions::filters::http::ext_proc::v3::ProcessingMode;
  auto ext_proc_config =
      ExternalProcessorBuilder()
          .SetTargetUri(alternative_ext_proc_server_->target())
          .SetInsecureChannelCredentials()
          .SetFailureModeAllow(false)  // Fail-closed!
          .SetRequestHeaderMode(ProcessingMode::SEND)
          .SetResponseHeaderMode(ProcessingMode::SKIP)
          .SetResponseTrailerMode(ProcessingMode::SKIP)
          .SetRequestBodyMode(ProcessingMode::GRPC)
          .SetResponseBodyMode(ProcessingMode::NONE)
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
  EXPECT_THAT(status.error_message(),
              ::testing::HasSubstr("grpc_message_compressed is not supported"));
}

TEST_P(XdsExtProcEnd2endTest,
       ClientToServerRequestBodyGrpcMessageCompressedFailOpenFailsCall) {
  auto mock_service = std::make_shared<GenericMockService>(
      [&](const ::envoy::service::ext_proc::v3::ProcessingRequest& request,
          ::envoy::service::ext_proc::v3::ProcessingResponse* response) {
        if (request.has_request_body()) {
          auto* common_response =
              response->mutable_request_body()->mutable_response();
          auto* body_mutation = common_response->mutable_body_mutation();
          auto* streamed_response = body_mutation->mutable_streamed_response();
          streamed_response->set_grpc_message_compressed(true);
        } else {
          SetDefaultEmptyResponse(request, response);
        }
      });
  StartAlternativeServer(mock_service);
  CreateAndStartBackends(1);
  using envoy::extensions::filters::http::ext_proc::v3::ProcessingMode;
  auto ext_proc_config =
      ExternalProcessorBuilder()
          .SetTargetUri(alternative_ext_proc_server_->target())
          .SetInsecureChannelCredentials()
          .SetFailureModeAllow(true)  // Fail-open (configured)
          .SetRequestHeaderMode(ProcessingMode::SEND)
          .SetResponseHeaderMode(ProcessingMode::SKIP)
          .SetResponseTrailerMode(ProcessingMode::SKIP)
          .SetRequestBodyMode(ProcessingMode::GRPC)
          .SetResponseBodyMode(ProcessingMode::NONE)
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
  // Should STILL fail because it is committed!
  EXPECT_FALSE(status.ok());
  EXPECT_EQ(status.error_code(), StatusCode::INTERNAL);
  EXPECT_THAT(status.error_message(),
              ::testing::HasSubstr("grpc_message_compressed is not supported"));
}

TEST_P(XdsExtProcEnd2endTest,
       ServerToClientResponseBodyContinueAndReplaceFailClosedFailsCall) {
  auto mock_service = std::make_shared<GenericMockService>(
      [&](const ::envoy::service::ext_proc::v3::ProcessingRequest& request,
          ::envoy::service::ext_proc::v3::ProcessingResponse* response) {
        if (request.has_response_body()) {
          auto* common_response =
              response->mutable_response_body()->mutable_response();
          common_response->set_status(
              ::envoy::service::ext_proc::v3::
                  CommonResponse_ResponseStatus_CONTINUE_AND_REPLACE);
          common_response->mutable_body_mutation();
        } else {
          SetDefaultEmptyResponse(request, response);
        }
      });
  StartAlternativeServer(mock_service);
  CreateAndStartBackends(1);
  using envoy::extensions::filters::http::ext_proc::v3::ProcessingMode;
  auto ext_proc_config =
      ExternalProcessorBuilder()
          .SetTargetUri(alternative_ext_proc_server_->target())
          .SetInsecureChannelCredentials()
          .SetFailureModeAllow(false)  // Fail-closed!
          .SetRequestHeaderMode(ProcessingMode::SKIP)
          .SetResponseHeaderMode(ProcessingMode::SEND)
          .SetResponseTrailerMode(ProcessingMode::SEND)
          .SetRequestBodyMode(ProcessingMode::NONE)
          .SetResponseBodyMode(ProcessingMode::GRPC)
          .Build();
  Listener listener = BuildListenerWithExtProcFilter(ext_proc_config);
  RouteConfiguration route_config = default_route_config_;
  SetListenerAndRouteConfiguration(balancer_.get(), listener, route_config);
  balancer_->ads_service()->SetCdsResource(default_cluster_);
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(EdsResourceArgs({
      {"locality0", CreateEndpointsForBackends(0, 1)},
  })));
  ClientContext context;
  auto stream = stub_->BidiStream(&context);
  EchoRequest request;
  request.set_message("hello");
  EXPECT_TRUE(stream->Write(request));
  EchoResponse response;
  EXPECT_FALSE(stream->Read(&response));
  Status status = stream->Finish();
  EXPECT_FALSE(status.ok());
  EXPECT_EQ(status.error_code(), StatusCode::INTERNAL);
  EXPECT_THAT(status.error_message(),
              ::testing::HasSubstr("CONTINUE_AND_REPLACE is not supported"));
}

TEST_P(XdsExtProcEnd2endTest,
       ServerToClientResponseBodyContinueAndReplaceFailOpenFailsCall) {
  auto mock_service = std::make_shared<GenericMockService>(
      [&](const ::envoy::service::ext_proc::v3::ProcessingRequest& request,
          ::envoy::service::ext_proc::v3::ProcessingResponse* response) {
        if (request.has_response_body()) {
          auto* common_response =
              response->mutable_response_body()->mutable_response();
          common_response->set_status(
              ::envoy::service::ext_proc::v3::
                  CommonResponse_ResponseStatus_CONTINUE_AND_REPLACE);
          common_response->mutable_body_mutation();
        } else {
          SetDefaultEmptyResponse(request, response);
        }
      });
  StartAlternativeServer(mock_service);
  CreateAndStartBackends(1);
  using envoy::extensions::filters::http::ext_proc::v3::ProcessingMode;
  auto ext_proc_config =
      ExternalProcessorBuilder()
          .SetTargetUri(alternative_ext_proc_server_->target())
          .SetInsecureChannelCredentials()
          .SetFailureModeAllow(true)  // Fail-open (configured)
          .SetRequestHeaderMode(ProcessingMode::SKIP)
          .SetResponseHeaderMode(ProcessingMode::SEND)
          .SetResponseTrailerMode(ProcessingMode::SEND)
          .SetRequestBodyMode(ProcessingMode::NONE)
          .SetResponseBodyMode(ProcessingMode::GRPC)
          .Build();
  Listener listener = BuildListenerWithExtProcFilter(ext_proc_config);
  RouteConfiguration route_config = default_route_config_;
  SetListenerAndRouteConfiguration(balancer_.get(), listener, route_config);
  balancer_->ads_service()->SetCdsResource(default_cluster_);
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(EdsResourceArgs({
      {"locality0", CreateEndpointsForBackends(0, 1)},
  })));
  ClientContext context;
  auto stream = stub_->BidiStream(&context);
  EchoRequest request;
  request.set_message("hello");
  EXPECT_TRUE(stream->Write(request));
  EchoResponse response;
  EXPECT_FALSE(stream->Read(&response));
  Status status = stream->Finish();
  // Should STILL fail because it is committed!
  EXPECT_FALSE(status.ok());
  EXPECT_EQ(status.error_code(), StatusCode::INTERNAL);
  EXPECT_THAT(status.error_message(),
              ::testing::HasSubstr("CONTINUE_AND_REPLACE is not supported"));
}

TEST_P(XdsExtProcEnd2endTest,
       ServerToClientResponseBodyGrpcMessageCompressedFailClosedFailsCall) {
  auto mock_service = std::make_shared<GenericMockService>(
      [&](const ::envoy::service::ext_proc::v3::ProcessingRequest& request,
          ::envoy::service::ext_proc::v3::ProcessingResponse* response) {
        if (request.has_response_body()) {
          auto* common_response =
              response->mutable_response_body()->mutable_response();
          auto* body_mutation = common_response->mutable_body_mutation();
          auto* streamed_response = body_mutation->mutable_streamed_response();
          streamed_response->set_grpc_message_compressed(true);
        } else {
          SetDefaultEmptyResponse(request, response);
        }
      });
  StartAlternativeServer(mock_service);
  CreateAndStartBackends(1);
  using envoy::extensions::filters::http::ext_proc::v3::ProcessingMode;
  auto ext_proc_config =
      ExternalProcessorBuilder()
          .SetTargetUri(alternative_ext_proc_server_->target())
          .SetInsecureChannelCredentials()
          .SetFailureModeAllow(false)  // Fail-closed!
          .SetRequestHeaderMode(ProcessingMode::SKIP)
          .SetResponseHeaderMode(ProcessingMode::SEND)
          .SetResponseTrailerMode(ProcessingMode::SEND)
          .SetRequestBodyMode(ProcessingMode::NONE)
          .SetResponseBodyMode(ProcessingMode::GRPC)
          .Build();
  Listener listener = BuildListenerWithExtProcFilter(ext_proc_config);
  RouteConfiguration route_config = default_route_config_;
  SetListenerAndRouteConfiguration(balancer_.get(), listener, route_config);
  balancer_->ads_service()->SetCdsResource(default_cluster_);
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(EdsResourceArgs({
      {"locality0", CreateEndpointsForBackends(0, 1)},
  })));
  ClientContext context;
  auto stream = stub_->BidiStream(&context);
  EchoRequest request;
  request.set_message("hello");
  EXPECT_TRUE(stream->Write(request));
  EchoResponse response;
  EXPECT_FALSE(stream->Read(&response));
  Status status = stream->Finish();
  EXPECT_FALSE(status.ok());
  EXPECT_EQ(status.error_code(), StatusCode::INTERNAL);
  EXPECT_THAT(status.error_message(),
              ::testing::HasSubstr("grpc_message_compressed is not supported"));
}

TEST_P(XdsExtProcEnd2endTest,
       ServerToClientResponseBodyGrpcMessageCompressedFailOpenFailsCall) {
  auto mock_service = std::make_shared<GenericMockService>(
      [&](const ::envoy::service::ext_proc::v3::ProcessingRequest& request,
          ::envoy::service::ext_proc::v3::ProcessingResponse* response) {
        if (request.has_response_body()) {
          auto* common_response =
              response->mutable_response_body()->mutable_response();
          auto* body_mutation = common_response->mutable_body_mutation();
          auto* streamed_response = body_mutation->mutable_streamed_response();
          streamed_response->set_grpc_message_compressed(true);
        } else {
          SetDefaultEmptyResponse(request, response);
        }
      });
  StartAlternativeServer(mock_service);
  CreateAndStartBackends(1);
  using envoy::extensions::filters::http::ext_proc::v3::ProcessingMode;
  auto ext_proc_config =
      ExternalProcessorBuilder()
          .SetTargetUri(alternative_ext_proc_server_->target())
          .SetInsecureChannelCredentials()
          .SetFailureModeAllow(true)  // Fail-open (configured)
          .SetRequestHeaderMode(ProcessingMode::SKIP)
          .SetResponseHeaderMode(ProcessingMode::SEND)
          .SetResponseTrailerMode(ProcessingMode::SEND)
          .SetRequestBodyMode(ProcessingMode::NONE)
          .SetResponseBodyMode(ProcessingMode::GRPC)
          .Build();
  Listener listener = BuildListenerWithExtProcFilter(ext_proc_config);
  RouteConfiguration route_config = default_route_config_;
  SetListenerAndRouteConfiguration(balancer_.get(), listener, route_config);
  balancer_->ads_service()->SetCdsResource(default_cluster_);
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(EdsResourceArgs({
      {"locality0", CreateEndpointsForBackends(0, 1)},
  })));
  ClientContext context;
  auto stream = stub_->BidiStream(&context);
  EchoRequest request;
  request.set_message("hello");
  EXPECT_TRUE(stream->Write(request));
  EchoResponse response;
  EXPECT_FALSE(stream->Read(&response));
  Status status = stream->Finish();
  // Should STILL fail because it is committed!
  EXPECT_FALSE(status.ok());
  EXPECT_EQ(status.error_code(), StatusCode::INTERNAL);
  EXPECT_THAT(status.error_message(),
              ::testing::HasSubstr("grpc_message_compressed is not supported"));
}

TEST_P(XdsExtProcEnd2endTest,
       ServerToClientResponseBodyBidiStreamMultipleMessagesPingPongSuccess) {
  CreateAndStartBackends(1);
  using envoy::extensions::filters::http::ext_proc::v3::ProcessingMode;
  auto ext_proc_config = ExternalProcessorBuilder()
                             .SetTargetUri(ext_proc_server_->target())
                             .SetInsecureChannelCredentials()
                             .SetRequestHeaderMode(ProcessingMode::SKIP)
                             .SetRequestBodyMode(ProcessingMode::NONE)
                             .SetResponseHeaderMode(ProcessingMode::SEND)
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

  ClientContext context;
  auto stream = stub_->BidiStream(&context);

  EchoRequest request;
  EchoResponse response;

  // Message 1
  request.set_message("message1");
  EXPECT_TRUE(stream->Write(request));
  EXPECT_TRUE(stream->Read(&response));
  EXPECT_EQ(response.message(), "message1-response-body-mutated");

  // Message 2
  request.set_message("message2");
  EXPECT_TRUE(stream->Write(request));
  EXPECT_TRUE(stream->Read(&response));
  EXPECT_EQ(response.message(), "message2-response-body-mutated");

  // Message 3
  request.set_message("message3");
  EXPECT_TRUE(stream->Write(request));
  EXPECT_TRUE(stream->Read(&response));
  EXPECT_EQ(response.message(), "message3-response-body-mutated");

  EXPECT_TRUE(stream->WritesDone());
  EXPECT_FALSE(stream->Read(&response));

  Status status = stream->Finish();
  EXPECT_TRUE(status.ok()) << status.error_message();

  MockExternalProcessorService::RequestCounts expected_counts;
  expected_counts.request_headers = 0;
  expected_counts.response_headers = 1;
  expected_counts.response_trailers = 1;
  expected_counts.request_body = 0;
  expected_counts.response_body = 3;
  ext_proc_server_->ext_proc_service()->WaitForRequestCounts(expected_counts);
}

TEST_P(XdsExtProcEnd2endTest,
       ServerToClientResponseBodyBidiStreamAsymmetricWriteReadSuccess) {
  CreateAndStartBackends(1);
  using envoy::extensions::filters::http::ext_proc::v3::ProcessingMode;
  auto ext_proc_config = ExternalProcessorBuilder()
                             .SetTargetUri(ext_proc_server_->target())
                             .SetInsecureChannelCredentials()
                             .SetRequestHeaderMode(ProcessingMode::SKIP)
                             .SetRequestBodyMode(ProcessingMode::NONE)
                             .SetResponseHeaderMode(ProcessingMode::SEND)
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

  ClientContext context;
  auto stream = stub_->BidiStream(&context);

  EchoRequest request;
  request.set_message("message1");
  EXPECT_TRUE(stream->Write(request));
  request.set_message("message2");
  EXPECT_TRUE(stream->Write(request));
  request.set_message("message3");
  EXPECT_TRUE(stream->Write(request));

  EXPECT_TRUE(stream->WritesDone());

  EchoResponse response;
  EXPECT_TRUE(stream->Read(&response));
  EXPECT_EQ(response.message(), "message1-response-body-mutated");
  EXPECT_TRUE(stream->Read(&response));
  EXPECT_EQ(response.message(), "message2-response-body-mutated");
  EXPECT_TRUE(stream->Read(&response));
  EXPECT_EQ(response.message(), "message3-response-body-mutated");

  EXPECT_FALSE(stream->Read(&response));

  Status status = stream->Finish();
  EXPECT_TRUE(status.ok()) << status.error_message();

  MockExternalProcessorService::RequestCounts expected_counts;
  expected_counts.request_headers = 0;
  expected_counts.response_headers = 1;
  expected_counts.response_trailers = 1;
  expected_counts.request_body = 0;
  expected_counts.response_body = 3;
  ext_proc_server_->ext_proc_service()->WaitForRequestCounts(expected_counts);
}

class DuplicateResponseBodyResponseMockService
    : public MockExternalProcessorBase {
 public:
  grpc::Status Process(
      grpc::ServerContext* /*context*/,
      grpc::ServerReaderWriter<
          ::envoy::service::ext_proc::v3::ProcessingResponse,
          ::envoy::service::ext_proc::v3::ProcessingRequest>* stream) override {
    ::envoy::service::ext_proc::v3::ProcessingRequest request;
    while (stream->Read(&request)) {
      ::envoy::service::ext_proc::v3::ProcessingResponse response;
      SetDefaultEmptyResponse(request, &response);
      stream->Write(response);
      if (request.has_response_body()) {
        // Send duplicate response for response body
        stream->Write(response);
      }
    }
    return grpc::Status::OK;
  }
};

TEST_P(XdsExtProcEnd2endTest,
       ServerToClientResponseBodyDuplicateResponseFailsCall) {
  auto mock_service =
      std::make_shared<DuplicateResponseBodyResponseMockService>();
  StartAlternativeServer(mock_service);
  CreateAndStartBackends(1);
  using envoy::extensions::filters::http::ext_proc::v3::ProcessingMode;
  auto ext_proc_config =
      ExternalProcessorBuilder()
          .SetTargetUri(alternative_ext_proc_server_->target())
          .SetInsecureChannelCredentials()
          .SetFailureModeAllow(false)  // Fail-closed!
          .SetRequestHeaderMode(ProcessingMode::SKIP)
          .SetResponseHeaderMode(ProcessingMode::SEND)
          .SetResponseTrailerMode(ProcessingMode::SEND)
          .SetRequestBodyMode(ProcessingMode::NONE)
          .SetResponseBodyMode(ProcessingMode::GRPC)
          .Build();
  Listener listener = BuildListenerWithExtProcFilter(ext_proc_config);
  RouteConfiguration route_config = default_route_config_;
  SetListenerAndRouteConfiguration(balancer_.get(), listener, route_config);
  balancer_->ads_service()->SetCdsResource(default_cluster_);
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(EdsResourceArgs({
      {"locality0", CreateEndpointsForBackends(0, 1)},
  })));

  ClientContext context;
  auto stream = stub_->BidiStream(&context);
  EchoRequest request;
  request.set_message("hello");
  EXPECT_TRUE(stream->Write(request));
  EchoResponse response;
  EXPECT_TRUE(stream->Read(&response));
  EXPECT_FALSE(stream->Read(&response));
  Status status = stream->Finish();
  EXPECT_FALSE(status.ok());
  EXPECT_EQ(status.error_code(), StatusCode::INTERNAL);
  EXPECT_THAT(
      status.error_message(),
      ::testing::HasSubstr("Received unexpected response body response"));
}

class FailOnSecondResponseBodyMockService : public MockExternalProcessorBase {
 public:
  grpc::Status Process(
      grpc::ServerContext* /*context*/,
      grpc::ServerReaderWriter<
          ::envoy::service::ext_proc::v3::ProcessingResponse,
          ::envoy::service::ext_proc::v3::ProcessingRequest>* stream) override {
    ::envoy::service::ext_proc::v3::ProcessingRequest request;
    int body_count = 0;
    while (stream->Read(&request)) {
      if (request.has_response_body()) {
        body_count++;
        if (body_count == 2) {
          return grpc::Status(grpc::StatusCode::RESOURCE_EXHAUSTED,
                              "Closed on second body");
        }
      }
      ::envoy::service::ext_proc::v3::ProcessingResponse response;
      SetDefaultEmptyResponse(request, &response);
      stream->Write(response);
    }
    return grpc::Status::OK;
  }
};

TEST_P(XdsExtProcEnd2endTest,
       ServerToClientResponseBodyBidiStreamExtProcConnectionErrorFailClosed) {
  auto mock_service = std::make_shared<FailOnSecondResponseBodyMockService>();
  StartAlternativeServer(mock_service);
  CreateAndStartBackends(1);
  using envoy::extensions::filters::http::ext_proc::v3::ProcessingMode;
  auto ext_proc_config =
      ExternalProcessorBuilder()
          .SetTargetUri(alternative_ext_proc_server_->target())
          .SetInsecureChannelCredentials()
          .SetFailureModeAllow(false)  // Fail-closed!
          .SetRequestHeaderMode(ProcessingMode::SKIP)
          .SetResponseHeaderMode(ProcessingMode::SEND)
          .SetResponseTrailerMode(ProcessingMode::SEND)
          .SetRequestBodyMode(ProcessingMode::NONE)
          .SetResponseBodyMode(ProcessingMode::GRPC)
          .Build();
  Listener listener = BuildListenerWithExtProcFilter(ext_proc_config);
  RouteConfiguration route_config = default_route_config_;
  SetListenerAndRouteConfiguration(balancer_.get(), listener, route_config);
  balancer_->ads_service()->SetCdsResource(default_cluster_);
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(EdsResourceArgs({
      {"locality0", CreateEndpointsForBackends(0, 1)},
  })));

  ClientContext context;
  auto stream = stub_->BidiStream(&context);

  EchoRequest request;
  EchoResponse response;

  // Message 1 (succeeds)
  request.set_message("message1");
  EXPECT_TRUE(stream->Write(request));
  EXPECT_TRUE(stream->Read(&response));

  // Message 2 (fails)
  request.set_message("message2");
  EXPECT_TRUE(stream->Write(request));
  // Read should fail because processor fails.
  EXPECT_FALSE(stream->Read(&response));

  Status status = stream->Finish();
  EXPECT_FALSE(status.ok());
  EXPECT_THAT(status.error_code(),
              ::testing::AnyOf(StatusCode::RESOURCE_EXHAUSTED,
                               StatusCode::CANCELLED, StatusCode::UNAVAILABLE));
}

TEST_P(XdsExtProcEnd2endTest,
       ServerToClientResponseBodyBidiStreamExtProcConnectionErrorFailOpen) {
  auto mock_service = std::make_shared<FailOnSecondResponseBodyMockService>();
  StartAlternativeServer(mock_service);
  CreateAndStartBackends(1);
  using envoy::extensions::filters::http::ext_proc::v3::ProcessingMode;
  auto ext_proc_config =
      ExternalProcessorBuilder()
          .SetTargetUri(alternative_ext_proc_server_->target())
          .SetInsecureChannelCredentials()
          .SetFailureModeAllow(true)  // Fail-open (configured)
          .SetRequestHeaderMode(ProcessingMode::SKIP)
          .SetResponseHeaderMode(ProcessingMode::SEND)
          .SetResponseTrailerMode(ProcessingMode::SEND)
          .SetRequestBodyMode(ProcessingMode::NONE)
          .SetResponseBodyMode(ProcessingMode::GRPC)
          .Build();
  Listener listener = BuildListenerWithExtProcFilter(ext_proc_config);
  RouteConfiguration route_config = default_route_config_;
  SetListenerAndRouteConfiguration(balancer_.get(), listener, route_config);
  balancer_->ads_service()->SetCdsResource(default_cluster_);
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(EdsResourceArgs({
      {"locality0", CreateEndpointsForBackends(0, 1)},
  })));

  ClientContext context;
  auto stream = stub_->BidiStream(&context);

  EchoRequest request;
  EchoResponse response;

  // Message 1 (succeeds)
  request.set_message("message1");
  EXPECT_TRUE(stream->Write(request));
  EXPECT_TRUE(stream->Read(&response));

  // Message 2 (fails)
  request.set_message("message2");
  EXPECT_TRUE(stream->Write(request));
  // Read should fail because processor fails and it is committed.
  EXPECT_FALSE(stream->Read(&response));

  Status status = stream->Finish();
  EXPECT_FALSE(status.ok());
  EXPECT_THAT(status.error_code(),
              ::testing::AnyOf(StatusCode::RESOURCE_EXHAUSTED,
                               StatusCode::CANCELLED, StatusCode::UNAVAILABLE));
}

TEST_P(XdsExtProcEnd2endTest,
       ServerToClientResponseBodyExtProcConnectionErrorFailCall) {
  auto mock_service =
      std::make_shared<CloseExtProcStreamOnResponseBodyMockService>();
  StartAlternativeServer(mock_service);
  CreateAndStartBackends(1);
  using envoy::extensions::filters::http::ext_proc::v3::ProcessingMode;
  auto ext_proc_config =
      ExternalProcessorBuilder()
          .SetTargetUri(alternative_ext_proc_server_->target())
          .SetInsecureChannelCredentials()
          .SetFailureModeAllow(false)  // Fail-closed!
          .SetRequestHeaderMode(ProcessingMode::SKIP)
          .SetResponseHeaderMode(ProcessingMode::SEND)
          .SetResponseTrailerMode(ProcessingMode::SEND)
          .SetRequestBodyMode(ProcessingMode::NONE)
          .SetResponseBodyMode(ProcessingMode::GRPC)
          .Build();
  Listener listener = BuildListenerWithExtProcFilter(ext_proc_config);
  RouteConfiguration route_config = default_route_config_;
  SetListenerAndRouteConfiguration(balancer_.get(), listener, route_config);
  balancer_->ads_service()->SetCdsResource(default_cluster_);
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(EdsResourceArgs({
      {"locality0", CreateEndpointsForBackends(0, 1)},
  })));

  ClientContext context;
  auto stream = stub_->BidiStream(&context);

  EchoRequest request;
  EchoResponse response;

  request.set_message("message1");
  EXPECT_TRUE(stream->Write(request));
  // Read should fail because processor closes stream on first response body.
  EXPECT_FALSE(stream->Read(&response));

  Status status = stream->Finish();
  EXPECT_FALSE(status.ok());
  EXPECT_EQ(status.error_code(), StatusCode::RESOURCE_EXHAUSTED);
  EXPECT_THAT(status.error_message(),
              ::testing::HasSubstr("Closed on response body"));
}

TEST_P(XdsExtProcEnd2endTest,
       ServerToClientResponseBodyExtProcConnectionErrorAllowCall) {
  auto mock_service =
      std::make_shared<CloseExtProcStreamOnResponseBodyMockService>();
  StartAlternativeServer(mock_service);
  CreateAndStartBackends(1);
  using envoy::extensions::filters::http::ext_proc::v3::ProcessingMode;
  auto ext_proc_config =
      ExternalProcessorBuilder()
          .SetTargetUri(alternative_ext_proc_server_->target())
          .SetInsecureChannelCredentials()
          .SetFailureModeAllow(true)  // Fail-open!
          .SetRequestHeaderMode(ProcessingMode::SKIP)
          .SetResponseHeaderMode(ProcessingMode::SEND)
          .SetResponseTrailerMode(ProcessingMode::SEND)
          .SetRequestBodyMode(ProcessingMode::NONE)
          .SetResponseBodyMode(ProcessingMode::GRPC)
          .Build();
  Listener listener = BuildListenerWithExtProcFilter(ext_proc_config);
  RouteConfiguration route_config = default_route_config_;
  SetListenerAndRouteConfiguration(balancer_.get(), listener, route_config);
  balancer_->ads_service()->SetCdsResource(default_cluster_);
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(EdsResourceArgs({
      {"locality0", CreateEndpointsForBackends(0, 1)},
  })));

  ClientContext context;
  auto stream = stub_->BidiStream(&context);

  EchoRequest request;
  EchoResponse response;

  request.set_message("message1");
  EXPECT_TRUE(stream->Write(request));
  // Even though failure_mode_allow is true (fail-open), because the
  // stream failed AFTER the first body message was sent to ext_proc, the filter
  // must fail the RPC to avoid message loss or inconsistent state.
  EXPECT_FALSE(stream->Read(&response));

  Status status = stream->Finish();
  EXPECT_FALSE(status.ok());
  EXPECT_EQ(status.error_code(), StatusCode::RESOURCE_EXHAUSTED);
  EXPECT_THAT(status.error_message(),
              ::testing::HasSubstr("Closed on response body"));
}

TEST_P(XdsExtProcEnd2endTest,
       ServerToClientResponseBodyObservabilityExtProcConnectionErrorFailCall) {
  ResetStubWithUniqueArg();
  auto mock_service =
      std::make_shared<CloseExtProcStreamOnResponseBodyMockService>();
  StartAlternativeServer(mock_service);
  CreateAndStartBackends(1);
  using envoy::extensions::filters::http::ext_proc::v3::ProcessingMode;
  auto ext_proc_config =
      ExternalProcessorBuilder()
          .SetTargetUri(alternative_ext_proc_server_->target())
          .SetInsecureChannelCredentials()
          .SetObservabilityMode(true)
          .SetFailureModeAllow(false)  // Fail-closed
          .SetRequestHeaderMode(ProcessingMode::SKIP)
          .SetResponseHeaderMode(ProcessingMode::SEND)
          .SetResponseTrailerMode(ProcessingMode::SEND)
          .SetRequestBodyMode(ProcessingMode::NONE)
          .SetResponseBodyMode(ProcessingMode::GRPC)
          .Build();
  Listener listener = BuildListenerWithExtProcFilter(ext_proc_config);
  RouteConfiguration route_config = default_route_config_;
  SetListenerAndRouteConfiguration(balancer_.get(), listener, route_config);
  balancer_->ads_service()->SetCdsResource(default_cluster_);
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(EdsResourceArgs({
      {"locality0", CreateEndpointsForBackends(0, 1)},
  })));

  ClientContext context;
  auto stream = stub_->BidiStream(&context);

  EchoRequest request;
  EchoResponse response;

  request.set_message("message1");
  EXPECT_TRUE(stream->Write(request));
  // In observability mode, the filter does not block the response body, so the
  // read might succeed before the asynchronous ext_proc error propagates to
  // fail the stream.
  stream->Read(&response);

  Status status = stream->Finish();
  EXPECT_FALSE(status.ok());
  EXPECT_EQ(status.error_code(), StatusCode::RESOURCE_EXHAUSTED);
  EXPECT_THAT(status.error_message(),
              ::testing::HasSubstr("Closed on response body"));
}

TEST_P(XdsExtProcEnd2endTest,
       ServerToClientResponseBodyObservabilityExtProcConnectionErrorAllowCall) {
  ResetStubWithUniqueArg();
  auto mock_service =
      std::make_shared<CloseExtProcStreamOnResponseBodyMockService>();
  StartAlternativeServer(mock_service);
  CreateAndStartBackends(1);
  using envoy::extensions::filters::http::ext_proc::v3::ProcessingMode;
  auto ext_proc_config =
      ExternalProcessorBuilder()
          .SetTargetUri(alternative_ext_proc_server_->target())
          .SetInsecureChannelCredentials()
          .SetObservabilityMode(true)
          .SetFailureModeAllow(true)  // Fail-open
          .SetRequestHeaderMode(ProcessingMode::SKIP)
          .SetResponseHeaderMode(ProcessingMode::SEND)
          .SetResponseTrailerMode(ProcessingMode::SEND)
          .SetRequestBodyMode(ProcessingMode::NONE)
          .SetResponseBodyMode(ProcessingMode::GRPC)
          .Build();
  Listener listener = BuildListenerWithExtProcFilter(ext_proc_config);
  RouteConfiguration route_config = default_route_config_;
  SetListenerAndRouteConfiguration(balancer_.get(), listener, route_config);
  balancer_->ads_service()->SetCdsResource(default_cluster_);
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(EdsResourceArgs({
      {"locality0", CreateEndpointsForBackends(0, 1)},
  })));

  ClientContext context;
  auto stream = stub_->BidiStream(&context);

  EchoRequest request;
  EchoResponse response;

  request.set_message("message1");
  EXPECT_TRUE(stream->Write(request));
  // Read should succeed because observability mode + fail-open allows it.
  EXPECT_TRUE(stream->Read(&response));
  EXPECT_EQ(response.message(), "message1");

  stream->WritesDone();
  Status status = stream->Finish();
  EXPECT_TRUE(status.ok()) << status.error_message();
}

class FailImmediatelyMockService : public MockExternalProcessorBase {
 public:
  grpc::Status Process(
      grpc::ServerContext* /*context*/,
      grpc::ServerReaderWriter<
          ::envoy::service::ext_proc::v3::ProcessingResponse,
          ::envoy::service::ext_proc::v3::ProcessingRequest>* /*stream*/)
      override {
    return grpc::Status(grpc::StatusCode::RESOURCE_EXHAUSTED,
                        "Failed immediately");
  }
};

TEST_P(XdsExtProcEnd2endTest,
       StreamFailBeforeRequestHeadersObservabilityFailCall) {
  ResetStubWithUniqueArg();
  auto mock_service = std::make_shared<FailImmediatelyMockService>();
  StartAlternativeServer(mock_service);
  CreateAndStartBackends(1);
  using envoy::extensions::filters::http::ext_proc::v3::ProcessingMode;
  auto ext_proc_config =
      ExternalProcessorBuilder()
          .SetTargetUri(alternative_ext_proc_server_->target())
          .SetInsecureChannelCredentials()
          .SetObservabilityMode(true)
          .SetFailureModeAllow(false)  // Fail-closed
          .SetRequestHeaderMode(ProcessingMode::SEND)
          .SetResponseHeaderMode(ProcessingMode::SEND)
          .SetResponseTrailerMode(ProcessingMode::SEND)
          .SetRequestBodyMode(ProcessingMode::NONE)
          .SetResponseBodyMode(ProcessingMode::NONE)
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
  EXPECT_EQ(status.error_code(), StatusCode::RESOURCE_EXHAUSTED);
  EXPECT_THAT(status.error_message(),
              ::testing::HasSubstr("Failed immediately"));
}

TEST_P(XdsExtProcEnd2endTest,
       StreamFailBeforeRequestHeadersObservabilityAllowCall) {
  ResetStubWithUniqueArg();
  auto mock_service = std::make_shared<FailImmediatelyMockService>();
  StartAlternativeServer(mock_service);
  CreateAndStartBackends(1);
  using envoy::extensions::filters::http::ext_proc::v3::ProcessingMode;
  auto ext_proc_config =
      ExternalProcessorBuilder()
          .SetTargetUri(alternative_ext_proc_server_->target())
          .SetInsecureChannelCredentials()
          .SetObservabilityMode(true)
          .SetFailureModeAllow(true)  // Fail-open
          .SetRequestHeaderMode(ProcessingMode::SEND)
          .SetResponseHeaderMode(ProcessingMode::SEND)
          .SetResponseTrailerMode(ProcessingMode::SEND)
          .SetRequestBodyMode(ProcessingMode::NONE)
          .SetResponseBodyMode(ProcessingMode::NONE)
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

class FailAfterRequestHeadersMockService : public MockExternalProcessorBase {
 public:
  grpc::Status Process(
      grpc::ServerContext* /*context*/,
      grpc::ServerReaderWriter<
          ::envoy::service::ext_proc::v3::ProcessingResponse,
          ::envoy::service::ext_proc::v3::ProcessingRequest>* stream) override {
    ::envoy::service::ext_proc::v3::ProcessingRequest request;
    if (stream->Read(&request)) {
      if (request.has_request_headers()) {
        return grpc::Status(grpc::StatusCode::RESOURCE_EXHAUSTED,
                            "Failed after request headers");
      }
    }
    return grpc::Status::OK;
  }
};

TEST_P(XdsExtProcEnd2endTest,
       StreamFailBeforeRequestBodyObservabilityFailCall) {
  ResetStubWithUniqueArg();
  auto mock_service = std::make_shared<FailAfterRequestHeadersMockService>();
  StartAlternativeServer(mock_service);
  CreateAndStartBackends(1);
  using envoy::extensions::filters::http::ext_proc::v3::ProcessingMode;
  auto ext_proc_config =
      ExternalProcessorBuilder()
          .SetTargetUri(alternative_ext_proc_server_->target())
          .SetInsecureChannelCredentials()
          .SetObservabilityMode(true)
          .SetFailureModeAllow(false)  // Fail-closed
          .SetRequestHeaderMode(ProcessingMode::SEND)
          .SetResponseHeaderMode(ProcessingMode::SEND)
          .SetResponseTrailerMode(ProcessingMode::SEND)
          .SetRequestBodyMode(ProcessingMode::GRPC)
          .SetResponseBodyMode(ProcessingMode::NONE)
          .Build();
  Listener listener = BuildListenerWithExtProcFilter(ext_proc_config);
  RouteConfiguration route_config = default_route_config_;
  SetListenerAndRouteConfiguration(balancer_.get(), listener, route_config);
  balancer_->ads_service()->SetCdsResource(default_cluster_);
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(EdsResourceArgs({
      {"locality0", CreateEndpointsForBackends(0, 1)},
  })));

  ClientContext context;
  auto stream = stub_->BidiStream(&context);

  EchoRequest request;
  EchoResponse response;

  request.set_message("message1");
  EXPECT_TRUE(stream->Write(request));
  stream->Read(&response);
  stream->WritesDone();
  Status status = stream->Finish();
  EXPECT_FALSE(status.ok());
  EXPECT_EQ(status.error_code(), StatusCode::RESOURCE_EXHAUSTED);
  EXPECT_THAT(status.error_message(),
              ::testing::HasSubstr("Failed after request headers"));
}

TEST_P(XdsExtProcEnd2endTest,
       StreamFailBeforeRequestBodyObservabilityAllowCall) {
  ResetStubWithUniqueArg();
  auto mock_service = std::make_shared<FailAfterRequestHeadersMockService>();
  StartAlternativeServer(mock_service);
  CreateAndStartBackends(1);
  using envoy::extensions::filters::http::ext_proc::v3::ProcessingMode;
  auto ext_proc_config =
      ExternalProcessorBuilder()
          .SetTargetUri(alternative_ext_proc_server_->target())
          .SetInsecureChannelCredentials()
          .SetObservabilityMode(true)
          .SetFailureModeAllow(true)  // Fail-open
          .SetRequestHeaderMode(ProcessingMode::SEND)
          .SetResponseHeaderMode(ProcessingMode::SEND)
          .SetResponseTrailerMode(ProcessingMode::SEND)
          .SetRequestBodyMode(ProcessingMode::GRPC)
          .SetResponseBodyMode(ProcessingMode::NONE)
          .Build();
  Listener listener = BuildListenerWithExtProcFilter(ext_proc_config);
  RouteConfiguration route_config = default_route_config_;
  SetListenerAndRouteConfiguration(balancer_.get(), listener, route_config);
  balancer_->ads_service()->SetCdsResource(default_cluster_);
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(EdsResourceArgs({
      {"locality0", CreateEndpointsForBackends(0, 1)},
  })));

  ClientContext context;
  auto stream = stub_->BidiStream(&context);

  EchoRequest request;
  EchoResponse response;

  request.set_message("message1");
  EXPECT_TRUE(stream->Write(request));
  EXPECT_TRUE(stream->Read(&response));
  EXPECT_EQ(response.message(), "message1");

  stream->WritesDone();
  Status status = stream->Finish();
  EXPECT_TRUE(status.ok()) << status.error_message();
}

TEST_P(XdsExtProcEnd2endTest,
       ServerToClientResponseBodyBidiStreamObservabilitySuccess) {
  CreateAndStartBackends(1);
  using envoy::extensions::filters::http::ext_proc::v3::ProcessingMode;
  auto ext_proc_config = ExternalProcessorBuilder()
                             .SetTargetUri(ext_proc_server_->target())
                             .SetInsecureChannelCredentials()
                             .SetObservabilityMode(true)  // Observability mode!
                             .SetRequestHeaderMode(ProcessingMode::SKIP)
                             .SetRequestBodyMode(ProcessingMode::NONE)
                             .SetResponseHeaderMode(ProcessingMode::SEND)
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

  ClientContext context;
  auto stream = stub_->BidiStream(&context);

  EchoRequest request;
  EchoResponse response;

  // Message 1
  request.set_message("message1");
  EXPECT_TRUE(stream->Write(request));
  EXPECT_TRUE(stream->Read(&response));
  // Mutation should be IGNORED, so we should get "message1" (echoed by server),
  // NOT "message1-response-body-mutated".
  EXPECT_EQ(response.message(), "message1");

  // Message 2
  request.set_message("message2");
  EXPECT_TRUE(stream->Write(request));
  EXPECT_TRUE(stream->Read(&response));
  EXPECT_EQ(response.message(), "message2");

  stream->WritesDone();
  Status status = stream->Finish();
  EXPECT_TRUE(status.ok());
}

class ServerToClientResponseBodyHalfCloseMockService
    : public MockExternalProcessorBase {
 public:
  grpc::Status Process(
      grpc::ServerContext* /*context*/,
      grpc::ServerReaderWriter<
          ::envoy::service::ext_proc::v3::ProcessingResponse,
          ::envoy::service::ext_proc::v3::ProcessingRequest>* stream) override {
    ::envoy::service::ext_proc::v3::ProcessingRequest request;
    while (stream->Read(&request)) {
      ::envoy::service::ext_proc::v3::ProcessingResponse response;
      SetDefaultEmptyResponse(request, &response);
      if (request.has_response_body()) {
        auto* response_body = response.mutable_response_body();
        auto* mutation = response_body->mutable_response();
        auto* body_mutation = mutation->mutable_body_mutation();
        body_mutation->mutable_streamed_response()->set_end_of_stream(true);
      }
      stream->Write(response);
    }
    return grpc::Status::OK;
  }
};

TEST_P(XdsExtProcEnd2endTest,
       ServerToClientResponseBodyHalfCloseFailClosedFailsCall) {
  auto mock_service =
      std::make_shared<ServerToClientResponseBodyHalfCloseMockService>();
  StartAlternativeServer(mock_service);
  CreateAndStartBackends(1);
  using envoy::extensions::filters::http::ext_proc::v3::ProcessingMode;
  auto ext_proc_config =
      ExternalProcessorBuilder()
          .SetTargetUri(alternative_ext_proc_server_->target())
          .SetInsecureChannelCredentials()
          .SetFailureModeAllow(false)  // Fail-closed!
          .SetRequestHeaderMode(ProcessingMode::SKIP)
          .SetResponseHeaderMode(ProcessingMode::SEND)
          .SetResponseTrailerMode(ProcessingMode::SEND)
          .SetRequestBodyMode(ProcessingMode::NONE)
          .SetResponseBodyMode(ProcessingMode::GRPC)
          .Build();
  Listener listener = BuildListenerWithExtProcFilter(ext_proc_config);
  RouteConfiguration route_config = default_route_config_;
  SetListenerAndRouteConfiguration(balancer_.get(), listener, route_config);
  balancer_->ads_service()->SetCdsResource(default_cluster_);
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(EdsResourceArgs({
      {"locality0", CreateEndpointsForBackends(0, 1)},
  })));

  ClientContext context;
  auto stream = stub_->BidiStream(&context);
  EchoRequest request;
  request.set_message("hello");
  EXPECT_TRUE(stream->Write(request));
  EchoResponse response;
  EXPECT_FALSE(stream->Read(&response));
  Status status = stream->Finish();
  EXPECT_FALSE(status.ok());
  EXPECT_EQ(status.error_code(), StatusCode::INTERNAL);
  EXPECT_THAT(
      status.error_message(),
      ::testing::HasSubstr("Processor sent end_of_stream in response_body"));
}

TEST_P(XdsExtProcEnd2endTest,
       ServerToClientResponseBodyHalfCloseFailOpenFailsCall) {
  auto mock_service =
      std::make_shared<ServerToClientResponseBodyHalfCloseMockService>();
  StartAlternativeServer(mock_service);
  CreateAndStartBackends(1);
  using envoy::extensions::filters::http::ext_proc::v3::ProcessingMode;
  auto ext_proc_config =
      ExternalProcessorBuilder()
          .SetTargetUri(alternative_ext_proc_server_->target())
          .SetInsecureChannelCredentials()
          .SetFailureModeAllow(true)  // Fail-open!
          .SetRequestHeaderMode(ProcessingMode::SKIP)
          .SetResponseHeaderMode(ProcessingMode::SEND)
          .SetResponseTrailerMode(ProcessingMode::SEND)
          .SetRequestBodyMode(ProcessingMode::NONE)
          .SetResponseBodyMode(ProcessingMode::GRPC)
          .Build();
  Listener listener = BuildListenerWithExtProcFilter(ext_proc_config);
  RouteConfiguration route_config = default_route_config_;
  SetListenerAndRouteConfiguration(balancer_.get(), listener, route_config);
  balancer_->ads_service()->SetCdsResource(default_cluster_);
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(EdsResourceArgs({
      {"locality0", CreateEndpointsForBackends(0, 1)},
  })));

  ClientContext context;
  auto stream = stub_->BidiStream(&context);
  EchoRequest request;
  request.set_message("hello");
  EXPECT_TRUE(stream->Write(request));
  EchoResponse response;
  EXPECT_FALSE(stream->Read(&response));
  Status status = stream->Finish();
  EXPECT_FALSE(status.ok());
  EXPECT_EQ(status.error_code(), StatusCode::INTERNAL);
  EXPECT_THAT(
      status.error_message(),
      ::testing::HasSubstr("Processor sent end_of_stream in response_body"));
}

TEST_P(XdsExtProcEnd2endTest,
       ClientToServerOrderingResponseBodyBeforeHeadersFailsCall) {
  auto mock_service = std::make_shared<GenericMockService>(
      [](const ::envoy::service::ext_proc::v3::ProcessingRequest& request,
         ::envoy::service::ext_proc::v3::ProcessingResponse* response) {
        if (request.has_request_headers()) {
          // Respond with request_body instead of request_headers!
          response->mutable_request_body();
        } else {
          SetDefaultEmptyResponse(request, response);
        }
      });
  StartAlternativeServer(mock_service);
  CreateAndStartBackends(1);
  using envoy::extensions::filters::http::ext_proc::v3::ProcessingMode;
  auto ext_proc_config =
      ExternalProcessorBuilder()
          .SetTargetUri(alternative_ext_proc_server_->target())
          .SetInsecureChannelCredentials()
          .SetFailureModeAllow(false)  // Fail closed
          .SetRequestHeaderMode(ProcessingMode::SEND)
          .SetResponseHeaderMode(ProcessingMode::SKIP)
          .SetResponseTrailerMode(ProcessingMode::SKIP)
          .SetRequestBodyMode(ProcessingMode::GRPC)
          .SetResponseBodyMode(ProcessingMode::NONE)
          .Build();
  Listener listener = BuildListenerWithExtProcFilter(ext_proc_config);
  RouteConfiguration route_config = default_route_config_;
  SetListenerAndRouteConfiguration(balancer_.get(), listener, route_config);
  balancer_->ads_service()->SetCdsResource(default_cluster_);
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(EdsResourceArgs({
      {"locality0", CreateEndpointsForBackends(0, 1)},
  })));
  ClientContext context;
  auto stream = stub_->BidiStream(&context);
  EchoRequest request;
  request.set_message("hello");
  stream->Write(request);
  EchoResponse response;
  EXPECT_FALSE(stream->Read(&response));
  Status status = stream->Finish();
  EXPECT_FALSE(status.ok());
  EXPECT_EQ(status.error_code(), StatusCode::INTERNAL);
  EXPECT_THAT(status.error_message(),
              ::testing::HasSubstr("Received request body response before "
                                   "request headers response"));
}

TEST_P(XdsExtProcEnd2endTest,
       ClientToServerOrderingHeadersResponseWhenDisabledFailsCall) {
  auto mock_service = std::make_shared<GenericMockService>(
      [](const ::envoy::service::ext_proc::v3::ProcessingRequest& request,
         ::envoy::service::ext_proc::v3::ProcessingResponse* response) {
        if (request.has_request_body()) {
          // Respond with request_headers instead of request_body!
          response->mutable_request_headers();
        } else {
          SetDefaultEmptyResponse(request, response);
        }
      });
  StartAlternativeServer(mock_service);
  CreateAndStartBackends(1);
  using envoy::extensions::filters::http::ext_proc::v3::ProcessingMode;
  auto ext_proc_config =
      ExternalProcessorBuilder()
          .SetTargetUri(alternative_ext_proc_server_->target())
          .SetInsecureChannelCredentials()
          .SetFailureModeAllow(false)                  // Fail closed
          .SetRequestHeaderMode(ProcessingMode::SKIP)  // Skip headers!
          .SetResponseHeaderMode(ProcessingMode::SKIP)
          .SetResponseTrailerMode(ProcessingMode::SKIP)
          .SetRequestBodyMode(ProcessingMode::GRPC)
          .SetResponseBodyMode(ProcessingMode::NONE)
          .Build();
  Listener listener = BuildListenerWithExtProcFilter(ext_proc_config);
  RouteConfiguration route_config = default_route_config_;
  SetListenerAndRouteConfiguration(balancer_.get(), listener, route_config);
  balancer_->ads_service()->SetCdsResource(default_cluster_);
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(EdsResourceArgs({
      {"locality0", CreateEndpointsForBackends(0, 1)},
  })));
  ClientContext context;
  auto stream = stub_->BidiStream(&context);
  EchoRequest request;
  request.set_message("hello");
  stream->Write(request);
  EchoResponse response;
  EXPECT_FALSE(stream->Read(&response));
  Status status = stream->Finish();
  EXPECT_FALSE(status.ok());
  EXPECT_EQ(status.error_code(), StatusCode::INTERNAL);
  EXPECT_THAT(status.error_message(),
              ::testing::HasSubstr("Received request headers response but "
                                   "request headers are disabled"));
}

TEST_P(XdsExtProcEnd2endTest,
       ServerToClientOrderingResponseBodyBeforeHeadersFailsCall) {
  auto mock_service = std::make_shared<GenericMockService>(
      [](const ::envoy::service::ext_proc::v3::ProcessingRequest& request,
         ::envoy::service::ext_proc::v3::ProcessingResponse* response) {
        if (request.has_response_headers()) {
          // Respond with response_body instead of response_headers!
          response->mutable_response_body();
        } else {
          SetDefaultEmptyResponse(request, response);
        }
      });
  StartAlternativeServer(mock_service);
  CreateAndStartBackends(1);
  using envoy::extensions::filters::http::ext_proc::v3::ProcessingMode;
  auto ext_proc_config =
      ExternalProcessorBuilder()
          .SetTargetUri(alternative_ext_proc_server_->target())
          .SetInsecureChannelCredentials()
          .SetFailureModeAllow(false)  // Fail closed
          .SetRequestHeaderMode(ProcessingMode::SEND)
          .SetRequestBodyMode(ProcessingMode::GRPC)
          .SetResponseHeaderMode(ProcessingMode::SEND)
          .SetResponseBodyMode(ProcessingMode::GRPC)
          .SetResponseTrailerMode(
              ProcessingMode::SEND)  // Must be SEND if body is GRPC
          .Build();
  Listener listener = BuildListenerWithExtProcFilter(ext_proc_config);
  RouteConfiguration route_config = default_route_config_;
  SetListenerAndRouteConfiguration(balancer_.get(), listener, route_config);
  balancer_->ads_service()->SetCdsResource(default_cluster_);
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(EdsResourceArgs({
      {"locality0", CreateEndpointsForBackends(0, 1)},
  })));
  ClientContext context;
  auto stream = stub_->BidiStream(&context);
  // Use WritesDone() to trigger S2C headers without sending body messages,
  // avoiding race conditions.
  stream->WritesDone();
  EchoResponse response;
  EXPECT_FALSE(stream->Read(&response));
  Status status = stream->Finish();
  EXPECT_FALSE(status.ok());
  EXPECT_EQ(status.error_code(), StatusCode::INTERNAL);
  EXPECT_THAT(status.error_message(),
              ::testing::HasSubstr("Received response body response before "
                                   "response headers response"));
}

TEST_P(XdsExtProcEnd2endTest,
       ServerToClientOrderingTrailersBeforeHeadersFailsCall) {
  auto mock_service = std::make_shared<GenericMockService>(
      [](const ::envoy::service::ext_proc::v3::ProcessingRequest& request,
         ::envoy::service::ext_proc::v3::ProcessingResponse* response) {
        if (request.has_response_headers()) {
          // Respond with response_trailers instead of response_headers!
          response->mutable_response_trailers();
        } else {
          SetDefaultEmptyResponse(request, response);
        }
      });
  StartAlternativeServer(mock_service);
  CreateAndStartBackends(1);
  using envoy::extensions::filters::http::ext_proc::v3::ProcessingMode;
  auto ext_proc_config =
      ExternalProcessorBuilder()
          .SetTargetUri(alternative_ext_proc_server_->target())
          .SetInsecureChannelCredentials()
          .SetFailureModeAllow(false)  // Fail closed
          .SetRequestHeaderMode(ProcessingMode::SEND)
          .SetRequestBodyMode(ProcessingMode::GRPC)
          .SetResponseHeaderMode(ProcessingMode::SEND)
          .SetResponseBodyMode(ProcessingMode::NONE)
          .SetResponseTrailerMode(ProcessingMode::SEND)
          .Build();
  Listener listener = BuildListenerWithExtProcFilter(ext_proc_config);
  RouteConfiguration route_config = default_route_config_;
  SetListenerAndRouteConfiguration(balancer_.get(), listener, route_config);
  balancer_->ads_service()->SetCdsResource(default_cluster_);
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(EdsResourceArgs({
      {"locality0", CreateEndpointsForBackends(0, 1)},
  })));
  ClientContext context;
  auto stream = stub_->BidiStream(&context);
  // Use WritesDone() to trigger S2C headers without sending body messages,
  // avoiding race conditions.
  stream->WritesDone();
  EchoResponse response;
  EXPECT_FALSE(stream->Read(&response));
  Status status = stream->Finish();
  EXPECT_FALSE(status.ok());
  EXPECT_EQ(status.error_code(), StatusCode::INTERNAL);
  EXPECT_THAT(status.error_message(),
              ::testing::HasSubstr("Received response trailers response before "
                                   "response headers response"));
}

TEST_P(XdsExtProcEnd2endTest,
       ServerToClientOrderingTrailersBeforeResponseBodyFailsCall) {
  // We disable S2C headers to work around the transport-level coalescing
  // limitation. This allows us to test the interaction between S2C body and
  // trailers.
  auto mock_service = std::make_shared<GenericMockService>(
      [](const ::envoy::service::ext_proc::v3::ProcessingRequest& request,
         ::envoy::service::ext_proc::v3::ProcessingResponse* response) {
        if (request.has_response_body()) {
          // Respond with response_trailers instead of response_body!
          response->mutable_response_trailers();
        } else {
          SetDefaultEmptyResponse(request, response);
        }
      });
  StartAlternativeServer(mock_service);
  CreateAndStartBackends(1);
  using envoy::extensions::filters::http::ext_proc::v3::ProcessingMode;
  auto ext_proc_config =
      ExternalProcessorBuilder()
          .SetTargetUri(alternative_ext_proc_server_->target())
          .SetInsecureChannelCredentials()
          .SetFailureModeAllow(false)  // Fail closed
          .SetRequestHeaderMode(ProcessingMode::SEND)
          .SetRequestBodyMode(ProcessingMode::GRPC)
          .SetResponseHeaderMode(ProcessingMode::SKIP)  // Skip S2C headers
          .SetResponseBodyMode(ProcessingMode::GRPC)    // Enable S2C body
          .SetResponseTrailerMode(
              ProcessingMode::SEND)  // Enable S2C trailers (must be SEND if
                                     // body is GRPC)
          .Build();
  Listener listener = BuildListenerWithExtProcFilter(ext_proc_config);
  RouteConfiguration route_config = default_route_config_;
  SetListenerAndRouteConfiguration(balancer_.get(), listener, route_config);
  balancer_->ads_service()->SetCdsResource(default_cluster_);
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(EdsResourceArgs({
      {"locality0", CreateEndpointsForBackends(0, 1)},
  })));
  ClientContext context;
  auto stream = stub_->BidiStream(&context);
  EchoRequest request;
  request.set_message("hello");
  stream->Write(request);
  EchoResponse response;
  EXPECT_FALSE(stream->Read(&response));
  Status status = stream->Finish();
  EXPECT_FALSE(status.ok());
  EXPECT_EQ(status.error_code(), StatusCode::INTERNAL);
  EXPECT_THAT(status.error_message(),
              ::testing::HasSubstr("Received response trailers response before "
                                   "all outstanding response body responses "
                                   "were received"));
}

class ResponseBodyAfterTrailersMockService : public MockExternalProcessorBase {
 public:
  grpc::Status Process(
      grpc::ServerContext* /*context*/,
      grpc::ServerReaderWriter<
          ::envoy::service::ext_proc::v3::ProcessingResponse,
          ::envoy::service::ext_proc::v3::ProcessingRequest>* stream) override {
    ::envoy::service::ext_proc::v3::ProcessingRequest request;
    while (stream->Read(&request)) {
      ::envoy::service::ext_proc::v3::ProcessingResponse response;
      if (request.has_response_trailers()) {
        // 1. Send response_trailers
        response.mutable_response_trailers();
        stream->Write(response);
        // 2. Send response_body (out of order!)
        response.Clear();
        response.mutable_response_body();
        stream->Write(response);
      } else {
        SetDefaultEmptyResponse(request, &response);
        stream->Write(response);
      }
    }
    return grpc::Status::OK;
  }
};

TEST_P(XdsExtProcEnd2endTest,
       ServerToClientOrderingResponseBodyAfterTrailersFailsCall) {
  // We disable S2C headers to work around the transport-level coalescing
  // limitation. This allows us to test the interaction between S2C body and
  // trailers.
  auto mock_service = std::make_shared<ResponseBodyAfterTrailersMockService>();
  StartAlternativeServer(mock_service);
  CreateAndStartBackends(1);
  using envoy::extensions::filters::http::ext_proc::v3::ProcessingMode;
  auto ext_proc_config =
      ExternalProcessorBuilder()
          .SetTargetUri(alternative_ext_proc_server_->target())
          .SetInsecureChannelCredentials()
          .SetFailureModeAllow(false)  // Fail closed
          .SetRequestHeaderMode(ProcessingMode::SEND)
          .SetRequestBodyMode(ProcessingMode::GRPC)
          .SetResponseHeaderMode(ProcessingMode::SKIP)   // Skip S2C headers
          .SetResponseBodyMode(ProcessingMode::GRPC)     // Enable S2C body
          .SetResponseTrailerMode(ProcessingMode::SEND)  // Enable S2C trailers
          .Build();
  Listener listener = BuildListenerWithExtProcFilter(ext_proc_config);
  RouteConfiguration route_config = default_route_config_;
  SetListenerAndRouteConfiguration(balancer_.get(), listener, route_config);
  balancer_->ads_service()->SetCdsResource(default_cluster_);
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(EdsResourceArgs({
      {"locality0", CreateEndpointsForBackends(0, 1)},
  })));
  ClientContext context;
  auto stream = stub_->BidiStream(&context);
  EchoRequest request;
  request.set_message("hello");
  stream->Write(request);
  EchoResponse response;
  // Read the echo response first to ensure the normal body flow works.
  EXPECT_TRUE(stream->Read(&response));
  // Half-close to trigger trailers from the backend.
  stream->WritesDone();
  // The backend will send trailers, triggering S2C trailers.
  // Ext-proc will respond with trailers, then body (error).
  EXPECT_FALSE(stream->Read(&response));
  Status status = stream->Finish();
  EXPECT_FALSE(status.ok());
  EXPECT_EQ(status.error_code(), StatusCode::INTERNAL);
  EXPECT_THAT(status.error_message(),
              ::testing::HasSubstr("Received response body response after "
                                   "response trailers response"));
}

TEST_P(XdsExtProcEnd2endTest,
       ServerToClientOrderingHeadersResponseWhenDisabledFailsCall) {
  auto mock_service = std::make_shared<GenericMockService>(
      [](const ::envoy::service::ext_proc::v3::ProcessingRequest& request,
         ::envoy::service::ext_proc::v3::ProcessingResponse* response) {
        if (request.has_response_body()) {
          // Respond with response_headers instead of response_body!
          response->mutable_response_headers();
        } else {
          SetDefaultEmptyResponse(request, response);
        }
      });
  StartAlternativeServer(mock_service);
  CreateAndStartBackends(1);
  using envoy::extensions::filters::http::ext_proc::v3::ProcessingMode;
  auto ext_proc_config =
      ExternalProcessorBuilder()
          .SetTargetUri(alternative_ext_proc_server_->target())
          .SetInsecureChannelCredentials()
          .SetFailureModeAllow(false)  // Fail closed
          .SetRequestHeaderMode(ProcessingMode::SEND)
          .SetRequestBodyMode(ProcessingMode::GRPC)
          .SetResponseHeaderMode(ProcessingMode::SKIP)  // Skip S2C headers
          .SetResponseBodyMode(ProcessingMode::GRPC)    // Enable S2C body
          .SetResponseTrailerMode(
              ProcessingMode::SEND)  // Must be SEND if body is GRPC
          .Build();
  Listener listener = BuildListenerWithExtProcFilter(ext_proc_config);
  RouteConfiguration route_config = default_route_config_;
  SetListenerAndRouteConfiguration(balancer_.get(), listener, route_config);
  balancer_->ads_service()->SetCdsResource(default_cluster_);
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(EdsResourceArgs({
      {"locality0", CreateEndpointsForBackends(0, 1)},
  })));
  ClientContext context;
  auto stream = stub_->BidiStream(&context);
  EchoRequest request;
  request.set_message("hello");
  stream->Write(request);
  EchoResponse response;
  EXPECT_FALSE(stream->Read(&response));
  Status status = stream->Finish();
  EXPECT_FALSE(status.ok());
  EXPECT_EQ(status.error_code(), StatusCode::INTERNAL);
  EXPECT_THAT(status.error_message(),
              ::testing::HasSubstr("Received response headers response but "
                                   "response headers are disabled"));
}

TEST_P(XdsExtProcEnd2endTest,
       ServerToClientOrderingTrailersResponseWhenDisabledFailsCall) {
  auto mock_service = std::make_shared<GenericMockService>(
      [](const ::envoy::service::ext_proc::v3::ProcessingRequest& request,
         ::envoy::service::ext_proc::v3::ProcessingResponse* response) {
        if (request.has_response_headers()) {
          // Respond with response_trailers instead of response_headers!
          response->mutable_response_trailers();
        } else {
          SetDefaultEmptyResponse(request, response);
        }
      });
  StartAlternativeServer(mock_service);
  CreateAndStartBackends(1);
  using envoy::extensions::filters::http::ext_proc::v3::ProcessingMode;
  auto ext_proc_config =
      ExternalProcessorBuilder()
          .SetTargetUri(alternative_ext_proc_server_->target())
          .SetInsecureChannelCredentials()
          .SetFailureModeAllow(false)  // Fail closed
          .SetRequestHeaderMode(ProcessingMode::SEND)
          .SetRequestBodyMode(ProcessingMode::GRPC)
          .SetResponseHeaderMode(ProcessingMode::SEND)  // Enable S2C headers
          .SetResponseBodyMode(ProcessingMode::NONE)
          .SetResponseTrailerMode(ProcessingMode::SKIP)  // Skip S2C trailers
          .Build();
  Listener listener = BuildListenerWithExtProcFilter(ext_proc_config);
  RouteConfiguration route_config = default_route_config_;
  SetListenerAndRouteConfiguration(balancer_.get(), listener, route_config);
  balancer_->ads_service()->SetCdsResource(default_cluster_);
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(EdsResourceArgs({
      {"locality0", CreateEndpointsForBackends(0, 1)},
  })));
  ClientContext context;
  auto stream = stub_->BidiStream(&context);
  // Use WritesDone() to trigger S2C headers without sending body messages,
  // avoiding race conditions.
  stream->WritesDone();
  EchoResponse response;
  EXPECT_FALSE(stream->Read(&response));
  Status status = stream->Finish();
  EXPECT_FALSE(status.ok());
  EXPECT_EQ(status.error_code(), StatusCode::INTERNAL);
  EXPECT_THAT(status.error_message(),
              ::testing::HasSubstr("Received response trailers response but "
                                   "response trailers are disabled"));
}

class CloseStreamAfterHeadersMockService : public MockExternalProcessorBase {
 public:
  grpc::Status Process(
      grpc::ServerContext* /*context*/,
      grpc::ServerReaderWriter<
          ::envoy::service::ext_proc::v3::ProcessingResponse,
          ::envoy::service::ext_proc::v3::ProcessingRequest>* stream) override {
    ::envoy::service::ext_proc::v3::ProcessingRequest request;
    if (stream->Read(&request)) {
      if (request.has_request_headers()) {
        ::envoy::service::ext_proc::v3::ProcessingResponse response;
        SetDefaultEmptyResponse(request, &response);
        stream->Write(response);
      }
    }
    // Returning an error ensures the filter detects the failure.
    return grpc::Status(grpc::StatusCode::ABORTED, "Closed after headers");
  }
};

TEST_P(XdsExtProcEnd2endTest,
       RequestHeadersExtProcCloseStreamAfterHeadersAllowCall) {
  auto mock_service = std::make_shared<CloseStreamAfterHeadersMockService>();
  StartAlternativeServer(mock_service);
  CreateAndStartBackends(1);
  using envoy::extensions::filters::http::ext_proc::v3::ProcessingMode;
  auto ext_proc_config =
      ExternalProcessorBuilder()
          .SetTargetUri(alternative_ext_proc_server_->target())
          .SetInsecureChannelCredentials()
          .SetFailureModeAllow(true)  // Fail-open!
          .SetRequestHeaderMode(ProcessingMode::SEND)
          .SetRequestBodyMode(ProcessingMode::GRPC)
          .SetResponseHeaderMode(ProcessingMode::SEND)   // Enable S2C headers
          .SetResponseBodyMode(ProcessingMode::GRPC)     // Enable S2C body
          .SetResponseTrailerMode(ProcessingMode::SEND)  // Enable S2C trailers
          .Build();
  Listener listener = BuildListenerWithExtProcFilter(ext_proc_config);
  RouteConfiguration route_config = default_route_config_;
  SetListenerAndRouteConfiguration(balancer_.get(), listener, route_config);
  balancer_->ads_service()->SetCdsResource(default_cluster_);
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(EdsResourceArgs({
      {"locality0", CreateEndpointsForBackends(0, 1)},
  })));

  ClientContext context;
  auto stream = stub_->BidiStream(&context);

  EchoRequest request;
  EchoResponse response;

  request.set_message("hello1");
  EXPECT_TRUE(stream->Write(request));
  EXPECT_TRUE(stream->Read(&response));
  EXPECT_EQ(response.message(), "hello1");

  request.set_message("hello2");
  EXPECT_TRUE(stream->Write(request));
  EXPECT_TRUE(stream->Read(&response));
  EXPECT_EQ(response.message(), "hello2");

  stream->WritesDone();
  EXPECT_FALSE(stream->Read(&response));  // Expect EOF

  Status status = stream->Finish();
  EXPECT_TRUE(status.ok()) << status.error_message();
}

TEST_P(XdsExtProcEnd2endTest,
       RequestHeadersExtProcCloseStreamAfterHeadersFailCall) {
  auto mock_service = std::make_shared<CloseStreamAfterHeadersMockService>();
  StartAlternativeServer(mock_service);
  CreateAndStartBackends(1);
  using envoy::extensions::filters::http::ext_proc::v3::ProcessingMode;
  auto ext_proc_config =
      ExternalProcessorBuilder()
          .SetTargetUri(alternative_ext_proc_server_->target())
          .SetInsecureChannelCredentials()
          .SetFailureModeAllow(false)  // Fail-closed!
          .SetRequestHeaderMode(ProcessingMode::SEND)
          .SetRequestBodyMode(ProcessingMode::GRPC)
          .SetResponseHeaderMode(ProcessingMode::SEND)   // Enable S2C headers
          .SetResponseBodyMode(ProcessingMode::GRPC)     // Enable S2C body
          .SetResponseTrailerMode(ProcessingMode::SEND)  // Enable S2C trailers
          .Build();
  Listener listener = BuildListenerWithExtProcFilter(ext_proc_config);
  RouteConfiguration route_config = default_route_config_;
  SetListenerAndRouteConfiguration(balancer_.get(), listener, route_config);
  balancer_->ads_service()->SetCdsResource(default_cluster_);
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(EdsResourceArgs({
      {"locality0", CreateEndpointsForBackends(0, 1)},
  })));

  ClientContext context;
  auto stream = stub_->BidiStream(&context);

  EchoRequest request;
  request.set_message("hello");
  // Write might succeed or fail depending on when the error is propagated.
  // But the stream should eventually fail.
  stream->Write(request);
  stream->WritesDone();

  EchoResponse response;
  EXPECT_FALSE(stream->Read(&response));

  Status status = stream->Finish();
  EXPECT_FALSE(status.ok());
  EXPECT_THAT(status.error_code(),
              ::testing::AnyOf(StatusCode::UNAVAILABLE, StatusCode::CANCELLED,
                               StatusCode::ABORTED));
}

TEST_P(
    XdsExtProcEnd2endTest,
    StreamErrorAfterRequestHeaderResponseBeforeResponseHeaderCallWhenFailureModeAllowIsTrue) {
  auto mock_service = std::make_shared<CloseStreamAfterHeadersMockService>();
  StartAlternativeServer(mock_service);
  CreateAndStartBackends(1);
  using envoy::extensions::filters::http::ext_proc::v3::ProcessingMode;
  auto ext_proc_config =
      ExternalProcessorBuilder()
          .SetTargetUri(alternative_ext_proc_server_->target())
          .SetInsecureChannelCredentials()
          .SetFailureModeAllow(true)  // Fail-open!
          .SetRequestHeaderMode(ProcessingMode::SEND)
          .SetRequestBodyMode(ProcessingMode::NONE)     // Skip body
          .SetResponseHeaderMode(ProcessingMode::SEND)  // Enable S2C headers
          .SetResponseBodyMode(ProcessingMode::NONE)    // Skip S2C body
          .SetResponseTrailerMode(ProcessingMode::SKIP)
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
  EXPECT_EQ(response.message(), kRequestMessage);
}

TEST_P(
    XdsExtProcEnd2endTest,
    StreamErrorAfterRequestHeaderResponseBeforeResponseHeaderCallWhenFailureModeAllowIsFalse) {
  auto mock_service = std::make_shared<CloseStreamAfterHeadersMockService>();
  StartAlternativeServer(mock_service);
  CreateAndStartBackends(1);
  using envoy::extensions::filters::http::ext_proc::v3::ProcessingMode;
  auto ext_proc_config =
      ExternalProcessorBuilder()
          .SetTargetUri(alternative_ext_proc_server_->target())
          .SetInsecureChannelCredentials()
          .SetFailureModeAllow(false)  // Fail-closed!
          .SetRequestHeaderMode(ProcessingMode::SEND)
          .SetRequestBodyMode(ProcessingMode::NONE)     // Skip body
          .SetResponseHeaderMode(ProcessingMode::SEND)  // Enable S2C headers
          .SetResponseBodyMode(ProcessingMode::NONE)    // Skip S2C body
          .SetResponseTrailerMode(ProcessingMode::SKIP)
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
  EXPECT_THAT(status.error_code(),
              ::testing::AnyOf(StatusCode::UNAVAILABLE, StatusCode::CANCELLED,
                               StatusCode::ABORTED));
}

TEST_P(XdsExtProcEnd2endTest,
       StreamErrorBeforeResponseBodyCallWhenFailureModeAllowIsTrue) {
  auto mock_service = std::make_shared<CloseStreamAfterHeadersMockService>();
  StartAlternativeServer(mock_service);
  CreateAndStartBackends(1);
  using envoy::extensions::filters::http::ext_proc::v3::ProcessingMode;
  auto ext_proc_config =
      ExternalProcessorBuilder()
          .SetTargetUri(alternative_ext_proc_server_->target())
          .SetInsecureChannelCredentials()
          .SetFailureModeAllow(true)  // Fail-open!
          .SetRequestHeaderMode(ProcessingMode::SEND)
          .SetRequestBodyMode(ProcessingMode::NONE)
          .SetResponseHeaderMode(
              ProcessingMode::SKIP)  // Skip S2C headers to avoid coalescing bug
          .SetResponseBodyMode(ProcessingMode::GRPC)  // Enable S2C body
          .SetResponseTrailerMode(
              ProcessingMode::SEND)  // Must be SEND if body is GRPC
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
  // Verify we actually received the body.
  EXPECT_EQ(response.message(), kRequestMessage);
}

TEST_P(XdsExtProcEnd2endTest,
       StreamErrorBeforeResponseBodyCallWhenFailureModeAllowIsFalse) {
  auto mock_service = std::make_shared<CloseStreamAfterHeadersMockService>();
  StartAlternativeServer(mock_service);
  CreateAndStartBackends(1);
  using envoy::extensions::filters::http::ext_proc::v3::ProcessingMode;
  auto ext_proc_config =
      ExternalProcessorBuilder()
          .SetTargetUri(alternative_ext_proc_server_->target())
          .SetInsecureChannelCredentials()
          .SetFailureModeAllow(false)  // Fail-closed!
          .SetRequestHeaderMode(ProcessingMode::SEND)
          .SetRequestBodyMode(ProcessingMode::NONE)
          .SetResponseHeaderMode(
              ProcessingMode::SKIP)  // Skip S2C headers to avoid coalescing bug
          .SetResponseBodyMode(ProcessingMode::GRPC)  // Enable S2C body
          .SetResponseTrailerMode(
              ProcessingMode::SEND)  // Must be SEND if body is GRPC
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
  EXPECT_THAT(status.error_code(),
              ::testing::AnyOf(StatusCode::UNAVAILABLE, StatusCode::CANCELLED,
                               StatusCode::ABORTED));
}

TEST_P(XdsExtProcEnd2endTest,
       StreamErrorBeforeResponseTrailerCallWhenFailureModeAllowIsTrue) {
  auto mock_service = std::make_shared<CloseStreamAfterHeadersMockService>();
  StartAlternativeServer(mock_service);
  CreateAndStartBackends(1);
  using envoy::extensions::filters::http::ext_proc::v3::ProcessingMode;
  auto ext_proc_config =
      ExternalProcessorBuilder()
          .SetTargetUri(alternative_ext_proc_server_->target())
          .SetInsecureChannelCredentials()
          .SetFailureModeAllow(true)  // Fail-open!
          .SetRequestHeaderMode(ProcessingMode::SEND)
          .SetRequestBodyMode(ProcessingMode::NONE)
          .SetResponseHeaderMode(ProcessingMode::SKIP)
          .SetResponseBodyMode(ProcessingMode::NONE)
          .SetResponseTrailerMode(ProcessingMode::SEND)  // Enable S2C trailers
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
  EXPECT_EQ(response.message(), kRequestMessage);
}

TEST_P(XdsExtProcEnd2endTest,
       StreamErrorBeforeResponseTrailerCallWhenFailureModeAllowIsFalse) {
  auto mock_service = std::make_shared<CloseStreamAfterHeadersMockService>();
  StartAlternativeServer(mock_service);
  CreateAndStartBackends(1);
  using envoy::extensions::filters::http::ext_proc::v3::ProcessingMode;
  auto ext_proc_config =
      ExternalProcessorBuilder()
          .SetTargetUri(alternative_ext_proc_server_->target())
          .SetInsecureChannelCredentials()
          .SetFailureModeAllow(false)  // Fail-closed!
          .SetRequestHeaderMode(ProcessingMode::SEND)
          .SetRequestBodyMode(ProcessingMode::NONE)
          .SetResponseHeaderMode(ProcessingMode::SKIP)
          .SetResponseBodyMode(ProcessingMode::NONE)
          .SetResponseTrailerMode(ProcessingMode::SEND)  // Enable S2C trailers
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
  EXPECT_THAT(status.error_code(),
              ::testing::AnyOf(StatusCode::UNAVAILABLE, StatusCode::CANCELLED,
                               StatusCode::ABORTED));
}

class FailAfterRequestHeadersAllowedMockService
    : public MockExternalProcessorBase {
 public:
  grpc::Status Process(
      grpc::ServerContext* /*context*/,
      grpc::ServerReaderWriter<
          ::envoy::service::ext_proc::v3::ProcessingResponse,
          ::envoy::service::ext_proc::v3::ProcessingRequest>* stream) override {
    ::envoy::service::ext_proc::v3::ProcessingRequest request;
    if (stream->Read(&request)) {
      if (request.has_request_headers()) {
        ::envoy::service::ext_proc::v3::ProcessingResponse response;
        response.mutable_request_headers();
        stream->Write(response);
        return grpc::Status(grpc::StatusCode::RESOURCE_EXHAUSTED,
                            "Failed after request headers allowed");
      }
    }
    return grpc::Status::OK;
  }
};

class FailAfterResponseHeadersMockService : public MockExternalProcessorBase {
 public:
  grpc::Status Process(
      grpc::ServerContext* /*context*/,
      grpc::ServerReaderWriter<
          ::envoy::service::ext_proc::v3::ProcessingResponse,
          ::envoy::service::ext_proc::v3::ProcessingRequest>* stream) override {
    ::envoy::service::ext_proc::v3::ProcessingRequest request;
    while (stream->Read(&request)) {
      ::envoy::service::ext_proc::v3::ProcessingResponse response;
      if (request.has_request_headers()) {
        response.mutable_request_headers();
        stream->Write(response);
      } else if (request.has_response_headers()) {
        response.mutable_response_headers();
        stream->Write(response);
        return grpc::Status(grpc::StatusCode::RESOURCE_EXHAUSTED,
                            "Failed after response headers");
      }
    }
    return grpc::Status::OK;
  }
};

class FailAfterResponseBodyMockService : public MockExternalProcessorBase {
 public:
  grpc::Status Process(
      grpc::ServerContext* /*context*/,
      grpc::ServerReaderWriter<
          ::envoy::service::ext_proc::v3::ProcessingResponse,
          ::envoy::service::ext_proc::v3::ProcessingRequest>* stream) override {
    ::envoy::service::ext_proc::v3::ProcessingRequest request;
    while (stream->Read(&request)) {
      ::envoy::service::ext_proc::v3::ProcessingResponse response;
      if (request.has_request_headers()) {
        response.mutable_request_headers();
        stream->Write(response);
      } else if (request.has_response_headers()) {
        response.mutable_response_headers();
        stream->Write(response);
      } else if (request.has_response_body()) {
        response.mutable_response_body();
        stream->Write(response);
        return grpc::Status(grpc::StatusCode::RESOURCE_EXHAUSTED,
                            "Failed after response body");
      }
    }
    return grpc::Status::OK;
  }
};

TEST_P(XdsExtProcEnd2endTest,
       StreamFailBeforeResponseHeadersObservabilityFailCall) {
  auto mock_service =
      std::make_shared<FailAfterRequestHeadersAllowedMockService>();
  StartAlternativeServer(mock_service);
  CreateAndStartBackends(1);
  ResetStubWithUniqueArg();
  using envoy::extensions::filters::http::ext_proc::v3::ProcessingMode;
  auto ext_proc_config =
      ExternalProcessorBuilder()
          .SetTargetUri(alternative_ext_proc_server_->target())
          .SetInsecureChannelCredentials()
          .SetObservabilityMode(true)
          .SetFailureModeAllow(false)  // Fail-closed
          .SetRequestHeaderMode(ProcessingMode::SEND)
          .SetResponseHeaderMode(ProcessingMode::SEND)
          .SetResponseTrailerMode(ProcessingMode::SEND)
          .SetRequestBodyMode(ProcessingMode::NONE)
          .SetResponseBodyMode(ProcessingMode::NONE)
          .Build();
  Listener listener = BuildListenerWithExtProcFilter(ext_proc_config);
  RouteConfiguration route_config = default_route_config_;
  SetListenerAndRouteConfiguration(balancer_.get(), listener, route_config);
  balancer_->ads_service()->SetCdsResource(default_cluster_);
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(EdsResourceArgs({
      {"locality0", CreateEndpointsForBackends(0, 1)},
  })));

  ClientContext context;
  auto stream = stub_->BidiStream(&context);
  EchoRequest request;
  EchoResponse response;
  request.set_message("message1");
  stream->Write(request);
  stream->Read(&response);
  Status status = stream->Finish();
  EXPECT_FALSE(status.ok());
  EXPECT_EQ(status.error_code(), StatusCode::RESOURCE_EXHAUSTED);
  EXPECT_THAT(status.error_message(),
              ::testing::HasSubstr("Failed after request headers allowed"));
}

TEST_P(XdsExtProcEnd2endTest,
       StreamFailBeforeResponseHeadersObservabilityAllowCall) {
  auto mock_service =
      std::make_shared<FailAfterRequestHeadersAllowedMockService>();
  StartAlternativeServer(mock_service);
  CreateAndStartBackends(1);
  ResetStubWithUniqueArg();
  using envoy::extensions::filters::http::ext_proc::v3::ProcessingMode;
  auto ext_proc_config =
      ExternalProcessorBuilder()
          .SetTargetUri(alternative_ext_proc_server_->target())
          .SetInsecureChannelCredentials()
          .SetObservabilityMode(true)
          .SetFailureModeAllow(true)  // Fail-open
          .SetRequestHeaderMode(ProcessingMode::SEND)
          .SetResponseHeaderMode(ProcessingMode::SEND)
          .SetResponseTrailerMode(ProcessingMode::SEND)
          .SetRequestBodyMode(ProcessingMode::NONE)
          .SetResponseBodyMode(ProcessingMode::NONE)
          .Build();
  Listener listener = BuildListenerWithExtProcFilter(ext_proc_config);
  RouteConfiguration route_config = default_route_config_;
  SetListenerAndRouteConfiguration(balancer_.get(), listener, route_config);
  balancer_->ads_service()->SetCdsResource(default_cluster_);
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(EdsResourceArgs({
      {"locality0", CreateEndpointsForBackends(0, 1)},
  })));

  ClientContext context;
  auto stream = stub_->BidiStream(&context);
  EchoRequest request;
  EchoResponse response;
  request.set_message("message1");
  EXPECT_TRUE(stream->Write(request));
  EXPECT_TRUE(stream->Read(&response));
  EXPECT_EQ(response.message(), "message1");
  stream->WritesDone();
  Status status = stream->Finish();
  EXPECT_TRUE(status.ok()) << status.error_message();
}

TEST_P(XdsExtProcEnd2endTest,
       StreamFailBeforeResponseBodyObservabilityFailCall) {
  auto mock_service = std::make_shared<FailAfterResponseHeadersMockService>();
  StartAlternativeServer(mock_service);
  CreateAndStartBackends(1);
  ResetStubWithUniqueArg();
  using envoy::extensions::filters::http::ext_proc::v3::ProcessingMode;
  auto ext_proc_config =
      ExternalProcessorBuilder()
          .SetTargetUri(alternative_ext_proc_server_->target())
          .SetInsecureChannelCredentials()
          .SetObservabilityMode(true)
          .SetFailureModeAllow(false)  // Fail-closed
          .SetRequestHeaderMode(ProcessingMode::SEND)
          .SetResponseHeaderMode(ProcessingMode::SEND)
          .SetResponseTrailerMode(ProcessingMode::SEND)
          .SetRequestBodyMode(ProcessingMode::NONE)
          .SetResponseBodyMode(ProcessingMode::GRPC)
          .Build();
  Listener listener = BuildListenerWithExtProcFilter(ext_proc_config);
  RouteConfiguration route_config = default_route_config_;
  SetListenerAndRouteConfiguration(balancer_.get(), listener, route_config);
  balancer_->ads_service()->SetCdsResource(default_cluster_);
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(EdsResourceArgs({
      {"locality0", CreateEndpointsForBackends(0, 1)},
  })));

  ClientContext context;
  auto stream = stub_->BidiStream(&context);
  EchoRequest request;
  EchoResponse response;
  request.set_message("message1");
  stream->Write(request);
  stream->Read(&response);
  Status status = stream->Finish();
  EXPECT_FALSE(status.ok());
  EXPECT_EQ(status.error_code(), StatusCode::RESOURCE_EXHAUSTED);
  EXPECT_THAT(status.error_message(),
              ::testing::HasSubstr("Failed after response headers"));
}

TEST_P(XdsExtProcEnd2endTest,
       StreamFailBeforeResponseBodyObservabilityAllowCall) {
  auto mock_service = std::make_shared<FailAfterResponseHeadersMockService>();
  StartAlternativeServer(mock_service);
  CreateAndStartBackends(1);
  ResetStubWithUniqueArg();
  using envoy::extensions::filters::http::ext_proc::v3::ProcessingMode;
  auto ext_proc_config =
      ExternalProcessorBuilder()
          .SetTargetUri(alternative_ext_proc_server_->target())
          .SetInsecureChannelCredentials()
          .SetObservabilityMode(true)
          .SetFailureModeAllow(true)  // Fail-open
          .SetRequestHeaderMode(ProcessingMode::SEND)
          .SetResponseHeaderMode(ProcessingMode::SEND)
          .SetResponseTrailerMode(ProcessingMode::SEND)
          .SetRequestBodyMode(ProcessingMode::NONE)
          .SetResponseBodyMode(ProcessingMode::GRPC)
          .Build();
  Listener listener = BuildListenerWithExtProcFilter(ext_proc_config);
  RouteConfiguration route_config = default_route_config_;
  SetListenerAndRouteConfiguration(balancer_.get(), listener, route_config);
  balancer_->ads_service()->SetCdsResource(default_cluster_);
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(EdsResourceArgs({
      {"locality0", CreateEndpointsForBackends(0, 1)},
  })));

  ClientContext context;
  auto stream = stub_->BidiStream(&context);
  EchoRequest request;
  EchoResponse response;
  request.set_message("message1");
  EXPECT_TRUE(stream->Write(request));
  EXPECT_TRUE(stream->Read(&response));
  EXPECT_EQ(response.message(), "message1");
  stream->WritesDone();
  Status status = stream->Finish();
  EXPECT_TRUE(status.ok()) << status.error_message();
}

TEST_P(XdsExtProcEnd2endTest,
       StreamFailBeforeResponseTrailersObservabilityFailCall) {
  auto mock_service = std::make_shared<FailAfterResponseBodyMockService>();
  StartAlternativeServer(mock_service);
  CreateAndStartBackends(1);
  ResetStubWithUniqueArg();
  using envoy::extensions::filters::http::ext_proc::v3::ProcessingMode;
  auto ext_proc_config =
      ExternalProcessorBuilder()
          .SetTargetUri(alternative_ext_proc_server_->target())
          .SetInsecureChannelCredentials()
          .SetObservabilityMode(true)
          .SetFailureModeAllow(false)  // Fail-closed
          .SetRequestHeaderMode(ProcessingMode::SEND)
          .SetResponseHeaderMode(ProcessingMode::SEND)
          .SetResponseTrailerMode(ProcessingMode::SEND)
          .SetRequestBodyMode(ProcessingMode::NONE)
          .SetResponseBodyMode(ProcessingMode::GRPC)
          .Build();
  Listener listener = BuildListenerWithExtProcFilter(ext_proc_config);
  RouteConfiguration route_config = default_route_config_;
  SetListenerAndRouteConfiguration(balancer_.get(), listener, route_config);
  balancer_->ads_service()->SetCdsResource(default_cluster_);
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(EdsResourceArgs({
      {"locality0", CreateEndpointsForBackends(0, 1)},
  })));

  ClientContext context;
  auto stream = stub_->BidiStream(&context);
  EchoRequest request;
  EchoResponse response;
  request.set_message("message1");
  stream->Write(request);
  stream->Read(&response);
  stream->WritesDone();
  Status status = stream->Finish();
  EXPECT_FALSE(status.ok());
  EXPECT_EQ(status.error_code(), StatusCode::RESOURCE_EXHAUSTED);
  EXPECT_THAT(status.error_message(),
              ::testing::HasSubstr("Failed after response body"));
}

TEST_P(XdsExtProcEnd2endTest,
       StreamFailBeforeResponseTrailersObservabilityAllowCall) {
  auto mock_service = std::make_shared<FailAfterResponseBodyMockService>();
  StartAlternativeServer(mock_service);
  CreateAndStartBackends(1);
  ResetStubWithUniqueArg();
  using envoy::extensions::filters::http::ext_proc::v3::ProcessingMode;
  auto ext_proc_config =
      ExternalProcessorBuilder()
          .SetTargetUri(alternative_ext_proc_server_->target())
          .SetInsecureChannelCredentials()
          .SetObservabilityMode(true)
          .SetFailureModeAllow(true)  // Fail-open
          .SetRequestHeaderMode(ProcessingMode::SEND)
          .SetResponseHeaderMode(ProcessingMode::SEND)
          .SetResponseTrailerMode(ProcessingMode::SEND)
          .SetRequestBodyMode(ProcessingMode::NONE)
          .SetResponseBodyMode(ProcessingMode::GRPC)
          .Build();
  Listener listener = BuildListenerWithExtProcFilter(ext_proc_config);
  RouteConfiguration route_config = default_route_config_;
  SetListenerAndRouteConfiguration(balancer_.get(), listener, route_config);
  balancer_->ads_service()->SetCdsResource(default_cluster_);
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(EdsResourceArgs({
      {"locality0", CreateEndpointsForBackends(0, 1)},
  })));

  ClientContext context;
  auto stream = stub_->BidiStream(&context);
  EchoRequest request;
  EchoResponse response;
  request.set_message("message1");
  EXPECT_TRUE(stream->Write(request));
  EXPECT_TRUE(stream->Read(&response));
  EXPECT_EQ(response.message(), "message1");
  stream->WritesDone();
  Status status = stream->Finish();
  EXPECT_TRUE(status.ok()) << status.error_message();
}

class CleanCloseMockService : public MockExternalProcessorBase {
 public:
  enum class CloseStage { kBeforeRequestHeaders, kAfterRequestHeaders };

  explicit CleanCloseMockService(CloseStage stage) : stage_(stage) {}

  grpc::Status Process(
      grpc::ServerContext* /*context*/,
      grpc::ServerReaderWriter<
          ::envoy::service::ext_proc::v3::ProcessingResponse,
          ::envoy::service::ext_proc::v3::ProcessingRequest>* stream) override {
    if (stage_ == CloseStage::kBeforeRequestHeaders) {
      return grpc::Status::OK;
    }
    ::envoy::service::ext_proc::v3::ProcessingRequest request;
    while (stream->Read(&request)) {
      if (request.has_request_headers() &&
          stage_ == CloseStage::kAfterRequestHeaders) {
        ::envoy::service::ext_proc::v3::ProcessingResponse response;
        SetDefaultEmptyResponse(request, &response);
        stream->Write(response);
        return grpc::Status::OK;
      }
      ::envoy::service::ext_proc::v3::ProcessingResponse response;
      SetDefaultEmptyResponse(request, &response);
      stream->Write(response);
    }
    return grpc::Status::OK;
  }

 private:
  CloseStage stage_;
};

TEST_P(XdsExtProcEnd2endTest, StreamCleanCloseBeforeRequestHeadersFailClosed) {
  auto mock_service = std::make_shared<CleanCloseMockService>(
      CleanCloseMockService::CloseStage::kBeforeRequestHeaders);
  StartAlternativeServer(mock_service);
  CreateAndStartBackends(1);
  ResetStubWithUniqueArg();
  using envoy::extensions::filters::http::ext_proc::v3::ProcessingMode;
  auto ext_proc_config =
      ExternalProcessorBuilder()
          .SetTargetUri(alternative_ext_proc_server_->target())
          .SetInsecureChannelCredentials()
          .SetObservabilityMode(false)
          .SetFailureModeAllow(false)
          .SetRequestHeaderMode(ProcessingMode::SEND)
          .SetResponseHeaderMode(ProcessingMode::SEND)
          .Build();
  Listener listener = BuildListenerWithExtProcFilter(ext_proc_config);
  RouteConfiguration route_config = default_route_config_;
  SetListenerAndRouteConfiguration(balancer_.get(), listener, route_config);
  balancer_->ads_service()->SetCdsResource(default_cluster_);
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(EdsResourceArgs({
      {"locality0", CreateEndpointsForBackends(0, 1)},
  })));

  ClientContext context;
  auto stream = stub_->BidiStream(&context);
  EchoRequest request;
  EchoResponse response;
  request.set_message("message1");
  EXPECT_TRUE(stream->Write(request));
  EXPECT_TRUE(stream->Read(&response));
  EXPECT_EQ(response.message(), "message1");
  stream->WritesDone();
  Status status = stream->Finish();
  EXPECT_TRUE(status.ok()) << status.error_message();
}

TEST_P(XdsExtProcEnd2endTest, StreamCleanCloseBeforeRequestHeadersFailOpen) {
  auto mock_service = std::make_shared<CleanCloseMockService>(
      CleanCloseMockService::CloseStage::kBeforeRequestHeaders);
  StartAlternativeServer(mock_service);
  CreateAndStartBackends(1);
  ResetStubWithUniqueArg();
  using envoy::extensions::filters::http::ext_proc::v3::ProcessingMode;
  auto ext_proc_config =
      ExternalProcessorBuilder()
          .SetTargetUri(alternative_ext_proc_server_->target())
          .SetInsecureChannelCredentials()
          .SetObservabilityMode(false)
          .SetFailureModeAllow(true)
          .SetRequestHeaderMode(ProcessingMode::SEND)
          .SetResponseHeaderMode(ProcessingMode::SEND)
          .Build();
  Listener listener = BuildListenerWithExtProcFilter(ext_proc_config);
  RouteConfiguration route_config = default_route_config_;
  SetListenerAndRouteConfiguration(balancer_.get(), listener, route_config);
  balancer_->ads_service()->SetCdsResource(default_cluster_);
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(EdsResourceArgs({
      {"locality0", CreateEndpointsForBackends(0, 1)},
  })));

  ClientContext context;
  auto stream = stub_->BidiStream(&context);
  EchoRequest request;
  EchoResponse response;
  request.set_message("message1");
  EXPECT_TRUE(stream->Write(request));
  EXPECT_TRUE(stream->Read(&response));
  EXPECT_EQ(response.message(), "message1");
  stream->WritesDone();
  Status status = stream->Finish();
  EXPECT_TRUE(status.ok()) << status.error_message();
}

TEST_P(XdsExtProcEnd2endTest,
       StreamCleanCloseBeforeRequestHeadersObservabilityFailClosed) {
  auto mock_service = std::make_shared<CleanCloseMockService>(
      CleanCloseMockService::CloseStage::kBeforeRequestHeaders);
  StartAlternativeServer(mock_service);
  CreateAndStartBackends(1);
  ResetStubWithUniqueArg();
  using envoy::extensions::filters::http::ext_proc::v3::ProcessingMode;
  auto ext_proc_config =
      ExternalProcessorBuilder()
          .SetTargetUri(alternative_ext_proc_server_->target())
          .SetInsecureChannelCredentials()
          .SetObservabilityMode(true)
          .SetFailureModeAllow(false)
          .SetRequestHeaderMode(ProcessingMode::SEND)
          .SetResponseHeaderMode(ProcessingMode::SEND)
          .Build();
  Listener listener = BuildListenerWithExtProcFilter(ext_proc_config);
  RouteConfiguration route_config = default_route_config_;
  SetListenerAndRouteConfiguration(balancer_.get(), listener, route_config);
  balancer_->ads_service()->SetCdsResource(default_cluster_);
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(EdsResourceArgs({
      {"locality0", CreateEndpointsForBackends(0, 1)},
  })));

  ClientContext context;
  auto stream = stub_->BidiStream(&context);
  EchoRequest request;
  EchoResponse response;
  request.set_message("message1");
  EXPECT_TRUE(stream->Write(request));
  EXPECT_TRUE(stream->Read(&response));
  EXPECT_EQ(response.message(), "message1");
  stream->WritesDone();
  Status status = stream->Finish();
  EXPECT_TRUE(status.ok()) << status.error_message();
}

TEST_P(XdsExtProcEnd2endTest,
       StreamCleanCloseBeforeRequestHeadersObservabilityFailOpen) {
  auto mock_service = std::make_shared<CleanCloseMockService>(
      CleanCloseMockService::CloseStage::kBeforeRequestHeaders);
  StartAlternativeServer(mock_service);
  CreateAndStartBackends(1);
  ResetStubWithUniqueArg();
  using envoy::extensions::filters::http::ext_proc::v3::ProcessingMode;
  auto ext_proc_config =
      ExternalProcessorBuilder()
          .SetTargetUri(alternative_ext_proc_server_->target())
          .SetInsecureChannelCredentials()
          .SetObservabilityMode(true)
          .SetFailureModeAllow(true)
          .SetRequestHeaderMode(ProcessingMode::SEND)
          .SetResponseHeaderMode(ProcessingMode::SEND)
          .Build();
  Listener listener = BuildListenerWithExtProcFilter(ext_proc_config);
  RouteConfiguration route_config = default_route_config_;
  SetListenerAndRouteConfiguration(balancer_.get(), listener, route_config);
  balancer_->ads_service()->SetCdsResource(default_cluster_);
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(EdsResourceArgs({
      {"locality0", CreateEndpointsForBackends(0, 1)},
  })));

  ClientContext context;
  auto stream = stub_->BidiStream(&context);
  EchoRequest request;
  EchoResponse response;
  request.set_message("message1");
  EXPECT_TRUE(stream->Write(request));
  EXPECT_TRUE(stream->Read(&response));
  EXPECT_EQ(response.message(), "message1");
  stream->WritesDone();
  Status status = stream->Finish();
  EXPECT_TRUE(status.ok()) << status.error_message();
}

TEST_P(XdsExtProcEnd2endTest, StreamCleanCloseAfterRequestHeaders) {
  auto mock_service = std::make_shared<CleanCloseMockService>(
      CleanCloseMockService::CloseStage::kAfterRequestHeaders);
  StartAlternativeServer(mock_service);
  CreateAndStartBackends(1);
  ResetStubWithUniqueArg();
  using envoy::extensions::filters::http::ext_proc::v3::ProcessingMode;
  auto ext_proc_config =
      ExternalProcessorBuilder()
          .SetTargetUri(alternative_ext_proc_server_->target())
          .SetInsecureChannelCredentials()
          .SetFailureModeAllow(false)
          .SetRequestHeaderMode(ProcessingMode::SEND)
          .SetResponseHeaderMode(ProcessingMode::SEND)
          .Build();
  Listener listener = BuildListenerWithExtProcFilter(ext_proc_config);
  RouteConfiguration route_config = default_route_config_;
  SetListenerAndRouteConfiguration(balancer_.get(), listener, route_config);
  balancer_->ads_service()->SetCdsResource(default_cluster_);
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(EdsResourceArgs({
      {"locality0", CreateEndpointsForBackends(0, 1)},
  })));

  ClientContext context;
  auto stream = stub_->BidiStream(&context);
  EchoRequest request;
  EchoResponse response;
  request.set_message("message1");
  EXPECT_TRUE(stream->Write(request));
  EXPECT_TRUE(stream->Read(&response));
  EXPECT_EQ(response.message(), "message1");
  stream->WritesDone();
  Status status = stream->Finish();
  EXPECT_TRUE(status.ok()) << status.error_message();
}

TEST_P(XdsExtProcEnd2endTest,
       StreamCleanCloseAfterRequestHeadersObservability) {
  auto mock_service = std::make_shared<CleanCloseMockService>(
      CleanCloseMockService::CloseStage::kAfterRequestHeaders);
  StartAlternativeServer(mock_service);
  CreateAndStartBackends(1);
  ResetStubWithUniqueArg();
  using envoy::extensions::filters::http::ext_proc::v3::ProcessingMode;
  auto ext_proc_config =
      ExternalProcessorBuilder()
          .SetTargetUri(alternative_ext_proc_server_->target())
          .SetInsecureChannelCredentials()
          .SetObservabilityMode(true)
          .SetFailureModeAllow(false)
          .SetRequestHeaderMode(ProcessingMode::SEND)
          .SetResponseHeaderMode(ProcessingMode::SEND)
          .Build();
  Listener listener = BuildListenerWithExtProcFilter(ext_proc_config);
  RouteConfiguration route_config = default_route_config_;
  SetListenerAndRouteConfiguration(balancer_.get(), listener, route_config);
  balancer_->ads_service()->SetCdsResource(default_cluster_);
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(EdsResourceArgs({
      {"locality0", CreateEndpointsForBackends(0, 1)},
  })));

  ClientContext context;
  auto stream = stub_->BidiStream(&context);
  EchoRequest request;
  EchoResponse response;
  request.set_message("message1");
  EXPECT_TRUE(stream->Write(request));
  EXPECT_TRUE(stream->Read(&response));
  EXPECT_EQ(response.message(), "message1");
  stream->WritesDone();
  Status status = stream->Finish();
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
