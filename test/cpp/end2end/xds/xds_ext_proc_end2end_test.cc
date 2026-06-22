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
  EXPECT_EQ(status.error_code(), grpc::StatusCode::INTERNAL);
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
  EXPECT_EQ(status.error_code(), grpc::StatusCode::INTERNAL);
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
  EXPECT_EQ(status.error_code(), grpc::StatusCode::INTERNAL);
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
  EXPECT_EQ(status.error_code(), grpc::StatusCode::INTERNAL);
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

TEST_P(XdsExtProcEnd2endTest,
       ClientToServerMessageMutatedWhenRequestBodyProcessingModeIsGrpc) {
  int ext_proc_port = grpc_pick_unused_port_or_die();
  std::string server_address = absl::StrCat("localhost:", ext_proc_port);
  MockRequestHeadersExternalProcessorService service;
  service.SetBehavior(
      MockRequestHeadersExternalProcessorService::Behavior::kMutateRequestBody);
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
          .SetRequestBodyMode(envoy::extensions::filters::http::ext_proc::v3::
                                  ProcessingMode::GRPC)
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
  ClientContext context;
  EchoRequest request;
  request.set_message("original-body");
  EchoResponse response;
  Status status = stub_->Echo(&context, request, &response);
  EXPECT_TRUE(status.ok()) << status.error_message();
  EXPECT_EQ(response.message(), "e2e-mutated-body");
  server->Shutdown();
}

TEST_P(XdsExtProcEnd2endTest,
       ClientToServerMessageRequestWhenObservabilityModeIsEnabled) {
  int ext_proc_port = grpc_pick_unused_port_or_die();
  std::string server_address = absl::StrCat("localhost:", ext_proc_port);
  MockRequestHeadersExternalProcessorService service;
  service.SetBehavior(
      MockRequestHeadersExternalProcessorService::Behavior::kMutateRequestBody);
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
          .SetRequestBodyMode(envoy::extensions::filters::http::ext_proc::v3::
                                  ProcessingMode::GRPC)
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
  ClientContext context;
  EchoRequest request;
  request.set_message("original-body");
  EchoResponse response;
  Status status = stub_->Echo(&context, request, &response);
  EXPECT_TRUE(status.ok()) << status.error_message();
  EXPECT_EQ(response.message(), "original-body");
  server->Shutdown();
}

TEST_P(XdsExtProcEnd2endTest, ClientToServerUnsolicitedResponseBodyFailsCall) {
  int ext_proc_port = grpc_pick_unused_port_or_die();
  std::string server_address = absl::StrCat("localhost:", ext_proc_port);
  MockRequestHeadersExternalProcessorService service;
  service.SetBehavior(MockRequestHeadersExternalProcessorService::Behavior::
                          kUnsolicitedResponseBody);
  ServerBuilder builder;
  builder.AddListeningPort(server_address, InsecureServerCredentials());
  builder.RegisterService(&service);
  std::unique_ptr<Server> server = builder.BuildAndStart();
  ASSERT_NE(server, nullptr);
  auto ext_proc =
      ExternalProcessorBuilder()
          .SetTargetUri(absl::StrCat("dns:localhost:", ext_proc_port))
          .SetInsecureChannelCredentials()
          .SetFailureModeAllow(false)
          .SetRequestHeaderMode(envoy::extensions::filters::http::ext_proc::v3::
                                    ProcessingMode::SKIP)
          .SetResponseHeaderMode(envoy::extensions::filters::http::ext_proc::
                                     v3::ProcessingMode::SKIP)
          .SetRequestBodyMode(envoy::extensions::filters::http::ext_proc::v3::
                                  ProcessingMode::GRPC)
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
  ClientContext context;
  EchoRequest request;
  request.set_message("msg1");
  EchoResponse response;
  Status status = stub_->Echo(&context, request, &response);
  EXPECT_FALSE(status.ok());
  EXPECT_EQ(status.error_code(), StatusCode::INTERNAL);
  EXPECT_EQ(
      status.error_message(),
      "Received unexpected request body response from external processor");
  server->Shutdown();
}

TEST_P(XdsExtProcEnd2endTest, ClientToServerMultipleMessagesSuccess) {
  int ext_proc_port = grpc_pick_unused_port_or_die();
  std::string server_address = absl::StrCat("localhost:", ext_proc_port);
  MockRequestHeadersExternalProcessorService service;
  service.SetBehavior(
      MockRequestHeadersExternalProcessorService::Behavior::kCopyRequestBody);
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
          .SetRequestBodyMode(envoy::extensions::filters::http::ext_proc::v3::
                                  ProcessingMode::GRPC)
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
  ClientContext context;
  auto stream = stub_->BidiStream(&context);
  ASSERT_NE(stream, nullptr);
  for (int i = 0; i < 3; ++i) {
    EchoRequest request;
    request.set_message(absl::StrCat("msg", i));
    ASSERT_TRUE(stream->Write(request));
    EchoResponse response;
    ASSERT_TRUE(stream->Read(&response));
    EXPECT_EQ(response.message(), absl::StrCat("msg", i));
  }
  ASSERT_TRUE(stream->WritesDone());
  EchoResponse response;
  ASSERT_FALSE(stream->Read(&response));
  Status status = stream->Finish();
  EXPECT_TRUE(status.ok()) << status.error_message();
  auto received_bodies = service.received_message_bodies();
  ASSERT_GE(received_bodies.size(), 3);
  for (int i = 0; i < 3; ++i) {
    EchoRequest received_request;
    ASSERT_TRUE(received_request.ParseFromString(received_bodies[i]));
    EXPECT_EQ(received_request.message(), absl::StrCat("msg", i));
  }
  server->Shutdown();
}

TEST_P(XdsExtProcEnd2endTest,
       ClientToServerMessageNotSentWhenRequestBodyProcessingModeIsNone) {
  int ext_proc_port = grpc_pick_unused_port_or_die();
  std::string server_address = absl::StrCat("localhost:", ext_proc_port);
  MockRequestHeadersExternalProcessorService service;
  service.SetBehavior(
      MockRequestHeadersExternalProcessorService::Behavior::kMutateRequestBody);
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
          .SetRequestBodyMode(envoy::extensions::filters::http::ext_proc::v3::
                                  ProcessingMode::NONE)
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
  ClientContext context;
  EchoRequest request;
  request.set_message("original-body");
  EchoResponse response;
  Status status = stub_->Echo(&context, request, &response);
  EXPECT_TRUE(status.ok()) << status.error_message();
  EXPECT_EQ(response.message(), "original-body");
  auto received_bodies = service.received_message_bodies();
  EXPECT_TRUE(received_bodies.empty());
  server->Shutdown();
}

TEST_P(XdsExtProcEnd2endTest,
       RequestAttributesSentInRequestBodyWhenRequestHeaderIsSkip) {
  int ext_proc_port = grpc_pick_unused_port_or_die();
  std::string server_address = absl::StrCat("localhost:", ext_proc_port);
  MockRequestHeadersExternalProcessorService service;
  service.SetBehavior(
      MockRequestHeadersExternalProcessorService::Behavior::kMutateRequestBody);
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
          .SetRequestBodyMode(envoy::extensions::filters::http::ext_proc::v3::
                                  ProcessingMode::GRPC)
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
  ClientContext context;
  EchoRequest request;
  request.set_message("original-body");
  EchoResponse response;
  Status status = stub_->Echo(&context, request, &response);
  EXPECT_TRUE(status.ok()) << status.error_message();
  EXPECT_EQ(response.message(), "e2e-mutated-body");
  auto attrs = service.last_received_attributes();
  EXPECT_EQ(attrs["request.path"], "/grpc.testing.EchoTestService/Echo");
  EXPECT_FALSE(attrs["request.host"].empty());
  server->Shutdown();
}

TEST_P(XdsExtProcEnd2endTest,
       ClientToServerMessageErrorOnRequestBodyWhenFailureModeAllowIsFalse) {
  int ext_proc_port = grpc_pick_unused_port_or_die();
  std::string server_address = absl::StrCat("localhost:", ext_proc_port);
  MockRequestHeadersExternalProcessorService service;
  service.SetBehavior(MockRequestHeadersExternalProcessorService::Behavior::
                          kErrorOnRequestBody);
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
          .SetRequestBodyMode(envoy::extensions::filters::http::ext_proc::v3::
                                  ProcessingMode::GRPC)
          .SetFailureModeAllow(false)
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
  ClientContext context;
  EchoRequest request;
  request.set_message("original-body");
  EchoResponse response;
  Status status = stub_->Echo(&context, request, &response);
  EXPECT_FALSE(status.ok());
  EXPECT_EQ(status.error_code(), StatusCode::INTERNAL);
  EXPECT_EQ(status.error_message(), "error on request body");
  server->Shutdown();
}

TEST_P(XdsExtProcEnd2endTest,
       ClientToServerMessageErrorOnRequestBodyWhenFailureModeAllowIsTrue) {
  int ext_proc_port = grpc_pick_unused_port_or_die();
  std::string server_address = absl::StrCat("localhost:", ext_proc_port);
  MockRequestHeadersExternalProcessorService service;
  service.SetBehavior(MockRequestHeadersExternalProcessorService::Behavior::
                          kErrorOnRequestBody);
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
          .SetRequestBodyMode(envoy::extensions::filters::http::ext_proc::v3::
                                  ProcessingMode::GRPC)
          .SetFailureModeAllow(true)
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
  ClientContext context;
  EchoRequest request;
  request.set_message("original-body");
  EchoResponse response;
  Status status = stub_->Echo(&context, request, &response);
  EXPECT_FALSE(status.ok());
  EXPECT_EQ(status.error_code(), StatusCode::INTERNAL);
  EXPECT_EQ(status.error_message(), "error on request body");
  server->Shutdown();
}

TEST_P(XdsExtProcEnd2endTest, ClientToServerEmptyMessageBodyHandled) {
  int ext_proc_port = grpc_pick_unused_port_or_die();
  std::string server_address = absl::StrCat("localhost:", ext_proc_port);
  MockRequestHeadersExternalProcessorService service;
  service.SetBehavior(
      MockRequestHeadersExternalProcessorService::Behavior::kMutateRequestBody);
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
          .SetRequestBodyMode(envoy::extensions::filters::http::ext_proc::v3::
                                  ProcessingMode::GRPC)
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
  ClientContext context;
  EchoRequest request;
  request.set_message("");
  EchoResponse response;
  Status status = stub_->Echo(&context, request, &response);
  EXPECT_TRUE(status.ok()) << status.error_message();
  EXPECT_EQ(response.message(), "e2e-mutated-body");
  auto received_bodies = service.received_message_bodies();
  ASSERT_GE(received_bodies.size(), 1);
  EXPECT_EQ(received_bodies[0], "");
  server->Shutdown();
}

TEST_P(XdsExtProcEnd2endTest, ClientToServerProcessorGracefulCloseMidCall) {
  int ext_proc_port = grpc_pick_unused_port_or_die();
  std::string server_address = absl::StrCat("localhost:", ext_proc_port);
  MockRequestHeadersExternalProcessorService service;
  service.SetBehavior(MockRequestHeadersExternalProcessorService::Behavior::
                          kCloseGracefullyMidCall);
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
          .SetRequestBodyMode(envoy::extensions::filters::http::ext_proc::v3::
                                  ProcessingMode::GRPC)
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
  ClientContext context;
  auto stream = stub_->BidiStream(&context);
  ASSERT_NE(stream, nullptr);
  // Send first message. It should be mutated.
  EchoRequest request;
  request.set_message("msg1");
  ASSERT_TRUE(stream->Write(request));
  EchoResponse response;
  ASSERT_TRUE(stream->Read(&response));
  EXPECT_EQ(response.message(), "e2e-mutated-body");
  // Send second message. It should bypass processor and NOT be mutated.
  request.set_message("msg2");
  ASSERT_TRUE(stream->Write(request));
  ASSERT_TRUE(stream->Read(&response));
  EXPECT_EQ(response.message(), "msg2");
  ASSERT_TRUE(stream->WritesDone());
  Status status = stream->Finish();
  EXPECT_TRUE(status.ok()) << status.error_message();
  auto received_bodies = service.received_message_bodies();
  // Processor should have only received the first message because it closed
  // after it.
  ASSERT_EQ(received_bodies.size(), 1);
  EchoRequest received_request;
  ASSERT_TRUE(received_request.ParseFromString(received_bodies[0]));
  EXPECT_EQ(received_request.message(), "msg1");
  server->Shutdown();
}

TEST_P(XdsExtProcEnd2endTest, ClientToServerRequestBodySkip) {
  int ext_proc_port = grpc_pick_unused_port_or_die();
  std::string server_address = absl::StrCat("localhost:", ext_proc_port);
  MockRequestHeadersExternalProcessorService service;
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
          .SetRequestBodyMode(envoy::extensions::filters::http::ext_proc::v3::
                                  ProcessingMode::NONE)
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
  ClientContext context;
  EchoRequest request;
  request.set_message("original-body");
  EchoResponse response;
  Status status = stub_->Echo(&context, request, &response);
  EXPECT_TRUE(status.ok()) << status.error_message();
  EXPECT_EQ(response.message(), "original-body");
  auto received_bodies = service.received_message_bodies();
  EXPECT_TRUE(received_bodies.empty());
  server->Shutdown();
}

TEST_P(XdsExtProcEnd2endTest,
       ClientToServerProcessorErrorMidCallWhenFailureModeAllowIsTrue) {
  int ext_proc_port = grpc_pick_unused_port_or_die();
  std::string server_address = absl::StrCat("localhost:", ext_proc_port);
  MockRequestHeadersExternalProcessorService service;
  service.SetBehavior(
      MockRequestHeadersExternalProcessorService::Behavior::kErrorMidCall);
  ServerBuilder builder;
  builder.AddListeningPort(server_address, InsecureServerCredentials());
  builder.RegisterService(&service);
  std::unique_ptr<Server> server = builder.BuildAndStart();
  ASSERT_NE(server, nullptr);
  auto ext_proc =
      ExternalProcessorBuilder()
          .SetTargetUri(absl::StrCat("dns:localhost:", ext_proc_port))
          .SetInsecureChannelCredentials()
          .SetFailureModeAllow(true)
          .SetRequestHeaderMode(envoy::extensions::filters::http::ext_proc::v3::
                                    ProcessingMode::SKIP)
          .SetResponseHeaderMode(envoy::extensions::filters::http::ext_proc::
                                     v3::ProcessingMode::SKIP)
          .SetRequestBodyMode(envoy::extensions::filters::http::ext_proc::v3::
                                  ProcessingMode::GRPC)
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
  ClientContext context;
  auto stream = stub_->BidiStream(&context);
  ASSERT_NE(stream, nullptr);
  // Send first message. It should be mutated.
  EchoRequest request;
  request.set_message("msg1");
  ASSERT_TRUE(stream->Write(request));
  EchoResponse response;
  ASSERT_TRUE(stream->Read(&response));
  EXPECT_EQ(response.message(), "e2e-mutated-body");
  // Send second message. It should trigger error.
  request.set_message("msg2");
  stream->Write(request);
  // We expect the stream to fail.
  ASSERT_FALSE(stream->Read(&response));
  Status status = stream->Finish();
  EXPECT_FALSE(status.ok());
  EXPECT_EQ(status.error_code(), StatusCode::INTERNAL);
  EXPECT_EQ(status.error_message(), "error mid call");
  auto received_bodies = service.received_message_bodies();
  // Processor should have received both messages.
  ASSERT_EQ(received_bodies.size(), 2);
  EchoRequest received_request;
  ASSERT_TRUE(received_request.ParseFromString(received_bodies[0]));
  EXPECT_EQ(received_request.message(), "msg1");
  ASSERT_TRUE(received_request.ParseFromString(received_bodies[1]));
  EXPECT_EQ(received_request.message(), "msg2");
  server->Shutdown();
}

TEST_P(XdsExtProcEnd2endTest,
       ClientToServerHeaderErrorWhenFailureModeAllowIsFalse) {
  int ext_proc_port = grpc_pick_unused_port_or_die();
  std::string server_address = absl::StrCat("localhost:", ext_proc_port);
  MockRequestHeadersExternalProcessorService service;
  service.SetBehavior(MockRequestHeadersExternalProcessorService::Behavior::
                          kErrorOnRequestHeaders);
  ServerBuilder builder;
  builder.AddListeningPort(server_address, InsecureServerCredentials());
  builder.RegisterService(&service);
  std::unique_ptr<Server> server = builder.BuildAndStart();
  ASSERT_NE(server, nullptr);
  auto ext_proc =
      ExternalProcessorBuilder()
          .SetTargetUri(absl::StrCat("dns:localhost:", ext_proc_port))
          .SetInsecureChannelCredentials()
          .SetFailureModeAllow(false)
          .SetRequestHeaderMode(envoy::extensions::filters::http::ext_proc::v3::
                                    ProcessingMode::SEND)
          .SetResponseHeaderMode(envoy::extensions::filters::http::ext_proc::
                                     v3::ProcessingMode::SKIP)
          .SetRequestBodyMode(envoy::extensions::filters::http::ext_proc::v3::
                                  ProcessingMode::NONE)
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
  ClientContext context;
  EchoRequest request;
  request.set_message("original-body");
  EchoResponse response;
  Status status = stub_->Echo(&context, request, &response);
  EXPECT_FALSE(status.ok());
  EXPECT_EQ(status.error_code(), StatusCode::INTERNAL);
  EXPECT_EQ(status.error_message(), "error on request headers");
  server->Shutdown();
}

TEST_P(XdsExtProcEnd2endTest,
       ClientToServerHeaderErrorWhenFailureModeAllowIsTrue) {
  int ext_proc_port = grpc_pick_unused_port_or_die();
  std::string server_address = absl::StrCat("localhost:", ext_proc_port);
  MockRequestHeadersExternalProcessorService service;
  service.SetBehavior(MockRequestHeadersExternalProcessorService::Behavior::
                          kErrorOnRequestHeaders);
  ServerBuilder builder;
  builder.AddListeningPort(server_address, InsecureServerCredentials());
  builder.RegisterService(&service);
  std::unique_ptr<Server> server = builder.BuildAndStart();
  ASSERT_NE(server, nullptr);
  auto ext_proc =
      ExternalProcessorBuilder()
          .SetTargetUri(absl::StrCat("dns:localhost:", ext_proc_port))
          .SetInsecureChannelCredentials()
          .SetFailureModeAllow(true)
          .SetRequestHeaderMode(envoy::extensions::filters::http::ext_proc::v3::
                                    ProcessingMode::SEND)
          .SetResponseHeaderMode(envoy::extensions::filters::http::ext_proc::
                                     v3::ProcessingMode::SKIP)
          .SetRequestBodyMode(envoy::extensions::filters::http::ext_proc::v3::
                                  ProcessingMode::NONE)
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
  ClientContext context;
  EchoRequest request;
  request.set_message("original-body");
  EchoResponse response;
  Status status = stub_->Echo(&context, request, &response);
  EXPECT_TRUE(status.ok()) << status.error_message();
  EXPECT_EQ(response.message(), "original-body");
  server->Shutdown();
}
TEST_P(XdsExtProcEnd2endTest,
       ClientToServerStreamingUnsolicitedRequestBodyFailsCall) {
  int ext_proc_port = grpc_pick_unused_port_or_die();
  std::string server_address = absl::StrCat("localhost:", ext_proc_port);
  MockRequestHeadersExternalProcessorService service;
  service.SetBehavior(MockRequestHeadersExternalProcessorService::Behavior::
                          kStreamingUnsolicitedResponseBody);
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
          .SetRequestBodyMode(envoy::extensions::filters::http::ext_proc::v3::
                                  ProcessingMode::GRPC)
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
  ClientContext context;
  auto stream = stub_->BidiStream(&context);
  ASSERT_NE(stream, nullptr);
  EchoRequest request;
  request.set_message("msg0");
  ASSERT_TRUE(stream->Write(request));
  EchoResponse response;
  ASSERT_TRUE(stream->Read(&response));
  EXPECT_EQ(response.message(), "msg0");
  request.set_message("msg1");
  if (stream->Write(request)) {
    if (stream->Read(&response)) {
      EXPECT_EQ(response.message(), "msg1");
      ASSERT_FALSE(stream->Read(&response));
    }
  }
  Status status = stream->Finish();
  EXPECT_FALSE(status.ok());
  EXPECT_EQ(status.error_code(), StatusCode::INTERNAL);
  EXPECT_NE(
      status.error_message().find(
          "Received unexpected request body response from external processor"),
      std::string::npos)
      << "Actual error message: " << status.error_message();
  server->Shutdown();
}
TEST_P(XdsExtProcEnd2endTest,
       ClientToServerRequestBodyContinueAndReplaceFailsCall) {
  int ext_proc_port = grpc_pick_unused_port_or_die();
  std::string server_address = absl::StrCat("localhost:", ext_proc_port);
  MockRequestHeadersExternalProcessorService service;
  service.SetBehavior(MockRequestHeadersExternalProcessorService::Behavior::
                          kRequestBodyContinueAndReplace);
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
          .SetRequestBodyMode(envoy::extensions::filters::http::ext_proc::v3::
                                  ProcessingMode::GRPC)
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
  ClientContext context;
  EchoRequest request;
  request.set_message("msg1");
  EchoResponse response;
  Status status = stub_->Echo(&context, request, &response);
  EXPECT_FALSE(status.ok());
  EXPECT_EQ(status.error_code(), StatusCode::INTERNAL);
  EXPECT_EQ(status.error_message(), "CONTINUE_AND_REPLACE is not supported");
  server->Shutdown();
}
TEST_P(XdsExtProcEnd2endTest,
       ClientToServerRequestBodyGrpcMessageCompressedFailsCall) {
  int ext_proc_port = grpc_pick_unused_port_or_die();
  std::string server_address = absl::StrCat("localhost:", ext_proc_port);
  MockRequestHeadersExternalProcessorService service;
  service.SetBehavior(MockRequestHeadersExternalProcessorService::Behavior::
                          kRequestBodyGrpcMessageCompressed);
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
          .SetRequestBodyMode(envoy::extensions::filters::http::ext_proc::v3::
                                  ProcessingMode::GRPC)
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
  ClientContext context;
  EchoRequest request;
  request.set_message("msg1");
  EchoResponse response;
  Status status = stub_->Echo(&context, request, &response);
  EXPECT_FALSE(status.ok());
  EXPECT_EQ(status.error_code(), StatusCode::INTERNAL);
  EXPECT_EQ(status.error_message(), "grpc_message_compressed is not supported");
  server->Shutdown();
}
TEST_P(XdsExtProcEnd2endTest,
       ClientToServerStreamingEarlyHalfCloseFailsSubsequentWrites) {
  int ext_proc_port = grpc_pick_unused_port_or_die();
  std::string server_address = absl::StrCat("localhost:", ext_proc_port);
  MockRequestHeadersExternalProcessorService service;
  service.SetBehavior(MockRequestHeadersExternalProcessorService::Behavior::
                          kStreamingEarlyHalfClose);
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
          .SetRequestBodyMode(envoy::extensions::filters::http::ext_proc::v3::
                                  ProcessingMode::GRPC)
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
  ClientContext context;
  auto stream = stub_->BidiStream(&context);
  EchoRequest request;
  request.set_message("msg0");
  ASSERT_TRUE(stream->Write(request));
  EchoResponse response;
  ASSERT_TRUE(stream->Read(&response));
  EXPECT_EQ(response.message(), "msg0");
  request.set_message("msg1");
  // This will fail (return false) instead of crashing.
  EXPECT_FALSE(stream->Write(request));
  ASSERT_FALSE(stream->Read(&response));
  Status status = stream->Finish();
  EXPECT_FALSE(status.ok());
  EXPECT_EQ(status.error_code(), StatusCode::INTERNAL);
  EXPECT_EQ(status.error_message(),
            "Client sends closed by external processor");
  server->Shutdown();
}

TEST_P(
    XdsExtProcEnd2endTest,
    ClientToServerStreamingEarlyHalfCloseWithoutMessageFailsSubsequentWrites) {
  int ext_proc_port = grpc_pick_unused_port_or_die();
  std::string server_address = absl::StrCat("localhost:", ext_proc_port);
  MockRequestHeadersExternalProcessorService service;
  service.SetBehavior(MockRequestHeadersExternalProcessorService::Behavior::
                          kStreamingEarlyHalfCloseWithoutMessage);
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
          .SetRequestBodyMode(envoy::extensions::filters::http::ext_proc::v3::
                                  ProcessingMode::GRPC)
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
  ClientContext context;
  auto stream = stub_->BidiStream(&context);
  EchoRequest request;
  request.set_message("msg0");
  // Write might return false because the filter fails the call immediately.
  stream->Write(request);
  EchoResponse response;
  // Read should return false (call failed)
  ASSERT_FALSE(stream->Read(&response));
  Status status = stream->Finish();
  EXPECT_FALSE(status.ok());
  EXPECT_EQ(status.error_code(), StatusCode::INTERNAL);
  EXPECT_EQ(status.error_message(),
            "end_of_stream_without_message not supported");
  server->Shutdown();
}

TEST_P(XdsExtProcEnd2endTest,
       ServerToClientMessageMutatedWhenResponseBodyProcessingModeIsGrpc) {
  int ext_proc_port = grpc_pick_unused_port_or_die();
  std::string server_address = absl::StrCat("localhost:", ext_proc_port);
  MockRequestHeadersExternalProcessorService service;
  service.SetBehavior(MockRequestHeadersExternalProcessorService::Behavior::
                          kMutateResponseBody);
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
          .SetResponseBodyMode(envoy::extensions::filters::http::ext_proc::v3::
                                   ProcessingMode::GRPC)
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
  ClientContext context;
  EchoRequest request;
  request.set_message("original-body");
  EchoResponse response;
  Status status = stub_->Echo(&context, request, &response);
  EXPECT_TRUE(status.ok()) << status.error_message();
  EXPECT_EQ(response.message(), "e2e-mutated-response-body");
  server->Shutdown();
}

TEST_P(
    XdsExtProcEnd2endTest,
    ServerToClientTrailerOnlyResponseNotMutatedWhenResponseTrailerProcessingModeIsSend) {
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
          .SetResponseBodyMode(envoy::extensions::filters::http::ext_proc::v3::
                                   ProcessingMode::GRPC)
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
  ClientContext context;
  EchoRequest request;
  request.mutable_param()->mutable_expected_error()->set_code(
      static_cast<int>(StatusCode::UNAVAILABLE));
  request.mutable_param()->mutable_expected_error()->set_error_message(
      "backend-error");
  EchoResponse response;
  Status status = stub_->Echo(&context, request, &response);
  EXPECT_FALSE(status.ok());
  EXPECT_EQ(status.error_code(), StatusCode::UNAVAILABLE);
  EXPECT_EQ(status.error_message(), "backend-error");
  auto trailer = context.GetServerTrailingMetadata();
  auto it = trailer.find("x-ext-proc-test-trailer");
  // Non-OK trailers should NOT be sent to ext_proc, so they shouldn't be
  // mutated.
  EXPECT_EQ(it, trailer.end());
  server->Shutdown();
}

TEST_P(XdsExtProcEnd2endTest,
       ServerToClientTrailerMutatedWhenResponseTrailerProcessingModeIsSend) {
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
          .SetResponseBodyMode(envoy::extensions::filters::http::ext_proc::v3::
                                   ProcessingMode::NONE)
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
  ClientContext context;
  EchoRequest request;
  request.set_message("hello");
  EchoResponse response;
  Status status = stub_->Echo(&context, request, &response);
  EXPECT_TRUE(status.ok()) << status.error_message();
  auto trailer = context.GetServerTrailingMetadata();
  auto it = trailer.find("x-ext-proc-test-trailer");
  ASSERT_NE(it, trailer.end());
  EXPECT_EQ(std::string(it->second.data(), it->second.size()),
            "e2e-modified-trailer");
  server->Shutdown();
}

TEST_P(XdsExtProcEnd2endTest,
       ServerToClientMessageRequestWhenObservabilityModeIsEnabled) {
  int ext_proc_port = grpc_pick_unused_port_or_die();
  std::string server_address = absl::StrCat("localhost:", ext_proc_port);
  MockRequestHeadersExternalProcessorService service;
  service.SetBehavior(MockRequestHeadersExternalProcessorService::Behavior::
                          kMutateResponseBody);
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
          .SetResponseBodyMode(envoy::extensions::filters::http::ext_proc::v3::
                                   ProcessingMode::GRPC)
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
  ClientContext context;
  EchoRequest request;
  request.set_message("original-body");
  EchoResponse response;
  Status status = stub_->Echo(&context, request, &response);
  EXPECT_TRUE(status.ok()) << status.error_message();
  EXPECT_EQ(response.message(), "original-body");
  // Verify that the processor received the response body.
  auto received_bodies = service.received_response_message_bodies();
  ASSERT_EQ(received_bodies.size(), 1);
  EchoResponse received_response;
  ASSERT_TRUE(received_response.ParseFromString(received_bodies[0]));
  EXPECT_EQ(received_response.message(), "original-body");
  server->Shutdown();
}

TEST_P(XdsExtProcEnd2endTest,
       ServerToClientMessageNotSentWhenResponseBodyProcessingModeIsNone) {
  int ext_proc_port = grpc_pick_unused_port_or_die();
  std::string server_address = absl::StrCat("localhost:", ext_proc_port);
  MockRequestHeadersExternalProcessorService service;
  service.SetBehavior(MockRequestHeadersExternalProcessorService::Behavior::
                          kMutateResponseBody);
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
          .SetResponseBodyMode(envoy::extensions::filters::http::ext_proc::v3::
                                   ProcessingMode::NONE)
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
  ClientContext context;
  EchoRequest request;
  request.set_message("original-body");
  EchoResponse response;
  Status status = stub_->Echo(&context, request, &response);
  EXPECT_TRUE(status.ok()) << status.error_message();
  EXPECT_EQ(response.message(), "original-body");
  auto received_bodies = service.received_response_message_bodies();
  EXPECT_TRUE(received_bodies.empty());
  server->Shutdown();
}

TEST_P(XdsExtProcEnd2endTest,
       ServerToClientMessageErrorOnResponseBodyWhenFailureModeAllowIsFalse) {
  int ext_proc_port = grpc_pick_unused_port_or_die();
  std::string server_address = absl::StrCat("localhost:", ext_proc_port);
  MockRequestHeadersExternalProcessorService service;
  service.SetBehavior(MockRequestHeadersExternalProcessorService::Behavior::
                          kErrorOnResponseBody);
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
          .SetResponseBodyMode(envoy::extensions::filters::http::ext_proc::v3::
                                   ProcessingMode::GRPC)
          .SetResponseTrailerMode(envoy::extensions::filters::http::ext_proc::
                                      v3::ProcessingMode::SEND)
          .SetFailureModeAllow(false)
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
  ClientContext context;
  EchoRequest request;
  request.set_message("original-body");
  EchoResponse response;
  Status status = stub_->Echo(&context, request, &response);
  EXPECT_FALSE(status.ok());
  EXPECT_EQ(status.error_code(), StatusCode::INTERNAL);
  EXPECT_EQ(status.error_message(), "error on response body");
  server->Shutdown();
}

TEST_P(XdsExtProcEnd2endTest,
       ServerToClientMessageErrorOnResponseBodyWhenFailureModeAllowIsTrue) {
  int ext_proc_port = grpc_pick_unused_port_or_die();
  std::string server_address = absl::StrCat("localhost:", ext_proc_port);
  MockRequestHeadersExternalProcessorService service;
  service.SetBehavior(MockRequestHeadersExternalProcessorService::Behavior::
                          kErrorOnResponseBody);
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
          .SetResponseBodyMode(envoy::extensions::filters::http::ext_proc::v3::
                                   ProcessingMode::GRPC)
          .SetResponseTrailerMode(envoy::extensions::filters::http::ext_proc::
                                      v3::ProcessingMode::SEND)
          .SetFailureModeAllow(true)
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
  ClientContext context;
  EchoRequest request;
  request.set_message("original-body");
  EchoResponse response;
  Status status = stub_->Echo(&context, request, &response);
  EXPECT_FALSE(status.ok());
  EXPECT_EQ(status.error_code(), StatusCode::INTERNAL);
  EXPECT_EQ(status.error_message(), "error on response body");
  server->Shutdown();
}

TEST_P(XdsExtProcEnd2endTest, ServerToClientMultipleMessagesSuccess) {
  int ext_proc_port = grpc_pick_unused_port_or_die();
  std::string server_address = absl::StrCat("localhost:", ext_proc_port);
  MockRequestHeadersExternalProcessorService service;
  service.SetBehavior(
      MockRequestHeadersExternalProcessorService::Behavior::kCopyResponseBody);
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
          .SetResponseBodyMode(envoy::extensions::filters::http::ext_proc::v3::
                                   ProcessingMode::GRPC)
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
  ClientContext context;
  auto stream = stub_->BidiStream(&context);
  ASSERT_NE(stream, nullptr);
  for (int i = 0; i < 3; ++i) {
    EchoRequest request;
    request.set_message(absl::StrCat("msg", i));
    ASSERT_TRUE(stream->Write(request));
    EchoResponse response;
    ASSERT_TRUE(stream->Read(&response));
    EXPECT_EQ(response.message(), absl::StrCat("msg", i));
  }
  ASSERT_TRUE(stream->WritesDone());
  EchoResponse response;
  ASSERT_FALSE(stream->Read(&response));
  Status status = stream->Finish();
  EXPECT_TRUE(status.ok()) << status.error_message();
  auto received_bodies = service.received_response_message_bodies();
  ASSERT_EQ(received_bodies.size(), 3);
  for (int i = 0; i < 3; ++i) {
    EchoResponse received_response;
    ASSERT_TRUE(received_response.ParseFromString(received_bodies[i]));
    EXPECT_EQ(received_response.message(), absl::StrCat("msg", i));
  }
  server->Shutdown();
}

TEST_P(XdsExtProcEnd2endTest, ServerToClientProcessorHalfCloseMidCall) {
  int ext_proc_port = grpc_pick_unused_port_or_die();
  std::string server_address = absl::StrCat("localhost:", ext_proc_port);
  MockRequestHeadersExternalProcessorService service;
  service.SetBehavior(MockRequestHeadersExternalProcessorService::Behavior::
                          kHalfCloseResponseBodyMidCall);
  service.SetErrorThreshold(2);  // Half-close on 2nd response (index 1)
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
          .SetResponseBodyMode(envoy::extensions::filters::http::ext_proc::v3::
                                   ProcessingMode::GRPC)
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
  ClientContext context;
  auto stream = stub_->BidiStream(&context);
  ASSERT_NE(stream, nullptr);
  // 1. Write msg0, read resp0 (should succeed)
  EchoRequest request;
  request.set_message("msg0");
  ASSERT_TRUE(stream->Write(request));
  EchoResponse response;
  ASSERT_TRUE(stream->Read(&response));
  EXPECT_EQ(response.message(), "msg0");
  // 2. Write msg1, read resp1 (should succeed, processor sets EOS here)
  request.set_message("msg1");
  ASSERT_TRUE(stream->Write(request));
  ASSERT_TRUE(stream->Read(&response));
  EXPECT_EQ(response.message(), "msg1");
  // 3. Write msg2. This should eventually cause failure because we forced
  // half-close and subsequent writes are rejected by filter with InternalError.
  request.set_message("msg2");
  // Write might return true due to buffering, but stream is failing.
  stream->Write(request);
  stream->WritesDone();
  Status status = stream->Finish();
  EXPECT_FALSE(status.ok());
  EXPECT_EQ(status.error_code(), StatusCode::INTERNAL);
  EXPECT_EQ(status.error_message(),
            "Client sends closed by external processor");
  server->Shutdown();
}

TEST_P(XdsExtProcEnd2endTest,
       ServerToClientProcessorHalfCloseMidCallInObservabilityMode) {
  int ext_proc_port = grpc_pick_unused_port_or_die();
  std::string server_address = absl::StrCat("localhost:", ext_proc_port);
  MockRequestHeadersExternalProcessorService service;
  service.SetBehavior(MockRequestHeadersExternalProcessorService::Behavior::
                          kHalfCloseResponseBodyMidCall);
  service.SetErrorThreshold(2);  // Half-close on 2nd response (index 1)
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
          .SetResponseBodyMode(envoy::extensions::filters::http::ext_proc::v3::
                                   ProcessingMode::GRPC)
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
  ClientContext context;
  auto stream = stub_->BidiStream(&context);
  ASSERT_NE(stream, nullptr);
  // 1. Write msg0, read resp0 (should succeed)
  EchoRequest request;
  request.set_message("msg0");
  ASSERT_TRUE(stream->Write(request));
  EchoResponse response;
  ASSERT_TRUE(stream->Read(&response));
  EXPECT_EQ(response.message(), "msg0");
  // 2. Write msg1, read resp1 (should succeed, processor sets EOS here)
  request.set_message("msg1");
  ASSERT_TRUE(stream->Write(request));
  ASSERT_TRUE(stream->Read(&response));
  EXPECT_EQ(response.message(), "msg1");
  // 3. Write msg2. In observability mode, this should succeed because we bypass
  // processor error/close.
  request.set_message("msg2");
  ASSERT_TRUE(stream->Write(request));
  ASSERT_TRUE(stream->Read(&response));
  EXPECT_EQ(response.message(), "msg2");
  ASSERT_TRUE(stream->WritesDone());
  Status status = stream->Finish();
  EXPECT_TRUE(status.ok()) << status.error_message();
  server->Shutdown();
}

TEST_P(XdsExtProcEnd2endTest, ServerToClientEmptyMessageBodyHandled) {
  int ext_proc_port = grpc_pick_unused_port_or_die();
  std::string server_address = absl::StrCat("localhost:", ext_proc_port);
  MockRequestHeadersExternalProcessorService service;
  service.SetBehavior(MockRequestHeadersExternalProcessorService::Behavior::
                          kMutateResponseBody);
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
          .SetResponseBodyMode(envoy::extensions::filters::http::ext_proc::v3::
                                   ProcessingMode::GRPC)
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
  ClientContext context;
  EchoRequest request;
  request.set_message("");
  EchoResponse response;
  Status status = stub_->Echo(&context, request, &response);
  EXPECT_TRUE(status.ok()) << status.error_message();
  EXPECT_EQ(response.message(), "e2e-mutated-response-body");
  server->Shutdown();
}

TEST_P(XdsExtProcEnd2endTest, ServerToClientProcessorGracefulCloseMidCall) {
  int ext_proc_port = grpc_pick_unused_port_or_die();
  std::string server_address = absl::StrCat("localhost:", ext_proc_port);
  MockRequestHeadersExternalProcessorService service;
  service.SetBehavior(MockRequestHeadersExternalProcessorService::Behavior::
                          kCloseGracefullyMidCall);
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
          .SetResponseBodyMode(envoy::extensions::filters::http::ext_proc::v3::
                                   ProcessingMode::GRPC)
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
  ClientContext context;
  auto stream = stub_->BidiStream(&context);
  ASSERT_NE(stream, nullptr);
  // Send first message. Response should be mutated.
  EchoRequest request;
  request.set_message("msg1");
  ASSERT_TRUE(stream->Write(request));
  EchoResponse response;
  ASSERT_TRUE(stream->Read(&response));
  EXPECT_EQ(response.message(), "e2e-mutated-response-body");
  // Send second message. Response should bypass processor and NOT be mutated.
  request.set_message("msg2");
  ASSERT_TRUE(stream->Write(request));
  ASSERT_TRUE(stream->Read(&response));
  EXPECT_EQ(response.message(), "msg2");
  ASSERT_TRUE(stream->WritesDone());
  Status status = stream->Finish();
  EXPECT_TRUE(status.ok()) << status.error_message();
  auto received_bodies = service.received_response_message_bodies();
  // Processor should have only received the first response because it closed
  // after it.
  ASSERT_EQ(received_bodies.size(), 1);
  EchoResponse received_response;
  ASSERT_TRUE(received_response.ParseFromString(received_bodies[0]));
  EXPECT_EQ(received_response.message(), "msg1");
  server->Shutdown();
}

TEST_P(XdsExtProcEnd2endTest,
       ServerToClientProcessorErrorMidCallWhenFailureModeAllowIsTrue) {
  int ext_proc_port = grpc_pick_unused_port_or_die();
  std::string server_address = absl::StrCat("localhost:", ext_proc_port);
  MockRequestHeadersExternalProcessorService service;
  service.SetBehavior(
      MockRequestHeadersExternalProcessorService::Behavior::kErrorMidCall);
  ServerBuilder builder;
  builder.AddListeningPort(server_address, InsecureServerCredentials());
  builder.RegisterService(&service);
  std::unique_ptr<Server> server = builder.BuildAndStart();
  ASSERT_NE(server, nullptr);
  auto ext_proc =
      ExternalProcessorBuilder()
          .SetTargetUri(absl::StrCat("dns:localhost:", ext_proc_port))
          .SetInsecureChannelCredentials()
          .SetFailureModeAllow(true)
          .SetRequestHeaderMode(envoy::extensions::filters::http::ext_proc::v3::
                                    ProcessingMode::SKIP)
          .SetResponseHeaderMode(envoy::extensions::filters::http::ext_proc::
                                     v3::ProcessingMode::SKIP)
          .SetResponseBodyMode(envoy::extensions::filters::http::ext_proc::v3::
                                   ProcessingMode::GRPC)
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
  ClientContext context;
  auto stream = stub_->BidiStream(&context);
  ASSERT_NE(stream, nullptr);
  // Send first message. Response should be mutated.
  EchoRequest request;
  request.set_message("msg1");
  ASSERT_TRUE(stream->Write(request));
  EchoResponse response;
  ASSERT_TRUE(stream->Read(&response));
  EXPECT_EQ(response.message(), "e2e-mutated-response-body");
  // Send second message. Response should trigger error.
  request.set_message("msg2");
  stream->Write(request);
  // We expect the stream to fail.
  ASSERT_FALSE(stream->Read(&response));
  Status status = stream->Finish();
  EXPECT_FALSE(status.ok());
  EXPECT_EQ(status.error_code(), StatusCode::INTERNAL);
  EXPECT_EQ(status.error_message(), "error mid call");
  server->Shutdown();
}

TEST_P(XdsExtProcEnd2endTest, ClientToServerMultipleMessagesFailure) {
  int ext_proc_port = grpc_pick_unused_port_or_die();
  std::string server_address = absl::StrCat("localhost:", ext_proc_port);
  MockRequestHeadersExternalProcessorService service;
  service.SetBehavior(
      MockRequestHeadersExternalProcessorService::Behavior::kErrorMidCall);
  service.SetErrorThreshold(3);
  ServerBuilder builder;
  builder.AddListeningPort(server_address, InsecureServerCredentials());
  builder.RegisterService(&service);
  std::unique_ptr<Server> server = builder.BuildAndStart();
  ASSERT_NE(server, nullptr);
  auto ext_proc =
      ExternalProcessorBuilder()
          .SetTargetUri(absl::StrCat("dns:localhost:", ext_proc_port))
          .SetInsecureChannelCredentials()
          .SetFailureModeAllow(true)
          .SetRequestHeaderMode(envoy::extensions::filters::http::ext_proc::v3::
                                    ProcessingMode::SKIP)
          .SetResponseHeaderMode(envoy::extensions::filters::http::ext_proc::
                                     v3::ProcessingMode::SKIP)
          .SetRequestBodyMode(envoy::extensions::filters::http::ext_proc::v3::
                                  ProcessingMode::GRPC)
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
  ClientContext context;
  auto stream = stub_->BidiStream(&context);
  ASSERT_NE(stream, nullptr);
  // Send first message. It should be mutated.
  EchoRequest request;
  request.set_message("msg1");
  ASSERT_TRUE(stream->Write(request));
  EchoResponse response;
  ASSERT_TRUE(stream->Read(&response));
  EXPECT_EQ(response.message(), "e2e-mutated-body");
  // Send second message. It should be mutated.
  request.set_message("msg2");
  ASSERT_TRUE(stream->Write(request));
  ASSERT_TRUE(stream->Read(&response));
  EXPECT_EQ(response.message(), "e2e-mutated-body");
  // Send third message. It should trigger error.
  request.set_message("msg3");
  stream->Write(request);
  // We expect the stream to fail.
  ASSERT_FALSE(stream->Read(&response));
  Status status = stream->Finish();
  EXPECT_FALSE(status.ok());
  EXPECT_EQ(status.error_code(), StatusCode::INTERNAL);
  EXPECT_EQ(status.error_message(), "error mid call");
  auto received_bodies = service.received_message_bodies();
  // Processor should have received all 3 messages.
  ASSERT_EQ(received_bodies.size(), 3);
  EchoRequest received_request;
  ASSERT_TRUE(received_request.ParseFromString(received_bodies[0]));
  EXPECT_EQ(received_request.message(), "msg1");
  ASSERT_TRUE(received_request.ParseFromString(received_bodies[1]));
  EXPECT_EQ(received_request.message(), "msg2");
  ASSERT_TRUE(received_request.ParseFromString(received_bodies[2]));
  EXPECT_EQ(received_request.message(), "msg3");
  server->Shutdown();
}

TEST_P(XdsExtProcEnd2endTest, ServerToClientMultipleMessagesFailure) {
  int ext_proc_port = grpc_pick_unused_port_or_die();
  std::string server_address = absl::StrCat("localhost:", ext_proc_port);
  MockRequestHeadersExternalProcessorService service;
  service.SetBehavior(
      MockRequestHeadersExternalProcessorService::Behavior::kErrorMidCall);
  service.SetErrorThreshold(3);
  ServerBuilder builder;
  builder.AddListeningPort(server_address, InsecureServerCredentials());
  builder.RegisterService(&service);
  std::unique_ptr<Server> server = builder.BuildAndStart();
  ASSERT_NE(server, nullptr);
  auto ext_proc =
      ExternalProcessorBuilder()
          .SetTargetUri(absl::StrCat("dns:localhost:", ext_proc_port))
          .SetInsecureChannelCredentials()
          .SetFailureModeAllow(true)
          .SetRequestHeaderMode(envoy::extensions::filters::http::ext_proc::v3::
                                    ProcessingMode::SKIP)
          .SetResponseHeaderMode(envoy::extensions::filters::http::ext_proc::
                                     v3::ProcessingMode::SKIP)
          .SetResponseBodyMode(envoy::extensions::filters::http::ext_proc::v3::
                                   ProcessingMode::GRPC)
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
  ClientContext context;
  auto stream = stub_->BidiStream(&context);
  ASSERT_NE(stream, nullptr);
  // Send first message. Response should be mutated.
  EchoRequest request;
  request.set_message("msg1");
  ASSERT_TRUE(stream->Write(request));
  EchoResponse response;
  ASSERT_TRUE(stream->Read(&response));
  EXPECT_EQ(response.message(), "e2e-mutated-response-body");
  // Send second message. Response should be mutated.
  request.set_message("msg2");
  ASSERT_TRUE(stream->Write(request));
  ASSERT_TRUE(stream->Read(&response));
  EXPECT_EQ(response.message(), "e2e-mutated-response-body");
  // Send third message. Response should trigger error.
  request.set_message("msg3");
  stream->Write(request);
  // We expect the stream to fail.
  ASSERT_FALSE(stream->Read(&response));
  Status status = stream->Finish();
  EXPECT_FALSE(status.ok());
  EXPECT_EQ(status.error_code(), StatusCode::INTERNAL);
  EXPECT_EQ(status.error_message(), "error mid call");
  server->Shutdown();
}

class ExtraResponseExternalProcessorService
    : public envoy::service::ext_proc::v3::ExternalProcessor::Service {
 public:
  grpc::Status Process(
      grpc::ServerContext* /*context*/,
      grpc::ServerReaderWriter<envoy::service::ext_proc::v3::ProcessingResponse,
                               envoy::service::ext_proc::v3::ProcessingRequest>*
          stream) override {
    envoy::service::ext_proc::v3::ProcessingRequest request;
    while (stream->Read(&request)) {
      if (request.has_response_body()) {
        // Send first response (echo back)
        envoy::service::ext_proc::v3::ProcessingResponse response1;
        auto* body_mutation1 = response1.mutable_response_body()
                                   ->mutable_response()
                                   ->mutable_body_mutation();
        auto* streamed_response1 = body_mutation1->mutable_streamed_response();
        streamed_response1->set_body(request.response_body().body());
        streamed_response1->set_end_of_stream(
            request.response_body().end_of_stream());
        stream->Write(response1);

        // Send second (extra) response
        envoy::service::ext_proc::v3::ProcessingResponse response2;
        auto* body_mutation2 = response2.mutable_response_body()
                                   ->mutable_response()
                                   ->mutable_body_mutation();
        auto* streamed_response2 = body_mutation2->mutable_streamed_response();
        streamed_response2->set_body("extra-body");
        streamed_response2->set_end_of_stream(false);
        stream->Write(response2);
      } else {
        // Just continue for other requests (headers etc)
        envoy::service::ext_proc::v3::ProcessingResponse response;
        if (request.has_request_headers()) {
          response.mutable_request_headers()->mutable_response();
        } else if (request.has_response_headers()) {
          response.mutable_response_headers()->mutable_response();
        } else if (request.has_request_body()) {
          response.mutable_request_body()->mutable_response();
        }
        stream->Write(response);
      }
    }
    return grpc::Status::OK;
  }
};

TEST_P(XdsExtProcEnd2endTest, ServerToClientExtraResponseBodyResponseFails) {
  int ext_proc_port = grpc_pick_unused_port_or_die();
  std::string server_address = absl::StrCat("localhost:", ext_proc_port);
  ExtraResponseExternalProcessorService service;
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
          .SetResponseBodyMode(envoy::extensions::filters::http::ext_proc::v3::
                                   ProcessingMode::GRPC)
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
  ClientContext context;
  auto stream = stub_->BidiStream(&context);
  ASSERT_NE(stream, nullptr);
  // Send first message.
  EchoRequest request;
  request.set_message("msg0");
  ASSERT_TRUE(stream->Write(request));
  EchoResponse response;
  // We might read the first response successfully.
  // But the second extra response will trigger internal error in filter.
  // The stream should fail.
  stream->Read(&response);
  Status status = stream->Finish();
  EXPECT_FALSE(status.ok());
  EXPECT_EQ(status.error_code(), StatusCode::INTERNAL);
  EXPECT_EQ(
      status.error_message(),
      "Received unexpected response body response from external processor");
  server->Shutdown();
}

TEST_P(XdsExtProcEnd2endTest, NoProcessingModeBypassesFilter) {
  auto ext_proc =
      ExternalProcessorBuilder()
          .SetTargetUri(
              "dns:localhost:1234")  // Dummy port where nothing is listening
          .SetInsecureChannelCredentials()
          .SetRequestHeaderMode(envoy::extensions::filters::http::ext_proc::v3::
                                    ProcessingMode::SKIP)
          .SetResponseHeaderMode(envoy::extensions::filters::http::ext_proc::
                                     v3::ProcessingMode::SKIP)
          .SetRequestBodyMode(envoy::extensions::filters::http::ext_proc::v3::
                                  ProcessingMode::NONE)
          .SetResponseBodyMode(envoy::extensions::filters::http::ext_proc::v3::
                                   ProcessingMode::NONE)
          .SetResponseTrailerMode(envoy::extensions::filters::http::ext_proc::
                                      v3::ProcessingMode::SKIP)
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
  // Make an RPC. It should succeed because it bypasses the ext proc filter
  // completely and does not try to connect to the dummy port.
  // Note: failure_mode_allow is false by default, so if it did try to connect,
  // the RPC would fail.
  Status status = SendRpc(RpcOptions());
  EXPECT_TRUE(status.ok()) << status.error_message() << " ("
                           << status.error_details() << ")";
}

TEST_P(XdsExtProcEnd2endTest, ObservabilityModeAllProcessingModesEnabled) {
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
          .SetResponseHeaderMode(envoy::extensions::filters::http::ext_proc::
                                     v3::ProcessingMode::SEND)
          .SetRequestBodyMode(envoy::extensions::filters::http::ext_proc::v3::
                                  ProcessingMode::GRPC)
          .SetResponseBodyMode(envoy::extensions::filters::http::ext_proc::v3::
                                   ProcessingMode::GRPC)
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
  ClientContext context;
  EchoRequest request;
  request.set_message("observability-test-request");
  EchoResponse response;
  Status status = stub_->Echo(&context, request, &response);
  EXPECT_TRUE(status.ok()) << status.error_message() << " ("
                           << status.error_details() << ")";
  EXPECT_EQ(response.message(), "observability-test-request");
  // Assert immediately without waiting.
  EXPECT_TRUE(service.received_request_headers());
  EXPECT_TRUE(service.received_response_headers());
  EXPECT_TRUE(service.received_response_trailers());
  ASSERT_EQ(service.received_message_bodies().size(), 1);
  ASSERT_EQ(service.received_response_message_bodies().size(), 1);
  EchoRequest received_req;
  ASSERT_TRUE(
      received_req.ParseFromString(service.received_message_bodies()[0]));
  EXPECT_EQ(received_req.message(), "observability-test-request");
  EchoResponse received_resp;
  ASSERT_TRUE(received_resp.ParseFromString(
      service.received_response_message_bodies()[0]));
  EXPECT_EQ(received_resp.message(), "observability-test-request");
  EXPECT_EQ(backends_[0]->backend_service()->request_count(), 1);
  EXPECT_EQ(backends_[0]->backend_service()->response_count(), 1);
  server->Shutdown();
}

TEST_P(XdsExtProcEnd2endTest, ObservabilityModeBidiStream10Messages) {
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
          .SetResponseHeaderMode(envoy::extensions::filters::http::ext_proc::
                                     v3::ProcessingMode::SEND)
          .SetRequestBodyMode(envoy::extensions::filters::http::ext_proc::v3::
                                  ProcessingMode::GRPC)
          .SetResponseBodyMode(envoy::extensions::filters::http::ext_proc::v3::
                                   ProcessingMode::GRPC)
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
  ClientContext context;
  auto stream = stub_->BidiStream(&context);
  ASSERT_NE(stream, nullptr);
  // Send 10 messages and read 10 responses in ping-pong fashion.
  for (int i = 0; i < 10; ++i) {
    EchoRequest request;
    request.set_message(absl::StrCat("observability-stream-request-", i));
    ASSERT_TRUE(stream->Write(request));
    EchoResponse response;
    ASSERT_TRUE(stream->Read(&response));
    EXPECT_EQ(response.message(),
              absl::StrCat("observability-stream-request-", i));
  }
  ASSERT_TRUE(stream->WritesDone());
  EchoResponse response;
  EXPECT_FALSE(stream->Read(&response));  // Expect EOF
  Status status = stream->Finish();
  EXPECT_TRUE(status.ok()) << status.error_message() << " ("
                           << status.error_details() << ")";
  // Assert immediately without waiting.
  EXPECT_TRUE(service.received_request_headers());
  EXPECT_TRUE(service.received_response_headers());
  EXPECT_TRUE(service.received_response_trailers());
  ASSERT_EQ(service.received_message_bodies().size(), 10);
  ASSERT_EQ(service.received_response_message_bodies().size(), 10);
  EXPECT_TRUE(service.received_end_of_stream_without_message());
  for (int i = 0; i < 10; ++i) {
    EchoRequest received_req;
    ASSERT_TRUE(
        received_req.ParseFromString(service.received_message_bodies()[i]));
    EXPECT_EQ(received_req.message(),
              absl::StrCat("observability-stream-request-", i));
    EchoResponse received_resp;
    ASSERT_TRUE(received_resp.ParseFromString(
        service.received_response_message_bodies()[i]));
    EXPECT_EQ(received_resp.message(),
              absl::StrCat("observability-stream-request-", i));
  }
  server->Shutdown();
}

TEST_P(XdsExtProcEnd2endTest,
       StreamErrorBeforeHeaderCallWhenFailureModeAllowIsTrue) {
  int ext_proc_port = grpc_pick_unused_port_or_die();
  std::string server_address = absl::StrCat("localhost:", ext_proc_port);
  MockRequestHeadersExternalProcessorService service;
  service.SetBehavior(
      MockRequestHeadersExternalProcessorService::Behavior::kImmediateError);
  ServerBuilder builder;
  builder.AddListeningPort(server_address, InsecureServerCredentials());
  builder.RegisterService(&service);
  std::unique_ptr<Server> server = builder.BuildAndStart();
  ASSERT_NE(server, nullptr);
  auto ext_proc =
      ExternalProcessorBuilder()
          .SetTargetUri(absl::StrCat("dns:localhost:", ext_proc_port))
          .SetInsecureChannelCredentials()
          .SetFailureModeAllow(true)
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

  Status status = SendRpc(RpcOptions());
  EXPECT_TRUE(status.ok()) << status.error_message() << " ("
                           << status.error_details() << ")";
  server->Shutdown();
}

TEST_P(XdsExtProcEnd2endTest,
       StreamErrorBeforeHeaderCallWhenFailureModeAllowIsFalse) {
  int ext_proc_port = grpc_pick_unused_port_or_die();
  std::string server_address = absl::StrCat("localhost:", ext_proc_port);
  MockRequestHeadersExternalProcessorService service;
  service.SetBehavior(
      MockRequestHeadersExternalProcessorService::Behavior::kImmediateError);
  ServerBuilder builder;
  builder.AddListeningPort(server_address, InsecureServerCredentials());
  builder.RegisterService(&service);
  std::unique_ptr<Server> server = builder.BuildAndStart();
  ASSERT_NE(server, nullptr);
  auto ext_proc =
      ExternalProcessorBuilder()
          .SetTargetUri(absl::StrCat("dns:localhost:", ext_proc_port))
          .SetInsecureChannelCredentials()
          .SetFailureModeAllow(false)
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

  Status status = SendRpc(RpcOptions());
  // Should fail because the connection succeeded and we sent headers before the
  // server rejected.
  EXPECT_FALSE(status.ok());
  EXPECT_EQ(status.error_code(), StatusCode::INTERNAL);
  EXPECT_EQ(status.error_message(), "immediate error");
  server->Shutdown();
}

TEST_P(XdsExtProcEnd2endTest,
       ConnectionErrorBeforeHeaderCallWhenFailureModeAllowIsFalse) {
  auto ext_proc =
      ExternalProcessorBuilder()
          .SetTargetUri("dns:localhost:1234")  // Dummy port
          .SetInsecureChannelCredentials()
          .SetFailureModeAllow(false)
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

  Status status = SendRpc(RpcOptions());
  // Should succeed because the connection fails before we can send any message.
  EXPECT_TRUE(status.ok()) << status.error_message() << " ("
                           << status.error_details() << ")";
}

TEST_P(XdsExtProcEnd2endTest,
       StreamErrorAfterHeaderResponseBeforeBodyCallWhenFailureModeAllowIsTrue) {
  int ext_proc_port = grpc_pick_unused_port_or_die();
  std::string server_address = absl::StrCat("localhost:", ext_proc_port);
  MockRequestHeadersExternalProcessorService service;
  service.SetBehavior(MockRequestHeadersExternalProcessorService::Behavior::
                          kErrorAfterRequestHeaders);
  ServerBuilder builder;
  builder.AddListeningPort(server_address, InsecureServerCredentials());
  builder.RegisterService(&service);
  std::unique_ptr<Server> server = builder.BuildAndStart();
  ASSERT_NE(server, nullptr);
  auto ext_proc =
      ExternalProcessorBuilder()
          .SetTargetUri(absl::StrCat("dns:localhost:", ext_proc_port))
          .SetInsecureChannelCredentials()
          .SetFailureModeAllow(true)
          .SetRequestHeaderMode(envoy::extensions::filters::http::ext_proc::v3::
                                    ProcessingMode::SEND)
          .SetRequestBodyMode(envoy::extensions::filters::http::ext_proc::v3::
                                  ProcessingMode::GRPC)
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

  Status status = SendRpc(RpcOptions());
  EXPECT_TRUE(status.ok()) << status.error_message() << " ("
                           << status.error_details() << ")";
  server->Shutdown();
}

TEST_P(
    XdsExtProcEnd2endTest,
    StreamErrorAfterHeaderResponseBeforeBodyCallWhenFailureModeAllowIsFalse) {
  int ext_proc_port = grpc_pick_unused_port_or_die();
  std::string server_address = absl::StrCat("localhost:", ext_proc_port);
  MockRequestHeadersExternalProcessorService service;
  service.SetBehavior(MockRequestHeadersExternalProcessorService::Behavior::
                          kErrorAfterRequestHeaders);
  ServerBuilder builder;
  builder.AddListeningPort(server_address, InsecureServerCredentials());
  builder.RegisterService(&service);
  std::unique_ptr<Server> server = builder.BuildAndStart();
  ASSERT_NE(server, nullptr);
  auto ext_proc =
      ExternalProcessorBuilder()
          .SetTargetUri(absl::StrCat("dns:localhost:", ext_proc_port))
          .SetInsecureChannelCredentials()
          .SetFailureModeAllow(false)
          .SetRequestHeaderMode(envoy::extensions::filters::http::ext_proc::v3::
                                    ProcessingMode::SEND)
          .SetRequestBodyMode(envoy::extensions::filters::http::ext_proc::v3::
                                  ProcessingMode::GRPC)
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

  Status status = SendRpc(RpcOptions());
  EXPECT_FALSE(status.ok());
  EXPECT_EQ(status.error_code(), StatusCode::INTERNAL);
  EXPECT_EQ(status.error_message(), "error after request headers");
  server->Shutdown();
}

TEST_P(
    XdsExtProcEnd2endTest,
    StreamErrorAfterRequestHeaderResponseBeforeResponseHeaderCallWhenFailureModeAllowIsTrue) {
  int ext_proc_port = grpc_pick_unused_port_or_die();
  std::string server_address = absl::StrCat("localhost:", ext_proc_port);
  MockRequestHeadersExternalProcessorService service;
  service.SetBehavior(MockRequestHeadersExternalProcessorService::Behavior::
                          kErrorAfterRequestHeaders);
  ServerBuilder builder;
  builder.AddListeningPort(server_address, InsecureServerCredentials());
  builder.RegisterService(&service);
  std::unique_ptr<Server> server = builder.BuildAndStart();
  ASSERT_NE(server, nullptr);
  auto ext_proc =
      ExternalProcessorBuilder()
          .SetTargetUri(absl::StrCat("dns:localhost:", ext_proc_port))
          .SetInsecureChannelCredentials()
          .SetFailureModeAllow(true)
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

  Status status = SendRpc(RpcOptions());
  EXPECT_TRUE(status.ok()) << status.error_message() << " ("
                           << status.error_details() << ")";
  server->Shutdown();
}

TEST_P(
    XdsExtProcEnd2endTest,
    StreamErrorAfterRequestHeaderResponseBeforeResponseHeaderCallWhenFailureModeAllowIsFalse) {
  int ext_proc_port = grpc_pick_unused_port_or_die();
  std::string server_address = absl::StrCat("localhost:", ext_proc_port);
  MockRequestHeadersExternalProcessorService service;
  service.SetBehavior(MockRequestHeadersExternalProcessorService::Behavior::
                          kErrorAfterRequestHeaders);
  ServerBuilder builder;
  builder.AddListeningPort(server_address, InsecureServerCredentials());
  builder.RegisterService(&service);
  std::unique_ptr<Server> server = builder.BuildAndStart();
  ASSERT_NE(server, nullptr);
  auto ext_proc =
      ExternalProcessorBuilder()
          .SetTargetUri(absl::StrCat("dns:localhost:", ext_proc_port))
          .SetInsecureChannelCredentials()
          .SetFailureModeAllow(false)
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

  Status status = SendRpc(RpcOptions());
  EXPECT_FALSE(status.ok());
  EXPECT_EQ(status.error_code(), StatusCode::INTERNAL);
  EXPECT_EQ(status.error_message(), "error after request headers");
  server->Shutdown();
}

TEST_P(
    XdsExtProcEnd2endTest,
    StreamErrorAfterResponseHeaderResponseBeforeResponseBodyCallWhenFailureModeAllowIsTrue) {
  int ext_proc_port = grpc_pick_unused_port_or_die();
  std::string server_address = absl::StrCat("localhost:", ext_proc_port);
  MockRequestHeadersExternalProcessorService service;
  service.SetBehavior(MockRequestHeadersExternalProcessorService::Behavior::
                          kErrorAfterResponseHeaders);
  ServerBuilder builder;
  builder.AddListeningPort(server_address, InsecureServerCredentials());
  builder.RegisterService(&service);
  std::unique_ptr<Server> server = builder.BuildAndStart();
  ASSERT_NE(server, nullptr);
  auto ext_proc =
      ExternalProcessorBuilder()
          .SetTargetUri(absl::StrCat("dns:localhost:", ext_proc_port))
          .SetInsecureChannelCredentials()
          .SetFailureModeAllow(true)
          .SetRequestHeaderMode(envoy::extensions::filters::http::ext_proc::v3::
                                    ProcessingMode::SKIP)
          .SetResponseHeaderMode(envoy::extensions::filters::http::ext_proc::
                                     v3::ProcessingMode::SEND)
          .SetResponseBodyMode(envoy::extensions::filters::http::ext_proc::v3::
                                   ProcessingMode::GRPC)
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

  Status status = SendRpc(RpcOptions());
  EXPECT_TRUE(status.ok()) << status.error_message() << " ("
                           << status.error_details() << ")";
  server->Shutdown();
}

TEST_P(
    XdsExtProcEnd2endTest,
    StreamErrorAfterResponseHeaderResponseBeforeResponseBodyCallWhenFailureModeAllowIsFalse) {
  int ext_proc_port = grpc_pick_unused_port_or_die();
  std::string server_address = absl::StrCat("localhost:", ext_proc_port);
  MockRequestHeadersExternalProcessorService service;
  service.SetBehavior(MockRequestHeadersExternalProcessorService::Behavior::
                          kErrorAfterResponseHeaders);
  ServerBuilder builder;
  builder.AddListeningPort(server_address, InsecureServerCredentials());
  builder.RegisterService(&service);
  std::unique_ptr<Server> server = builder.BuildAndStart();
  ASSERT_NE(server, nullptr);
  auto ext_proc =
      ExternalProcessorBuilder()
          .SetTargetUri(absl::StrCat("dns:localhost:", ext_proc_port))
          .SetInsecureChannelCredentials()
          .SetFailureModeAllow(false)
          .SetRequestHeaderMode(envoy::extensions::filters::http::ext_proc::v3::
                                    ProcessingMode::SKIP)
          .SetResponseHeaderMode(envoy::extensions::filters::http::ext_proc::
                                     v3::ProcessingMode::SEND)
          .SetResponseBodyMode(envoy::extensions::filters::http::ext_proc::v3::
                                   ProcessingMode::GRPC)
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

  Status status = SendRpc(RpcOptions());
  EXPECT_FALSE(status.ok());
  EXPECT_EQ(status.error_code(), StatusCode::INTERNAL);
  EXPECT_EQ(status.error_message(), "error after response headers");
  server->Shutdown();
}

TEST_P(XdsExtProcEnd2endTest, EventOrderingTrailersOnly) {
  int ext_proc_port = grpc_pick_unused_port_or_die();
  std::string server_address = absl::StrCat("localhost:", ext_proc_port);
  MockRequestHeadersExternalProcessorService service;
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
          .SetResponseBodyMode(envoy::extensions::filters::http::ext_proc::v3::
                                   ProcessingMode::GRPC)
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

  ClientContext context;
  EchoRequest request;
  request.set_message("hello");
  // Configure backend to return error immediately (Trailers-Only)
  request.mutable_param()->mutable_expected_error()->set_code(
      static_cast<int>(StatusCode::UNAVAILABLE));
  request.mutable_param()->mutable_expected_error()->set_error_message(
      "backend error");
  EchoResponse response;
  Status status = stub_->Echo(&context, request, &response);
  EXPECT_FALSE(status.ok());
  EXPECT_EQ(status.error_code(), StatusCode::UNAVAILABLE);
  EXPECT_EQ(status.error_message(), "backend error");

  // Verify ext_proc received response_headers with EOS = true
  EXPECT_TRUE(service.received_response_headers());
  EXPECT_TRUE(service.response_headers_had_eos());

  // Verify ext_proc did NOT receive response_trailers or response_body
  EXPECT_FALSE(service.received_response_trailers());
  EXPECT_TRUE(service.received_response_message_bodies().empty());

  server->Shutdown();
}

TEST_P(XdsExtProcEnd2endTest,
       EventOrderingProtocolConfigOnFirstMessageServerHeaders) {
  int ext_proc_port = grpc_pick_unused_port_or_die();
  std::string server_address = absl::StrCat("localhost:", ext_proc_port);
  MockRequestHeadersExternalProcessorService service;
  ServerBuilder builder;
  builder.AddListeningPort(server_address, InsecureServerCredentials());
  builder.RegisterService(&service);
  std::unique_ptr<Server> server = builder.BuildAndStart();
  ASSERT_NE(server, nullptr);
  // Bypass request path completely, enable only response headers
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

  ClientContext context;
  EchoRequest request;
  request.set_message("hello");
  EchoResponse response;
  Status status = stub_->Echo(&context, request, &response);
  EXPECT_TRUE(status.ok()) << status.error_message();

  // Verify ext_proc received response_headers as first message and it had
  // config
  EXPECT_TRUE(service.received_response_headers());
  EXPECT_TRUE(service.has_protocol_config_in_response_headers());
  EXPECT_FALSE(service.received_request_headers());

  server->Shutdown();
}

TEST_P(XdsExtProcEnd2endTest,
       EventOrderingProtocolConfigOnFirstMessageClientBody) {
  int ext_proc_port = grpc_pick_unused_port_or_die();
  std::string server_address = absl::StrCat("localhost:", ext_proc_port);
  MockRequestHeadersExternalProcessorService service;
  ServerBuilder builder;
  builder.AddListeningPort(server_address, InsecureServerCredentials());
  builder.RegisterService(&service);
  std::unique_ptr<Server> server = builder.BuildAndStart();
  ASSERT_NE(server, nullptr);
  // Bypass request headers, enable request body. First message will be request
  // body.
  auto ext_proc =
      ExternalProcessorBuilder()
          .SetTargetUri(absl::StrCat("dns:localhost:", ext_proc_port))
          .SetInsecureChannelCredentials()
          .SetRequestHeaderMode(envoy::extensions::filters::http::ext_proc::v3::
                                    ProcessingMode::SKIP)
          .SetRequestBodyMode(envoy::extensions::filters::http::ext_proc::v3::
                                  ProcessingMode::GRPC)
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

  ClientContext context;
  EchoRequest request;
  request.set_message("hello");
  EchoResponse response;
  Status status = stub_->Echo(&context, request, &response);
  EXPECT_TRUE(status.ok()) << status.error_message();

  // Verify ext_proc received request_body as first message and it had config
  EXPECT_FALSE(service.received_request_headers());
  EXPECT_FALSE(service.received_message_bodies().empty());
  EXPECT_TRUE(service.has_protocol_config_in_request_body());

  server->Shutdown();
}

TEST_P(XdsExtProcEnd2endTest, ProtocolViolationRequestBodyBeforeHeaders) {
  int ext_proc_port = grpc_pick_unused_port_or_die();
  std::string server_address = absl::StrCat("localhost:", ext_proc_port);
  MockRequestHeadersExternalProcessorService service;
  service.SetBehavior(MockRequestHeadersExternalProcessorService::Behavior::
                          kSendRequestBodyWithoutHeaders);
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
          .SetRequestBodyMode(envoy::extensions::filters::http::ext_proc::v3::
                                  ProcessingMode::GRPC)
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

  ClientContext context;
  EchoRequest request;
  request.set_message("hello");
  EchoResponse response;
  Status status = stub_->Echo(&context, request, &response);
  EXPECT_FALSE(status.ok());
  EXPECT_EQ(status.error_code(), StatusCode::INTERNAL);
  EXPECT_NE(
      status.error_message().find(
          "Received request body response before request headers response"),
      std::string::npos)
      << "Actual error: " << status.error_message();

  server->Shutdown();
}

TEST_P(XdsExtProcEnd2endTest, ProtocolViolationResponseBodyBeforeHeaders) {
  int ext_proc_port = grpc_pick_unused_port_or_die();
  std::string server_address = absl::StrCat("localhost:", ext_proc_port);
  MockRequestHeadersExternalProcessorService service;
  service.SetBehavior(MockRequestHeadersExternalProcessorService::Behavior::
                          kSendResponseBodyWithoutHeaders);
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
          .SetResponseBodyMode(envoy::extensions::filters::http::ext_proc::v3::
                                   ProcessingMode::GRPC)
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

  ClientContext context;
  EchoRequest request;
  request.set_message("hello");
  EchoResponse response;
  Status status = stub_->Echo(&context, request, &response);
  EXPECT_FALSE(status.ok());
  EXPECT_EQ(status.error_code(), StatusCode::INTERNAL);
  EXPECT_NE(
      status.error_message().find(
          "Received response body response before response headers response"),
      std::string::npos)
      << "Actual error: " << status.error_message();

  server->Shutdown();
}

TEST_P(XdsExtProcEnd2endTest, ProtocolViolationResponseTrailersInTrailersOnly) {
  int ext_proc_port = grpc_pick_unused_port_or_die();
  std::string server_address = absl::StrCat("localhost:", ext_proc_port);
  MockRequestHeadersExternalProcessorService service;
  service.SetBehavior(MockRequestHeadersExternalProcessorService::Behavior::
                          kSendResponseTrailersInTrailersOnly);
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

  ClientContext context;
  EchoRequest request;
  request.set_message("hello");
  request.mutable_param()->mutable_expected_error()->set_code(
      static_cast<int>(StatusCode::UNAVAILABLE));
  request.mutable_param()->mutable_expected_error()->set_error_message(
      "backend error");
  EchoResponse response;
  Status status = stub_->Echo(&context, request, &response);
  EXPECT_FALSE(status.ok());
  EXPECT_EQ(status.error_code(), StatusCode::INTERNAL);
  EXPECT_NE(status.error_message().find(
                "Received response trailers response in a Trailers-Only call"),
            std::string::npos)
      << "Actual error: " << status.error_message();

  server->Shutdown();
}

TEST_P(XdsExtProcEnd2endTest, ProtocolViolationUnexpectedRequestHeaders) {
  int ext_proc_port = grpc_pick_unused_port_or_die();
  std::string server_address = absl::StrCat("localhost:", ext_proc_port);
  MockRequestHeadersExternalProcessorService service;
  service.SetBehavior(MockRequestHeadersExternalProcessorService::Behavior::
                          kSendUnexpectedRequestHeaders);
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

  ClientContext context;
  EchoRequest request;
  request.set_message("hello");
  EchoResponse response;
  Status status = stub_->Echo(&context, request, &response);
  EXPECT_FALSE(status.ok());
  EXPECT_EQ(status.error_code(), StatusCode::INTERNAL);
  EXPECT_NE(
      status.error_message().find(
          "Received request headers response but request headers are disabled"),
      std::string::npos)
      << "Actual error: " << status.error_message();

  server->Shutdown();
}

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
