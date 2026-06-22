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

class XdsExtProcEnd2endTest : public XdsEnd2endTest {
 public:
  void SetUp() override {
    grpc_core::SetEnv("GRPC_EXPERIMENTAL_XDS_EXT_PROC_ON_CLIENT", "true");
    grpc_tracer_set_enabled("ext_proc_filter", 1);
    grpc_tracer_set_enabled("promise_primitives", 1);
    grpc_tracer_set_enabled("call_state", 1);
    grpc_tracer_set_enabled("channel", 1);
    grpc_tracer_set_enabled("transport", 1);
    grpc_core::SetEnv("GRPC_VERBOSITY", "DEBUG");
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

GTEST_ALLOW_UNINSTANTIATED_PARAMETERIZED_TEST(XdsExtProcEnd2endTest);
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
    kMutateRequestBody,
    kMutateResponseBody,
    kErrorOnRequestBody,
    kErrorOnResponseBody,
    kCloseGracefullyMidCall,
    kErrorMidCall,
    kErrorOnRequestHeaders,
    kUnsolicitedResponseBody,
    kCopyRequestBody,
    kCopyResponseBody,
    kStreamingUnsolicitedResponseBody,
    kRequestBodyContinueAndReplace,
    kRequestBodyGrpcMessageCompressed,
    kStreamingEarlyHalfClose,
    kStreamingEarlyHalfCloseWithoutMessage,
    kHalfCloseResponseBodyMidCall,
    kImmediateError,
    kErrorAfterRequestHeaders,
    kErrorAfterResponseHeaders,
    kSendRequestBodyWithoutHeaders,
    kSendResponseBodyWithoutHeaders,
    kSendResponseTrailersWithoutHeaders,
    kSendResponseTrailersInTrailersOnly,
    kSendResponseBodyInTrailersOnly,
    kSendUnexpectedRequestHeaders,
    kSendUnexpectedRequestBody,
  };

  void SetBehavior(Behavior behavior) { behavior_ = behavior; }
  void SetErrorThreshold(size_t threshold) { error_threshold_ = threshold; }

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

  bool has_protocol_config_in_request_body() {
    absl::MutexLock lock(&mu_);
    return has_protocol_config_in_request_body_;
  }

  bool has_protocol_config_in_response_body() {
    absl::MutexLock lock(&mu_);
    return has_protocol_config_in_response_body_;
  }

  bool request_headers_had_eos() {
    absl::MutexLock lock(&mu_);
    return request_headers_had_eos_;
  }

  bool response_headers_had_eos() {
    absl::MutexLock lock(&mu_);
    return response_headers_had_eos_;
  }

  std::vector<std::string> received_message_bodies() {
    absl::MutexLock lock(&mu_);
    return received_message_bodies_;
  }

  std::vector<std::string> received_response_message_bodies() {
    absl::MutexLock lock(&mu_);
    return received_response_message_bodies_;
  }

  bool received_request_headers() {
    absl::MutexLock lock(&mu_);
    return received_request_headers_;
  }

  bool received_response_headers() {
    absl::MutexLock lock(&mu_);
    return received_response_headers_;
  }

  bool received_response_trailers() {
    absl::MutexLock lock(&mu_);
    return received_response_trailers_;
  }

  bool received_end_of_stream_without_message() {
    absl::MutexLock lock(&mu_);
    return received_end_of_stream_without_message_;
  }

  grpc::Status Process(
      grpc::ServerContext* /*context*/,
      grpc::ServerReaderWriter<
          ::envoy::service::ext_proc::v3::ProcessingResponse,
          ::envoy::service::ext_proc::v3::ProcessingRequest>* stream) override {
    LOG(INFO) << "MockRequestHeadersExternalProcessorService::Process started";
    if (behavior_ == Behavior::kSendUnexpectedRequestHeaders) {
      ::envoy::service::ext_proc::v3::ProcessingResponse unsolicited_response;
      unsolicited_response.mutable_request_headers();
      stream->Write(unsolicited_response);
      return grpc::Status::OK;
    }
    if (behavior_ == Behavior::kSendUnexpectedRequestBody) {
      ::envoy::service::ext_proc::v3::ProcessingResponse unsolicited_response;
      unsolicited_response.mutable_request_body();
      stream->Write(unsolicited_response);
      return grpc::Status::OK;
    }
    if (behavior_ == Behavior::kImmediateError) {
      return grpc::Status(grpc::StatusCode::INTERNAL, "immediate error");
    }
    ::envoy::service::ext_proc::v3::ProcessingRequest request;
    while (stream->Read(&request)) {
      LOG(INFO) << "MockRequestHeadersExternalProcessorService::Process read "
                   "request: "
                << request.DebugString();
      bool should_break = false;
      {
        auto it = request.attributes().find("envoy.filters.http.ext_proc");
        if (it != request.attributes().end()) {
          absl::MutexLock lock(&mu_);
          last_received_attributes_.clear();
          const auto& fields = it->second.fields();
          for (const auto& [key, val] : fields) {
            if (val.has_string_value()) {
              last_received_attributes_[key] = val.string_value();
            }
          }
        }
      }
      if (request.has_protocol_config()) {
        absl::MutexLock lock(&mu_);
        if (request.has_request_headers()) {
          has_protocol_config_in_request_headers_ = true;
        } else if (request.has_response_headers()) {
          has_protocol_config_in_response_headers_ = true;
        } else if (request.has_request_body()) {
          has_protocol_config_in_request_body_ = true;
        } else if (request.has_response_body()) {
          has_protocol_config_in_response_body_ = true;
        }
      }
      ::envoy::service::ext_proc::v3::ProcessingResponse response;
      if (request.has_request_headers()) {
        {
          absl::MutexLock lock(&mu_);
          received_request_headers_ = true;
          request_headers_had_eos_ = request.request_headers().end_of_stream();
        }
        if (behavior_ == Behavior::kErrorOnRequestHeaders) {
          return grpc::Status(grpc::StatusCode::INTERNAL,
                              "error on request headers");
        }
        if (behavior_ == Behavior::kErrorAfterRequestHeaders) {
          response.mutable_request_headers()
              ->mutable_response()
              ->mutable_header_mutation();
          stream->Write(response);
          return grpc::Status(grpc::StatusCode::INTERNAL,
                              "error after request headers");
        }
        if (behavior_ == Behavior::kSendRequestBodyWithoutHeaders) {
          response.mutable_request_body();
        } else if (behavior_ == Behavior::kFail) {
          auto* immediate = response.mutable_immediate_response();
          immediate->mutable_status()->set_code(
              envoy::type::v3::StatusCode::Forbidden);
          immediate->mutable_grpc_status()->set_status(
              static_cast<uint32_t>(grpc::StatusCode::PERMISSION_DENIED));
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
        {
          absl::MutexLock lock(&mu_);
          received_response_headers_ = true;
          response_headers_had_eos_ =
              request.response_headers().end_of_stream();
        }
        if (behavior_ == Behavior::kSendResponseBodyWithoutHeaders) {
          response.mutable_response_body();
        } else if (behavior_ == Behavior::kSendResponseTrailersWithoutHeaders) {
          response.mutable_response_trailers();
        } else if (behavior_ == Behavior::kSendResponseTrailersInTrailersOnly) {
          response.mutable_response_trailers();
        } else if (behavior_ == Behavior::kSendResponseBodyInTrailersOnly) {
          response.mutable_response_body();
        } else if (behavior_ == Behavior::kErrorAfterResponseHeaders) {
          response.mutable_response_headers()
              ->mutable_response()
              ->mutable_header_mutation();
          stream->Write(response);
          return grpc::Status(grpc::StatusCode::INTERNAL,
                              "error after response headers");
        } else if (behavior_ == Behavior::kMutateHeaders) {
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
        {
          absl::MutexLock lock(&mu_);
          received_response_trailers_ = true;
        }
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
      } else if (request.has_request_body()) {
        size_t body_count = 0;
        {
          absl::MutexLock lock(&mu_);
          if (request.request_body().end_of_stream_without_message()) {
            received_end_of_stream_without_message_ = true;
          } else {
            received_message_bodies_.push_back(request.request_body().body());
          }
          body_count = received_message_bodies_.size();
        }
        if (behavior_ == Behavior::kUnsolicitedResponseBody) {
          auto* body_mutation = response.mutable_request_body()
                                    ->mutable_response()
                                    ->mutable_body_mutation();
          auto* streamed_response = body_mutation->mutable_streamed_response();
          streamed_response->set_end_of_stream(
              request.request_body().end_of_stream());
          streamed_response->set_end_of_stream_without_message(
              request.request_body().end_of_stream_without_message());
          stream->Write(response);
          ::envoy::service::ext_proc::v3::ProcessingResponse
              unsolicited_response;
          auto* unsolicited_body_mutation =
              unsolicited_response.mutable_request_body()
                  ->mutable_response()
                  ->mutable_body_mutation();
          auto* unsolicited_streamed_response =
              unsolicited_body_mutation->mutable_streamed_response();
          unsolicited_streamed_response->set_end_of_stream(false);
          unsolicited_streamed_response->set_end_of_stream_without_message(
              false);
          stream->Write(unsolicited_response);
          continue;
        }
        if (behavior_ == Behavior::kCopyRequestBody) {
          auto* body_mutation = response.mutable_request_body()
                                    ->mutable_response()
                                    ->mutable_body_mutation();
          auto* streamed_response = body_mutation->mutable_streamed_response();
          streamed_response->set_body(request.request_body().body());
          streamed_response->set_end_of_stream(
              request.request_body().end_of_stream());
          streamed_response->set_end_of_stream_without_message(
              request.request_body().end_of_stream_without_message());
        }
        if (behavior_ == Behavior::kStreamingUnsolicitedResponseBody) {
          auto* body_mutation = response.mutable_request_body()
                                    ->mutable_response()
                                    ->mutable_body_mutation();
          auto* streamed_response = body_mutation->mutable_streamed_response();
          streamed_response->set_body(request.request_body().body());
          streamed_response->set_end_of_stream(
              request.request_body().end_of_stream());
          streamed_response->set_end_of_stream_without_message(
              request.request_body().end_of_stream_without_message());
          stream->Write(response);
          if (body_count == 2) {
            ::envoy::service::ext_proc::v3::ProcessingResponse
                unsolicited_response;
            auto* unsolicited_body_mutation =
                unsolicited_response.mutable_request_body()
                    ->mutable_response()
                    ->mutable_body_mutation();
            auto* unsolicited_streamed_response =
                unsolicited_body_mutation->mutable_streamed_response();
            unsolicited_streamed_response->set_end_of_stream(false);
            unsolicited_streamed_response->set_end_of_stream_without_message(
                false);
            stream->Write(unsolicited_response);
          }
          continue;
        }
        if (behavior_ == Behavior::kRequestBodyContinueAndReplace) {
          auto* response_body = response.mutable_request_body();
          auto* common_response = response_body->mutable_response();
          common_response->set_status(::envoy::service::ext_proc::v3::
                                          CommonResponse::CONTINUE_AND_REPLACE);
        }
        if (behavior_ == Behavior::kRequestBodyGrpcMessageCompressed) {
          auto* body_mutation = response.mutable_request_body()
                                    ->mutable_response()
                                    ->mutable_body_mutation();
          auto* streamed_response = body_mutation->mutable_streamed_response();
          streamed_response->set_grpc_message_compressed(true);
        }
        if (behavior_ == Behavior::kStreamingEarlyHalfClose) {
          auto* body_mutation = response.mutable_request_body()
                                    ->mutable_response()
                                    ->mutable_body_mutation();
          auto* streamed_response = body_mutation->mutable_streamed_response();
          streamed_response->set_body(request.request_body().body());
          streamed_response->set_end_of_stream(true);
          streamed_response->set_end_of_stream_without_message(false);
          stream->Write(response);
          continue;
        }
        if (behavior_ == Behavior::kStreamingEarlyHalfCloseWithoutMessage) {
          auto* body_mutation = response.mutable_request_body()
                                    ->mutable_response()
                                    ->mutable_body_mutation();
          auto* streamed_response = body_mutation->mutable_streamed_response();
          streamed_response->set_end_of_stream(false);
          streamed_response->set_end_of_stream_without_message(true);
          stream->Write(response);
          continue;
        }
        if (behavior_ == Behavior::kErrorOnRequestBody) {
          return grpc::Status(grpc::StatusCode::INTERNAL,
                              "error on request body");
        }
        if (behavior_ == Behavior::kErrorMidCall &&
            body_count >= error_threshold_) {
          return grpc::Status(grpc::StatusCode::INTERNAL, "error mid call");
        }
        if (behavior_ == Behavior::kMutateRequestBody ||
            behavior_ == Behavior::kCloseGracefullyMidCall ||
            (behavior_ == Behavior::kErrorMidCall &&
             body_count < error_threshold_)) {
          auto* body_mutation = response.mutable_request_body()
                                    ->mutable_response()
                                    ->mutable_body_mutation();
          auto* streamed_response = body_mutation->mutable_streamed_response();
          if (!request.request_body().end_of_stream_without_message()) {
            EchoRequest mutated_request;
            mutated_request.set_message("e2e-mutated-body");
            std::string serialized_mutated_request;
            mutated_request.SerializeToString(&serialized_mutated_request);
            streamed_response->set_body(serialized_mutated_request);
          }
          streamed_response->set_end_of_stream(
              request.request_body().end_of_stream());
          streamed_response->set_end_of_stream_without_message(
              request.request_body().end_of_stream_without_message());
          if (behavior_ == Behavior::kCloseGracefullyMidCall) {
            should_break = true;
          }
        } else {
          auto* body_mutation = response.mutable_request_body()
                                    ->mutable_response()
                                    ->mutable_body_mutation();
          auto* streamed_response = body_mutation->mutable_streamed_response();
          streamed_response->set_end_of_stream(
              request.request_body().end_of_stream());
          streamed_response->set_end_of_stream_without_message(
              request.request_body().end_of_stream_without_message());
        }
      } else if (request.has_response_body()) {
        size_t response_body_count = 0;
        {
          absl::MutexLock lock(&mu_);
          if (!request.response_body().end_of_stream_without_message()) {
            received_response_message_bodies_.push_back(
                request.response_body().body());
          }
          response_body_count = received_response_message_bodies_.size();
        }
        if (behavior_ == Behavior::kErrorOnResponseBody) {
          return grpc::Status(grpc::StatusCode::INTERNAL,
                              "error on response body");
        }
        if (behavior_ == Behavior::kErrorMidCall &&
            response_body_count >= error_threshold_) {
          return grpc::Status(grpc::StatusCode::INTERNAL, "error mid call");
        }
        if (behavior_ == Behavior::kCopyResponseBody ||
            (behavior_ == Behavior::kHalfCloseResponseBodyMidCall &&
             response_body_count < error_threshold_)) {
          auto* body_mutation = response.mutable_response_body()
                                    ->mutable_response()
                                    ->mutable_body_mutation();
          auto* streamed_response = body_mutation->mutable_streamed_response();
          streamed_response->set_body(request.response_body().body());
          streamed_response->set_end_of_stream(
              request.response_body().end_of_stream());
          streamed_response->set_end_of_stream_without_message(
              request.response_body().end_of_stream_without_message());
        } else if (behavior_ == Behavior::kHalfCloseResponseBodyMidCall &&
                   response_body_count >= error_threshold_) {
          auto* body_mutation = response.mutable_response_body()
                                    ->mutable_response()
                                    ->mutable_body_mutation();
          auto* streamed_response = body_mutation->mutable_streamed_response();
          streamed_response->set_body(request.response_body().body());
          streamed_response->set_end_of_stream(true);  // Force EOS
          streamed_response->set_end_of_stream_without_message(false);
        } else if (behavior_ == Behavior::kMutateResponseBody ||
                   behavior_ == Behavior::kCloseGracefullyMidCall ||
                   (behavior_ == Behavior::kErrorMidCall &&
                    response_body_count < error_threshold_)) {
          auto* body_mutation = response.mutable_response_body()
                                    ->mutable_response()
                                    ->mutable_body_mutation();
          auto* streamed_response = body_mutation->mutable_streamed_response();
          if (!request.response_body().end_of_stream_without_message()) {
            EchoResponse mutated_response;
            mutated_response.set_message("e2e-mutated-response-body");
            std::string serialized_mutated_response;
            mutated_response.SerializeToString(&serialized_mutated_response);
            streamed_response->set_body(serialized_mutated_response);
          }
          streamed_response->set_end_of_stream(
              request.response_body().end_of_stream());
          streamed_response->set_end_of_stream_without_message(
              request.response_body().end_of_stream_without_message());
          if (behavior_ == Behavior::kCloseGracefullyMidCall) {
            should_break = true;
          }
        } else {
          auto* body_mutation = response.mutable_response_body()
                                    ->mutable_response()
                                    ->mutable_body_mutation();
          auto* streamed_response = body_mutation->mutable_streamed_response();
          streamed_response->set_end_of_stream(
              request.response_body().end_of_stream());
          streamed_response->set_end_of_stream_without_message(
              request.response_body().end_of_stream_without_message());
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
      if (should_break) {
        break;
      }
    }
    LOG(INFO) << "MockRequestHeadersExternalProcessorService::Process finished";
    return grpc::Status::OK;
  }

 private:
  Behavior behavior_ = Behavior::kContinue;
  size_t error_threshold_ = 2;
  absl::Mutex mu_;
  std::map<std::string, std::string> last_received_attributes_
      ABSL_GUARDED_BY(mu_);
  bool has_protocol_config_in_request_headers_ ABSL_GUARDED_BY(mu_) = false;
  bool has_protocol_config_in_response_headers_ ABSL_GUARDED_BY(mu_) = false;
  bool has_protocol_config_in_request_body_ ABSL_GUARDED_BY(mu_) = false;
  bool has_protocol_config_in_response_body_ ABSL_GUARDED_BY(mu_) = false;
  bool request_headers_had_eos_ ABSL_GUARDED_BY(mu_) = false;
  bool response_headers_had_eos_ ABSL_GUARDED_BY(mu_) = false;
  std::vector<std::string> received_message_bodies_ ABSL_GUARDED_BY(mu_);
  std::vector<std::string> received_response_message_bodies_
      ABSL_GUARDED_BY(mu_);
  bool received_request_headers_ ABSL_GUARDED_BY(mu_) = false;
  bool received_response_headers_ ABSL_GUARDED_BY(mu_) = false;
  bool received_response_trailers_ ABSL_GUARDED_BY(mu_) = false;
  bool received_end_of_stream_without_message_ ABSL_GUARDED_BY(mu_) = false;
};

}  // namespace
}  // namespace testing
}  // namespace grpc

int main(int argc, char** argv) {
  grpc_core::SetEnv("GRPC_EXPERIMENTAL_XDS_EXT_PROC_ON_CLIENT", "true");
  grpc_core::SetEnv("GRPC_TRACE",
                    "call_state,promise_primitives,ext_proc_filter");
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
