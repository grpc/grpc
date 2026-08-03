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
#include <string>
#include <vector>

#include "envoy/config/cluster/v3/cluster.pb.h"
#include "envoy/config/common/mutation_rules/v3/mutation_rules.pb.h"
#include "envoy/extensions/filters/http/ext_proc/v3/ext_proc.pb.h"
#include "envoy/extensions/filters/network/http_connection_manager/v3/http_connection_manager.pb.h"
#include "envoy/extensions/grpc_service/call_credentials/access_token/v3/access_token_credentials.pb.h"
#include "envoy/extensions/grpc_service/channel_credentials/google_default/v3/google_default_credentials.pb.h"
#include "envoy/extensions/grpc_service/channel_credentials/insecure/v3/insecure_credentials.pb.h"
#include "envoy/service/ext_proc/v3/external_processor.grpc.pb.h"
#include "src/core/config/config_vars.h"
#include "src/core/lib/experiments/config.h"
#include "src/core/util/env.h"
#include "test/core/test_util/fake_stats_plugin.h"
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

MATCHER_P2(GrpcStatusIs, code, message_matcher, "") {
  return ::testing::ExplainMatchResult(code, arg.error_code(),
                                       result_listener) &&
         ::testing::ExplainMatchResult(message_matcher, arg.error_message(),
                                       result_listener);
}

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
  ExternalProcessorBuilder() {
    auto* processing_mode = ext_proc_.mutable_processing_mode();
    processing_mode->set_request_header_mode(
        envoy::extensions::filters::http::ext_proc::v3::ProcessingMode::SKIP);
    processing_mode->set_response_header_mode(
        envoy::extensions::filters::http::ext_proc::v3::ProcessingMode::SKIP);
    processing_mode->set_response_trailer_mode(
        envoy::extensions::filters::http::ext_proc::v3::ProcessingMode::SKIP);
    auto* timeout = ext_proc_.mutable_grpc_service()->mutable_timeout();
    timeout->set_seconds(1);  // 1s
    timeout->set_nanos(0);
  }

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

  ExternalProcessorBuilder& SetGrpcServiceTimeout(
      const google::protobuf::Duration& timeout) {
    *ext_proc_.mutable_grpc_service()->mutable_timeout() = timeout;
    return *this;
  }

  envoy::extensions::filters::http::ext_proc::v3::ExternalProcessor Build() {
    return ext_proc_;
  }

 private:
  envoy::extensions::filters::http::ext_proc::v3::ExternalProcessor ext_proc_;
};

constexpr absl::string_view kFilterInstanceName = "ext_proc_instance";

constexpr char kRequestHeadersMutatedHeaderKey[] =
    "x-extproc-request-headers-mutated";
constexpr char kResponseHeadersMutatedHeaderKey[] =
    "x-extproc-response-headers-mutated";
constexpr char kResponseTrailersMutatedHeaderKey[] =
    "x-extproc-response-trailers-mutated";
constexpr char kHeaderMutatedValue[] = "yes";
constexpr char kRequestBodyMutatedSuffix[] = "-request-body-mutated";
constexpr char kResponseBodyMutatedSuffix[] = "-response-body-mutated";
constexpr char kImmediateResponseHeaderKey[] =
    "x-extproc-immediate-response-added";
constexpr char kMessage1[] = "message1";
constexpr char kMessage2[] = "message2";
constexpr char kMessage1Mutated[] = "message1-mutated";
constexpr char kMutatedSuffix[] = "-mutated";

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
          header->mutable_header()->set_key(kRequestHeadersMutatedHeaderKey);
          header->mutable_header()->set_value(kHeaderMutatedValue);
        } else if (request.has_response_headers()) {
          counts_.response_headers++;
          auto* mutation = response.mutable_response_headers()
                               ->mutable_response()
                               ->mutable_header_mutation();
          auto* header = mutation->add_set_headers();
          header->mutable_header()->set_key(kResponseHeadersMutatedHeaderKey);
          header->mutable_header()->set_value(kHeaderMutatedValue);
        } else if (request.has_request_body()) {
          counts_.request_body++;
          auto* body_mutation = response.mutable_request_body()
                                    ->mutable_response()
                                    ->mutable_body_mutation();
          grpc::testing::EchoRequest echo_request;
          if (echo_request.ParseFromString(request.request_body().body())) {
            echo_request.set_message(absl::StrCat(echo_request.message(),
                                                  kRequestBodyMutatedSuffix));
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
                                                   kResponseBodyMutatedSuffix));
            std::string mutated_body;
            GRPC_CHECK(echo_response.SerializeToString(&mutated_body));
            body_mutation->mutable_streamed_response()->set_body(mutated_body);
          } else {
            body_mutation->mutable_streamed_response()->set_body(
                request.response_body().body());
          }
        } else if (request.has_response_trailers()) {
          counts_.response_trailers++;
          auto* mutation =
              response.mutable_response_trailers()->mutable_header_mutation();
          auto* header = mutation->add_set_headers();
          header->mutable_header()->set_key(kResponseTrailersMutatedHeaderKey);
          header->mutable_header()->set_value(kHeaderMutatedValue);
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
    auto* body_mutation = response->mutable_request_body()
                              ->mutable_response()
                              ->mutable_body_mutation();
    body_mutation->mutable_streamed_response()->set_body(
        request.request_body().body());
    body_mutation->mutable_streamed_response()->set_end_of_stream(
        request.request_body().end_of_stream());
  } else if (request.has_response_body()) {
    auto* body_mutation = response->mutable_response_body()
                              ->mutable_response()
                              ->mutable_body_mutation();
    body_mutation->mutable_streamed_response()->set_body(
        request.response_body().body());
    body_mutation->mutable_streamed_response()->set_end_of_stream(
        request.response_body().end_of_stream());
  } else if (request.has_request_trailers()) {
    response->mutable_request_trailers()->mutable_header_mutation();
  } else if (request.has_response_trailers()) {
    response->mutable_response_trailers()->mutable_header_mutation();
  }
}

class GenericMockService : public MockExternalProcessorBase {
 public:
  using Callback = std::function<grpc::Status(
      const ::envoy::service::ext_proc::v3::ProcessingRequest&,
      ::envoy::service::ext_proc::v3::ProcessingResponse*)>;

  using StreamCallback = std::function<grpc::Status(
      grpc::ServerReaderWriter<
          ::envoy::service::ext_proc::v3::ProcessingResponse,
          ::envoy::service::ext_proc::v3::ProcessingRequest>* stream)>;

  explicit GenericMockService(Callback callback)
      : callback_(std::move(callback)) {}

  explicit GenericMockService(StreamCallback stream_callback)
      : stream_callback_(std::move(stream_callback)) {}

  grpc::Status Process(
      grpc::ServerContext* /*context*/,
      grpc::ServerReaderWriter<
          ::envoy::service::ext_proc::v3::ProcessingResponse,
          ::envoy::service::ext_proc::v3::ProcessingRequest>* stream) override {
    if (stream_callback_) {
      return stream_callback_(stream);
    }
    ::envoy::service::ext_proc::v3::ProcessingRequest request;
    while (stream->Read(&request)) {
      ::envoy::service::ext_proc::v3::ProcessingResponse response;
      grpc::Status status = callback_(request, &response);
      if (!status.ok()) {
        return status;
      }
      stream->Write(response);
    }
    return grpc::Status::OK;
  }

 private:
  Callback callback_;
  StreamCallback stream_callback_;
};

class CustomBidiStreamServiceImpl : public TestServiceImpl {
 public:
  Status BidiStream(
      ServerContext* /*context*/,
      ServerReaderWriter<EchoResponse, EchoRequest>* stream) override {
    EchoRequest request1;
    if (!stream->Read(&request1)) return Status::OK;
    stream->SendInitialMetadata();
    EchoRequest request2;
    if (!stream->Read(&request2)) return Status::OK;
    EchoResponse response;
    response.set_message(request1.message());
    stream->Write(response);
    return Status::OK;
  }
};

class XdsExtProcEnd2endTest : public XdsEnd2endTest {
 public:
  class ExtProcServerBase {
   public:
    virtual ~ExtProcServerBase() = default;
    virtual void Start() = 0;
    virtual void Shutdown() = 0;
    virtual std::string target() const = 0;
    virtual int port() const = 0;
  };

  template <typename ServiceType>
  class ExtProcServer : public ExtProcServerBase {
   public:
    ExtProcServer(std::unique_ptr<ServiceType> service)
        : service_(std::move(service)), port_(grpc_pick_unused_port_or_die()) {}

    void Start() override {
      LOG(INFO) << "starting ExtProc server on port " << port_;
      std::string server_address = absl::StrCat("localhost:", port_);
      ServerBuilder builder;
      builder.AddListeningPort(server_address,
                               grpc::InsecureServerCredentials());
      builder.RegisterService(service_.get());
      server_ = builder.BuildAndStart();
      GRPC_CHECK(server_ != nullptr)
          << "Failed to start ExtProcServer on " << server_address;
      LOG(INFO) << "ExtProc server startup complete";
    }

    void Shutdown() override {
      if (server_) {
        server_->Shutdown(grpc_timeout_milliseconds_to_deadline(0));
      }
    }

    std::string target() const override {
      return absl::StrCat("localhost:", port_);
    }
    int port() const override { return port_; }
    ServiceType* ext_proc_service() { return service_.get(); }

   private:
    std::unique_ptr<ServiceType> service_;
    int port_;
    std::unique_ptr<Server> server_;
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
    InitClient(MakeBootstrapBuilder().SetTrustedXdsServer(),
               /*lb_expected_authority=*/"",
               /*xds_resource_does_not_exist_timeout_ms=*/0,
               /*balancer_authority_override=*/"", /*args=*/nullptr);
    ext_proc_server_ =
        std::make_unique<ExtProcServer<MockExternalProcessorService>>(
            std::make_unique<MockExternalProcessorService>());
    ext_proc_server_->Start();
  }

  void TearDown() override {
    if (alternative_ext_proc_server_ != nullptr) {
      alternative_ext_proc_server_->Shutdown();
    }
    ext_proc_server_->Shutdown();
    XdsEnd2endTest::TearDown();
  }

  template <typename ServiceType>
  void StartAlternativeServer(std::unique_ptr<ServiceType> service) {
    ext_proc_server_->Shutdown();
    alternative_ext_proc_server_ =
        std::make_unique<ExtProcServer<ServiceType>>(std::move(service));
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

  std::unique_ptr<ExtProcServer<MockExternalProcessorService>> ext_proc_server_;
  std::unique_ptr<ExtProcServerBase> alternative_ext_proc_server_;
};

INSTANTIATE_TEST_SUITE_P(XdsTest, XdsExtProcEnd2endTest,
                         ::testing::Values(XdsTestType()), &XdsTestType::Name);

//
// Core Processing Mode tests
//

TEST_P(XdsExtProcEnd2endTest, ProcessingModeAllDisabledSuccess) {
  CreateAndStartBackends(1);
  auto ext_proc_config =
      ExternalProcessorBuilder()
          .SetTargetUri(ext_proc_server_->target())
          .SetInsecureChannelCredentials()
          .SetObservabilityMode(false)
          .SetRequestHeaderMode(envoy::extensions::filters::http::ext_proc::v3::
                                    ProcessingMode::SKIP)
          .SetResponseHeaderMode(envoy::extensions::filters::http::ext_proc::
                                     v3::ProcessingMode::SKIP)
          .SetResponseTrailerMode(envoy::extensions::filters::http::ext_proc::
                                      v3::ProcessingMode::SKIP)
          .SetRequestBodyMode(envoy::extensions::filters::http::ext_proc::v3::
                                  ProcessingMode::NONE)
          .SetResponseBodyMode(envoy::extensions::filters::http::ext_proc::v3::
                                   ProcessingMode::NONE)
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
  EchoResponse response;
  std::multimap<std::string, std::string> server_initial_metadata;
  std::multimap<std::string, std::string> server_trailing_metadata;
  Status status =
      SendRpcGetTrailers(rpc_options, &response, &server_initial_metadata,
                         &server_trailing_metadata);
  EXPECT_TRUE(status.ok()) << "RPC failed: " << status.error_message();
  // Wait for expected counts (all 0)
  MockExternalProcessorService::RequestCounts expected_counts;
  expected_counts.request_headers = 0;
  expected_counts.response_headers = 0;
  expected_counts.response_trailers = 0;
  expected_counts.request_body = 0;
  expected_counts.response_body = 0;
  ext_proc_server_->ext_proc_service()->WaitForRequestCounts(expected_counts);
  auto counts = ext_proc_server_->ext_proc_service()->GetRequestCounts();
  EXPECT_EQ(counts.request_headers, 0);
  EXPECT_EQ(counts.response_headers, 0);
  EXPECT_EQ(counts.response_trailers, 0);
  EXPECT_EQ(counts.request_body, 0);
  EXPECT_EQ(counts.response_body, 0);
  // Verify mutations (none expected)
  auto it = server_initial_metadata.find(kRequestHeadersMutatedHeaderKey);
  EXPECT_EQ(it, server_initial_metadata.end());
  it = server_initial_metadata.find(kResponseHeadersMutatedHeaderKey);
  EXPECT_EQ(it, server_initial_metadata.end());
  it = server_trailing_metadata.find(kResponseTrailersMutatedHeaderKey);
  EXPECT_EQ(it, server_trailing_metadata.end());
  EXPECT_EQ(response.message(), kRequestMessage);
  EXPECT_EQ(ext_proc_server_->ext_proc_service()->num_calls(), 0);
}

TEST_P(XdsExtProcEnd2endTest, ProcessingModeAllEnabledSuccess) {
  CreateAndStartBackends(1);
  auto ext_proc_config =
      ExternalProcessorBuilder()
          .SetTargetUri(ext_proc_server_->target())
          .SetInsecureChannelCredentials()
          .SetObservabilityMode(false)
          .SetRequestHeaderMode(envoy::extensions::filters::http::ext_proc::v3::
                                    ProcessingMode::SEND)
          .SetResponseHeaderMode(envoy::extensions::filters::http::ext_proc::
                                     v3::ProcessingMode::SEND)
          .SetResponseTrailerMode(envoy::extensions::filters::http::ext_proc::
                                      v3::ProcessingMode::SEND)
          .SetRequestBodyMode(envoy::extensions::filters::http::ext_proc::v3::
                                  ProcessingMode::GRPC)
          .SetResponseBodyMode(envoy::extensions::filters::http::ext_proc::v3::
                                   ProcessingMode::GRPC)
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
  EchoResponse response;
  std::multimap<std::string, std::string> server_initial_metadata;
  std::multimap<std::string, std::string> server_trailing_metadata;
  Status status =
      SendRpcGetTrailers(rpc_options, &response, &server_initial_metadata,
                         &server_trailing_metadata);
  EXPECT_TRUE(status.ok()) << "RPC failed: " << status.error_message();
  // Wait for expected counts
  MockExternalProcessorService::RequestCounts expected_counts;
  expected_counts.request_headers = 1;
  expected_counts.response_headers = 1;
  expected_counts.response_trailers = 1;
  expected_counts.request_body = 1;
  expected_counts.response_body = 1;
  ext_proc_server_->ext_proc_service()->WaitForRequestCounts(expected_counts);
  auto counts = ext_proc_server_->ext_proc_service()->GetRequestCounts();
  EXPECT_EQ(counts.request_headers, 1);
  EXPECT_EQ(counts.response_headers, 1);
  EXPECT_EQ(counts.response_trailers, 1);
  EXPECT_THAT(counts.request_body, ::testing::AnyOf(1, 2));
  EXPECT_EQ(counts.response_body, 1);
  // Verify mutations
  auto it = server_initial_metadata.find(kRequestHeadersMutatedHeaderKey);
  ASSERT_NE(it, server_initial_metadata.end());
  EXPECT_EQ(it->second, kHeaderMutatedValue);
  it = server_initial_metadata.find(kResponseHeadersMutatedHeaderKey);
  ASSERT_NE(it, server_initial_metadata.end());
  EXPECT_EQ(it->second, kHeaderMutatedValue);
  it = server_trailing_metadata.find(kResponseTrailersMutatedHeaderKey);
  ASSERT_NE(it, server_trailing_metadata.end());
  EXPECT_EQ(it->second, kHeaderMutatedValue);
  std::string expected_message = kRequestMessage;
  absl::StrAppend(&expected_message, kRequestBodyMutatedSuffix);
  absl::StrAppend(&expected_message, kResponseBodyMutatedSuffix);
  EXPECT_EQ(response.message(), expected_message);
  EXPECT_EQ(ext_proc_server_->ext_proc_service()->num_calls(), 1);
}

TEST_P(XdsExtProcEnd2endTest,
       ProcessingModeAllEnabledWithObservabilityModeSuccess) {
  CreateAndStartBackends(1);
  auto ext_proc_config_builder = ExternalProcessorBuilder()
                                     .SetTargetUri(ext_proc_server_->target())
                                     .SetInsecureChannelCredentials()
                                     .SetObservabilityMode(true);
  google::protobuf::Duration timeout;
  timeout.set_seconds(1);
  timeout.set_nanos(0);
  ext_proc_config_builder.SetDeferredCloseTimeout(timeout);
  ext_proc_config_builder.SetRequestHeaderMode(
      envoy::extensions::filters::http::ext_proc::v3::ProcessingMode::SEND);
  ext_proc_config_builder.SetResponseHeaderMode(
      envoy::extensions::filters::http::ext_proc::v3::ProcessingMode::SEND);
  ext_proc_config_builder.SetResponseTrailerMode(
      envoy::extensions::filters::http::ext_proc::v3::ProcessingMode::SEND);
  ext_proc_config_builder.SetRequestBodyMode(
      envoy::extensions::filters::http::ext_proc::v3::ProcessingMode::GRPC);
  ext_proc_config_builder.SetResponseBodyMode(
      envoy::extensions::filters::http::ext_proc::v3::ProcessingMode::GRPC);
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
  // Wait for expected counts
  MockExternalProcessorService::RequestCounts expected_counts;
  expected_counts.request_headers = 1;
  expected_counts.response_headers = 1;
  expected_counts.response_trailers = 1;
  expected_counts.request_body = 1;
  expected_counts.response_body = 1;
  ext_proc_server_->ext_proc_service()->WaitForRequestCounts(expected_counts);
  auto counts = ext_proc_server_->ext_proc_service()->GetRequestCounts();
  EXPECT_EQ(counts.request_headers, 1);
  EXPECT_EQ(counts.response_headers, 1);
  EXPECT_EQ(counts.response_trailers, 1);
  EXPECT_THAT(counts.request_body, ::testing::AnyOf(1, 2));
  EXPECT_EQ(counts.response_body, 1);
  // Verify mutations (none expected in observability mode)
  auto it = server_initial_metadata.find(kRequestHeadersMutatedHeaderKey);
  EXPECT_EQ(it, server_initial_metadata.end());
  it = server_initial_metadata.find(kResponseHeadersMutatedHeaderKey);
  EXPECT_EQ(it, server_initial_metadata.end());
  it = server_trailing_metadata.find(kResponseTrailersMutatedHeaderKey);
  EXPECT_EQ(it, server_trailing_metadata.end());
  EXPECT_EQ(response.message(), kRequestMessage);
  EXPECT_EQ(ext_proc_server_->ext_proc_service()->num_calls(), 1);
}

//
// Trailers Only tests
//

TEST_P(XdsExtProcEnd2endTest, TrailersOnlyProcessingModeAllEnabled) {
  CreateAndStartBackends(1);
  auto ext_proc_config =
      ExternalProcessorBuilder()
          .SetTargetUri(ext_proc_server_->target())
          .SetInsecureChannelCredentials()
          .SetObservabilityMode(false)
          .SetRequestHeaderMode(envoy::extensions::filters::http::ext_proc::v3::
                                    ProcessingMode::SEND)
          .SetResponseHeaderMode(envoy::extensions::filters::http::ext_proc::
                                     v3::ProcessingMode::SEND)
          .SetResponseTrailerMode(envoy::extensions::filters::http::ext_proc::
                                      v3::ProcessingMode::SEND)
          .SetRequestBodyMode(envoy::extensions::filters::http::ext_proc::v3::
                                  ProcessingMode::GRPC)
          .SetResponseBodyMode(envoy::extensions::filters::http::ext_proc::v3::
                                   ProcessingMode::GRPC)
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
  EchoResponse response;
  std::multimap<std::string, std::string> server_initial_metadata;
  std::multimap<std::string, std::string> server_trailing_metadata;
  Status status =
      SendRpcGetTrailers(rpc_options, &response, &server_initial_metadata,
                         &server_trailing_metadata);
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

TEST_P(XdsExtProcEnd2endTest,
       TrailersOnlyProcessingModeAllEnabledWithObservabilityMode) {
  CreateAndStartBackends(1);
  auto ext_proc_config =
      ExternalProcessorBuilder()
          .SetTargetUri(ext_proc_server_->target())
          .SetInsecureChannelCredentials()
          .SetObservabilityMode(true)
          .SetRequestHeaderMode(envoy::extensions::filters::http::ext_proc::v3::
                                    ProcessingMode::SEND)
          .SetResponseHeaderMode(envoy::extensions::filters::http::ext_proc::
                                     v3::ProcessingMode::SEND)
          .SetResponseTrailerMode(envoy::extensions::filters::http::ext_proc::
                                      v3::ProcessingMode::SEND)
          .SetRequestBodyMode(envoy::extensions::filters::http::ext_proc::v3::
                                  ProcessingMode::GRPC)
          .SetResponseBodyMode(envoy::extensions::filters::http::ext_proc::v3::
                                   ProcessingMode::GRPC)
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
  EchoResponse response;
  std::multimap<std::string, std::string> server_initial_metadata;
  std::multimap<std::string, std::string> server_trailing_metadata;
  Status status =
      SendRpcGetTrailers(rpc_options, &response, &server_initial_metadata,
                         &server_trailing_metadata);
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

//
// Request Headers tests
//

TEST_P(XdsExtProcEnd2endTest, RequestHeadersContinueAndReplaceFails) {
  auto mock_service = std::make_unique<GenericMockService>(
      [](const ::envoy::service::ext_proc::v3::ProcessingRequest& request,
         ::envoy::service::ext_proc::v3::ProcessingResponse* response) {
        if (request.has_request_headers()) {
          response->mutable_request_headers()->mutable_response()->set_status(
              ::envoy::service::ext_proc::v3::CommonResponse::
                  CONTINUE_AND_REPLACE);
        } else {
          SetDefaultEmptyResponse(request, response);
        }
        return grpc::Status::OK;
      });
  StartAlternativeServer(std::move(mock_service));
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
  CheckRpcSendFailure(DEBUG_LOCATION, StatusCode::INTERNAL,
                      "CONTINUE_AND_REPLACE is not supported");
}

TEST_P(XdsExtProcEnd2endTest,
       RequestHeadersExtProcConnectionErrorFailureModeFalse) {
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
  CheckRpcSendFailure(DEBUG_LOCATION, StatusCode::UNAVAILABLE,
                      MakeConnectionFailureRegex(
                          "failed to connect to all addresses; last error: ",
                          /*resolution_note=*/""));
}

TEST_P(XdsExtProcEnd2endTest,
       RequestHeadersExtProcConnectionErrorFailureModeTrue) {
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

TEST_P(XdsExtProcEnd2endTest, RequestHeadersInvalidHeaderMutationFails) {
  auto mock_service = std::make_unique<GenericMockService>(
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
        return grpc::Status::OK;
      });
  StartAlternativeServer(std::move(mock_service));
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
  CheckRpcSendFailure(
      DEBUG_LOCATION, StatusCode::INTERNAL,
      "Failed to parse XdsHeaderValueOption: \\[field:header\\.key "
      "error:header \"host\" not allowed\\]");
}

TEST_P(XdsExtProcEnd2endTest,
       RequestHeadersObservabilityExtProcConnectionErrorFailureModeFalse) {
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
  CheckRpcSendFailure(DEBUG_LOCATION, StatusCode::UNAVAILABLE,
                      MakeConnectionFailureRegex(
                          "failed to connect to all addresses; last error: ",
                          /*resolution_note=*/""));
}

TEST_P(XdsExtProcEnd2endTest,
       RequestHeadersObservabilityExtProcConnectionErrorFailureModeTrue) {
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

TEST_P(XdsExtProcEnd2endTest, RequestHeadersRequestAttributesSent) {
  std::string path_received;
  std::string method_received;
  absl::Mutex mu;
  auto mock_service = std::make_unique<GenericMockService>(
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
        return grpc::Status::OK;
      });
  StartAlternativeServer(std::move(mock_service));
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
  EXPECT_EQ(path_received, "/grpc.testing.EchoTestService/Echo");
  EXPECT_EQ(method_received, "POST");
}

//
// Request Attributes tests
//

TEST_P(XdsExtProcEnd2endTest,
       RequestAttributesSentInRequestBodyWhenRequestHeaderIsSkip) {
  std::string path_received;
  std::string method_received;
  bool headers_received = false;
  absl::Mutex mu;
  auto mock_service = std::make_unique<GenericMockService>(
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
          SetDefaultEmptyResponse(request, response);
        } else {
          SetDefaultEmptyResponse(request, response);
        }
        return grpc::Status::OK;
      });
  StartAlternativeServer(std::move(mock_service));
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

//
// Request Body tests
//

TEST_P(XdsExtProcEnd2endTest, RequestBodyContinueAndReplace) {
  auto mock_service = std::make_unique<GenericMockService>(
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
        return grpc::Status::OK;
      });
  StartAlternativeServer(std::move(mock_service));
  CreateAndStartBackends(1);
  using envoy::extensions::filters::http::ext_proc::v3::ProcessingMode;
  auto ext_proc_config =
      ExternalProcessorBuilder()
          .SetTargetUri(alternative_ext_proc_server_->target())
          .SetInsecureChannelCredentials()
          .SetFailureModeAllow(true)
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
  // Even though failure_mode_allow is true (fail-open), the RPC must still fail
  // because the external processor returned an unsupported response (protocol
  // error), not a connection error.
  EXPECT_THAT(status, GrpcStatusIs(StatusCode::INTERNAL,
                                   "CONTINUE_AND_REPLACE is not supported"));
  alternative_ext_proc_server_->Shutdown();
}

TEST_P(XdsExtProcEnd2endTest,
       RequestBodyExtProcConnectionErrorFailureModeFalse) {
  auto mock_service = std::make_unique<GenericMockService>(
      [](const ::envoy::service::ext_proc::v3::ProcessingRequest& request,
         ::envoy::service::ext_proc::v3::ProcessingResponse* response) {
        if (request.has_request_body()) {
          return grpc::Status(grpc::StatusCode::RESOURCE_EXHAUSTED,
                              "Call closed by ext_proc server on request body");
        }
        SetDefaultEmptyResponse(request, response);
        return grpc::Status::OK;
      });
  StartAlternativeServer(std::move(mock_service));
  CreateAndStartBackends(1);
  using envoy::extensions::filters::http::ext_proc::v3::ProcessingMode;
  auto ext_proc_config =
      ExternalProcessorBuilder()
          .SetTargetUri(alternative_ext_proc_server_->target())
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
  CheckRpcSendFailure(DEBUG_LOCATION, StatusCode::RESOURCE_EXHAUSTED,
                      "Call closed by ext_proc server on request body");
}

TEST_P(XdsExtProcEnd2endTest,
       RequestBodyExtProcConnectionErrorFailureModeTrue) {
  auto mock_service = std::make_unique<GenericMockService>(
      [](const ::envoy::service::ext_proc::v3::ProcessingRequest& request,
         ::envoy::service::ext_proc::v3::ProcessingResponse* response) {
        if (request.has_request_body()) {
          return grpc::Status(grpc::StatusCode::RESOURCE_EXHAUSTED,
                              "Call closed by ext_proc server on request body");
        }
        SetDefaultEmptyResponse(request, response);
        return grpc::Status::OK;
      });
  StartAlternativeServer(std::move(mock_service));
  CreateAndStartBackends(1);
  using envoy::extensions::filters::http::ext_proc::v3::ProcessingMode;
  auto ext_proc_config =
      ExternalProcessorBuilder()
          .SetTargetUri(alternative_ext_proc_server_->target())
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
  // NOTE: Even though failure_mode_allow is true (fail-open), because the
  // stream failed AFTER the first body message was sent to ext_proc, the filter
  // must fail the RPC to avoid message loss or inconsistent state.
  EXPECT_THAT(status,
              GrpcStatusIs(StatusCode::RESOURCE_EXHAUSTED,
                           "Call closed by ext_proc server on request body"));
}

TEST_P(XdsExtProcEnd2endTest, RequestBodyGrpcMessageCompressed) {
  auto mock_service = std::make_unique<GenericMockService>(
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
        return grpc::Status::OK;
      });
  StartAlternativeServer(std::move(mock_service));
  CreateAndStartBackends(1);
  using envoy::extensions::filters::http::ext_proc::v3::ProcessingMode;
  auto ext_proc_config =
      ExternalProcessorBuilder()
          .SetTargetUri(alternative_ext_proc_server_->target())
          .SetInsecureChannelCredentials()
          .SetFailureModeAllow(true)
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
  // Even though failure_mode_allow is true (fail-open), the RPC must still fail
  // because the external processor returned an unsupported response (protocol
  // error), not a connection error.
  EXPECT_THAT(status, GrpcStatusIs(StatusCode::INTERNAL,
                                   "grpc_message_compressed is not supported"));
}

TEST_P(XdsExtProcEnd2endTest,
       RequestBodyObservabilityExtProcConnectionErrorFailureModeFalse) {
  auto mock_service = std::make_unique<GenericMockService>(
      [](const ::envoy::service::ext_proc::v3::ProcessingRequest& request,
         ::envoy::service::ext_proc::v3::ProcessingResponse* response) {
        if (request.has_request_body()) {
          return grpc::Status(grpc::StatusCode::RESOURCE_EXHAUSTED,
                              "Call closed by ext_proc server on request body");
        }
        SetDefaultEmptyResponse(request, response);
        return grpc::Status::OK;
      });
  StartAlternativeServer(std::move(mock_service));
  CreateAndStartBackends(1);
  using envoy::extensions::filters::http::ext_proc::v3::ProcessingMode;
  auto ext_proc_config =
      ExternalProcessorBuilder()
          .SetTargetUri(alternative_ext_proc_server_->target())
          .SetInsecureChannelCredentials()
          .SetObservabilityMode(true)
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
  CheckRpcSendFailure(DEBUG_LOCATION, StatusCode::RESOURCE_EXHAUSTED,
                      "Call closed by ext_proc server on request body");
}

TEST_P(XdsExtProcEnd2endTest,
       RequestBodyObservabilityExtProcConnectionErrorFailureModeTrue) {
  auto mock_service = std::make_unique<GenericMockService>(
      [](const ::envoy::service::ext_proc::v3::ProcessingRequest& request,
         ::envoy::service::ext_proc::v3::ProcessingResponse* response) {
        if (request.has_request_body()) {
          return grpc::Status(grpc::StatusCode::RESOURCE_EXHAUSTED,
                              "Call closed by ext_proc server on request body");
        }
        SetDefaultEmptyResponse(request, response);
        return grpc::Status::OK;
      });
  StartAlternativeServer(std::move(mock_service));
  CreateAndStartBackends(1);
  using envoy::extensions::filters::http::ext_proc::v3::ProcessingMode;
  auto ext_proc_config =
      ExternalProcessorBuilder()
          .SetTargetUri(alternative_ext_proc_server_->target())
          .SetInsecureChannelCredentials()
          .SetObservabilityMode(true)
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
  EXPECT_TRUE(status.ok()) << "Expected OK, got: " << status.error_message();
}

//
// Bidirectional Streaming tests
//

TEST_P(XdsExtProcEnd2endTest, BidiStreamEarlyHalfCloseWithMessageFailure) {
  CreateAndStartBackends(1);
  auto request_body_count = std::make_shared<int>(0);
  auto ext_proc_service = std::make_unique<GenericMockService>(
      [request_body_count](
          const ::envoy::service::ext_proc::v3::ProcessingRequest& request,
          ::envoy::service::ext_proc::v3::ProcessingResponse* response) {
        SetDefaultEmptyResponse(request, response);
        if (request.has_request_body()) {
          ++(*request_body_count);
          const auto& body_req = request.request_body();
          EXPECT_FALSE(body_req.end_of_stream());
          EXPECT_FALSE(body_req.end_of_stream_without_message());
          auto* body_mutation = response->mutable_request_body()
                                    ->mutable_response()
                                    ->mutable_body_mutation();
          if (*request_body_count == 1) {
            grpc::testing::EchoRequest echo_request;
            if (echo_request.ParseFromString(request.request_body().body())) {
              echo_request.set_message(
                  absl::StrCat(echo_request.message(), kMutatedSuffix));
              std::string mutated_body;
              GRPC_CHECK(echo_request.SerializeToString(&mutated_body));
              body_mutation->mutable_streamed_response()->set_body(
                  mutated_body);
            } else {
              body_mutation->mutable_streamed_response()->set_body(
                  request.request_body().body());
            }
            body_mutation->mutable_streamed_response()->set_end_of_stream(true);
          } else {
            ADD_FAILURE() << "Processor received message after half-close";
          }
        }
        return grpc::Status::OK;
      });
  StartAlternativeServer(std::move(ext_proc_service));
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
  request.set_message(kMessage1);
  EXPECT_TRUE(stream->Write(request));
  EXPECT_TRUE(stream->Read(&response));
  EXPECT_EQ(response.message(), kMessage1Mutated);
  // Message 2 - should fail because processor half-closed
  request.set_message(kMessage2);
  stream->Write(request);
  EXPECT_FALSE(stream->Read(&response));
  Status status = stream->Finish();
  EXPECT_THAT(status,
              GrpcStatusIs(StatusCode::INTERNAL,
                           "Client sends closed by external processor"));
}

TEST_P(XdsExtProcEnd2endTest, BidiStreamEarlyHalfCloseWithoutMessageFailure) {
  CreateAndStartBackends(1);
  auto request_body_count = std::make_shared<int>(0);
  auto ext_proc_service = std::make_unique<GenericMockService>(
      [request_body_count](
          const ::envoy::service::ext_proc::v3::ProcessingRequest& request,
          ::envoy::service::ext_proc::v3::ProcessingResponse* response) {
        SetDefaultEmptyResponse(request, response);
        if (request.has_request_body()) {
          ++(*request_body_count);
          const auto& body_req = request.request_body();
          EXPECT_FALSE(body_req.end_of_stream());
          EXPECT_FALSE(body_req.end_of_stream_without_message());
          auto* body_mutation = response->mutable_request_body()
                                    ->mutable_response()
                                    ->mutable_body_mutation();
          if (*request_body_count == 1) {
            body_mutation->mutable_streamed_response()->set_end_of_stream(true);
            body_mutation->mutable_streamed_response()
                ->set_end_of_stream_without_message(true);
          } else {
            ADD_FAILURE() << "Processor received message after half-close";
          }
        }
        return grpc::Status::OK;
      });
  StartAlternativeServer(std::move(ext_proc_service));
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
  request.set_message(kMessage1);
  EXPECT_FALSE(stream->Write(request));
  // Message 2 - should fail because processor half-closed
  request.set_message(kMessage2);
  stream->Write(request);
  EXPECT_FALSE(stream->Read(&response));
  Status status = stream->Finish();
  EXPECT_THAT(status,
              GrpcStatusIs(StatusCode::INTERNAL,
                           "Client sends closed by external processor"));
}

TEST_P(XdsExtProcEnd2endTest, BidiStreamNormalHalfCloseSuccess) {
  CreateAndStartBackends(1);
  struct ExtProcClaims {
    absl::Mutex mu;
    int body_chunks ABSL_GUARDED_BY(mu) = 0;
    bool saw_eos_without_msg ABSL_GUARDED_BY(mu) = false;
  };
  auto claims = std::make_shared<ExtProcClaims>();
  auto ext_proc_service = std::make_unique<GenericMockService>(
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
        return grpc::Status::OK;
      });
  StartAlternativeServer(std::move(ext_proc_service));
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

//
// Response Headers tests
//

TEST_P(XdsExtProcEnd2endTest, ResponseHeadersContinueAndReplaceFails) {
  auto mock_service = std::make_unique<GenericMockService>(
      [](const ::envoy::service::ext_proc::v3::ProcessingRequest& request,
         ::envoy::service::ext_proc::v3::ProcessingResponse* response) {
        if (request.has_response_headers()) {
          response->mutable_response_headers()->mutable_response()->set_status(
              ::envoy::service::ext_proc::v3::CommonResponse::
                  CONTINUE_AND_REPLACE);
        } else {
          SetDefaultEmptyResponse(request, response);
        }
        return grpc::Status::OK;
      });
  StartAlternativeServer(std::move(mock_service));
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
  CheckRpcSendFailure(DEBUG_LOCATION, StatusCode::INTERNAL,
                      "CONTINUE_AND_REPLACE is not supported");
}

TEST_P(XdsExtProcEnd2endTest,
       ResponseHeadersExtProcConnectionErrorFailureModeFalse) {
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
  CheckRpcSendFailure(DEBUG_LOCATION, StatusCode::UNAVAILABLE,
                      MakeConnectionFailureRegex(
                          "failed to connect to all addresses; last error: ",
                          /*resolution_note=*/""));
}

TEST_P(XdsExtProcEnd2endTest,
       ResponseHeadersExtProcConnectionErrorFailureModeTrue) {
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

TEST_P(XdsExtProcEnd2endTest, ResponseHeadersInvalidHeaderMutationFails) {
  auto mock_service = std::make_unique<GenericMockService>(
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
        return grpc::Status::OK;
      });
  StartAlternativeServer(std::move(mock_service));
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
  CheckRpcSendFailure(
      DEBUG_LOCATION, StatusCode::INTERNAL,
      "Failed to parse XdsHeaderValueOption: \\[field:header\\.key "
      "error:header \"host\" not allowed\\]");
}

TEST_P(XdsExtProcEnd2endTest,
       ResponseHeadersObservabilityExtProcConnectionErrorFailureModeFalse) {
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
  CheckRpcSendFailure(DEBUG_LOCATION, StatusCode::UNAVAILABLE,
                      MakeConnectionFailureRegex(
                          "failed to connect to all addresses; last error: ",
                          /*resolution_note=*/""));
}

TEST_P(XdsExtProcEnd2endTest,
       ResponseHeadersObservabilityExtProcConnectionErrorFailureModeTrue) {
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

//
// Response Body tests
//

TEST_P(XdsExtProcEnd2endTest, ResponseBodyContinueAndReplace) {
  auto mock_service = std::make_unique<GenericMockService>(
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
        return grpc::Status::OK;
      });
  StartAlternativeServer(std::move(mock_service));
  CreateAndStartBackends(1);
  using envoy::extensions::filters::http::ext_proc::v3::ProcessingMode;
  auto ext_proc_config =
      ExternalProcessorBuilder()
          .SetTargetUri(alternative_ext_proc_server_->target())
          .SetInsecureChannelCredentials()
          .SetFailureModeAllow(true)
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
  CheckRpcSendFailure(DEBUG_LOCATION, StatusCode::INTERNAL,
                      "CONTINUE_AND_REPLACE is not supported");
}

TEST_P(XdsExtProcEnd2endTest,
       ResponseBodyExtProcConnectionErrorFailureModeFalse) {
  auto mock_service = std::make_unique<GenericMockService>(
      [](const ::envoy::service::ext_proc::v3::ProcessingRequest& request,
         ::envoy::service::ext_proc::v3::ProcessingResponse* response) {
        if (request.has_response_body()) {
          return grpc::Status(
              grpc::StatusCode::RESOURCE_EXHAUSTED,
              "Call closed by ext_proc server on response body");
        }
        SetDefaultEmptyResponse(request, response);
        return grpc::Status::OK;
      });
  StartAlternativeServer(std::move(mock_service));
  CreateAndStartBackends(1);
  using envoy::extensions::filters::http::ext_proc::v3::ProcessingMode;
  auto ext_proc_config =
      ExternalProcessorBuilder()
          .SetTargetUri(alternative_ext_proc_server_->target())
          .SetInsecureChannelCredentials()
          .SetFailureModeAllow(false)
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
  CheckRpcSendFailure(DEBUG_LOCATION, StatusCode::RESOURCE_EXHAUSTED,
                      "Call closed by ext_proc server on response body");
}

TEST_P(XdsExtProcEnd2endTest,
       ResponseBodyExtProcConnectionErrorFailureModeTrue) {
  auto mock_service = std::make_unique<GenericMockService>(
      [](const ::envoy::service::ext_proc::v3::ProcessingRequest& request,
         ::envoy::service::ext_proc::v3::ProcessingResponse* response) {
        if (request.has_response_body()) {
          return grpc::Status(
              grpc::StatusCode::RESOURCE_EXHAUSTED,
              "Call closed by ext_proc server on response body");
        }
        SetDefaultEmptyResponse(request, response);
        return grpc::Status::OK;
      });
  StartAlternativeServer(std::move(mock_service));
  CreateAndStartBackends(1);
  using envoy::extensions::filters::http::ext_proc::v3::ProcessingMode;
  auto ext_proc_config =
      ExternalProcessorBuilder()
          .SetTargetUri(alternative_ext_proc_server_->target())
          .SetInsecureChannelCredentials()
          .SetFailureModeAllow(true)
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
  CheckRpcSendFailure(DEBUG_LOCATION, StatusCode::RESOURCE_EXHAUSTED,
                      "Call closed by ext_proc server on response body");
}

TEST_P(XdsExtProcEnd2endTest, ResponseBodyGrpcMessageCompressed) {
  auto mock_service = std::make_unique<GenericMockService>(
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
        return grpc::Status::OK;
      });
  StartAlternativeServer(std::move(mock_service));
  CreateAndStartBackends(1);
  using envoy::extensions::filters::http::ext_proc::v3::ProcessingMode;
  auto ext_proc_config =
      ExternalProcessorBuilder()
          .SetTargetUri(alternative_ext_proc_server_->target())
          .SetInsecureChannelCredentials()
          .SetFailureModeAllow(true)
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
  RpcOptions rpc_options;
  EchoResponse response;
  Status status = SendRpcGetTrailers(rpc_options, &response, nullptr, nullptr);
  // Even though failure_mode_allow is true (fail-open), the RPC must still fail
  // because the external processor returned an unsupported response (protocol
  // error), not a connection error.
  EXPECT_THAT(status, GrpcStatusIs(StatusCode::INTERNAL,
                                   "grpc_message_compressed is not supported"));
}

TEST_P(XdsExtProcEnd2endTest, ResponseBodyObservabilityStreamErrorAllowCall) {
  auto mock_service = std::make_unique<GenericMockService>(
      [](grpc::ServerReaderWriter<
          ::envoy::service::ext_proc::v3::ProcessingResponse,
          ::envoy::service::ext_proc::v3::ProcessingRequest>* stream) {
        ::envoy::service::ext_proc::v3::ProcessingRequest request;
        while (stream->Read(&request)) {
          ::envoy::service::ext_proc::v3::ProcessingResponse response;
          if (request.has_request_headers()) {
            response.mutable_request_headers();
            stream->Write(response);
          } else if (request.has_response_headers()) {
            response.mutable_response_headers();
            stream->Write(response);
            return grpc::Status(
                grpc::StatusCode::RESOURCE_EXHAUSTED,
                "Call closed by ext_proc server after response headers");
          }
        }
        return grpc::Status::OK;
      });
  StartAlternativeServer(std::move(mock_service));
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
  request.set_message(kMessage1);
  EXPECT_TRUE(stream->Write(request));
  EXPECT_TRUE(stream->Read(&response));
  EXPECT_EQ(response.message(), kMessage1);
  stream->WritesDone();
  Status status = stream->Finish();
  EXPECT_TRUE(status.ok()) << status.error_message();
}

TEST_P(XdsExtProcEnd2endTest, ResponseBodyObservabilityStreamErrorFailCall) {
  auto mock_service = std::make_unique<GenericMockService>(
      [](grpc::ServerReaderWriter<
          ::envoy::service::ext_proc::v3::ProcessingResponse,
          ::envoy::service::ext_proc::v3::ProcessingRequest>* stream) {
        ::envoy::service::ext_proc::v3::ProcessingRequest request;
        while (stream->Read(&request)) {
          ::envoy::service::ext_proc::v3::ProcessingResponse response;
          if (request.has_request_headers()) {
            response.mutable_request_headers();
            stream->Write(response);
          } else if (request.has_response_headers()) {
            response.mutable_response_headers();
            stream->Write(response);
            return grpc::Status(
                grpc::StatusCode::RESOURCE_EXHAUSTED,
                "Call closed by ext_proc server after response headers");
          }
        }
        return grpc::Status::OK;
      });
  StartAlternativeServer(std::move(mock_service));
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
  request.set_message(kMessage1);
  stream->Write(request);
  stream->Read(&response);
  Status status = stream->Finish();
  EXPECT_THAT(
      status,
      GrpcStatusIs(StatusCode::RESOURCE_EXHAUSTED,
                   "Call closed by ext_proc server after response headers"));
}

//
// Response Trailers tests
//

TEST_P(XdsExtProcEnd2endTest,
       ResponseTrailersExtProcConnectionErrorFailureModeFalse) {
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
  CheckRpcSendFailure(DEBUG_LOCATION, StatusCode::UNAVAILABLE,
                      MakeConnectionFailureRegex(
                          "failed to connect to all addresses; last error: ",
                          /*resolution_note=*/""));
}

TEST_P(XdsExtProcEnd2endTest,
       ResponseTrailersExtProcConnectionErrorFailureModeTrue) {
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

TEST_P(XdsExtProcEnd2endTest, ResponseTrailersInvalidHeaderMutationFails) {
  auto mock_service = std::make_unique<GenericMockService>(
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
        return grpc::Status::OK;
      });
  StartAlternativeServer(std::move(mock_service));
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
  CheckRpcSendFailure(
      DEBUG_LOCATION, StatusCode::INTERNAL,
      "Failed to parse XdsHeaderValueOption: \\[field:header\\.key "
      "error:header \"host\" not allowed\\]");
}

TEST_P(XdsExtProcEnd2endTest,
       ResponseTrailersObservabilityExtProcConnectionErrorFailureModeFalse) {
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
  CheckRpcSendFailure(DEBUG_LOCATION, StatusCode::UNAVAILABLE,
                      MakeConnectionFailureRegex(
                          "failed to connect to all addresses; last error: ",
                          /*resolution_note=*/""));
}

TEST_P(XdsExtProcEnd2endTest,
       ResponseTrailersObservabilityExtProcConnectionErrorFailureModeTrue) {
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

//
// Immediate Response (Disabled) tests
//

TEST_P(XdsExtProcEnd2endTest, DisableImmediateResponseForRequestBody) {
  auto mock_service = std::make_unique<GenericMockService>(
      [](const ::envoy::service::ext_proc::v3::ProcessingRequest& request,
         ::envoy::service::ext_proc::v3::ProcessingResponse* response) {
        if (request.has_request_body()) {
          auto* immediate = response->mutable_immediate_response();
          immediate->mutable_grpc_status()->set_status(
              grpc::StatusCode::PERMISSION_DENIED);
          immediate->set_details("Access Denied by ExtProc (Request Body)");
          auto* mutation = immediate->mutable_headers();
          auto* header = mutation->add_set_headers();
          header->mutable_header()->set_key(kImmediateResponseHeaderKey);
          header->mutable_header()->set_value(kHeaderMutatedValue);
        } else {
          SetDefaultEmptyResponse(request, response);
        }
        return grpc::Status::OK;
      });
  StartAlternativeServer(std::move(mock_service));
  CreateAndStartBackends(1);
  using envoy::extensions::filters::http::ext_proc::v3::ProcessingMode;
  auto ext_proc_config =
      ExternalProcessorBuilder()
          .SetTargetUri(alternative_ext_proc_server_->target())
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
  EchoResponse response;
  std::multimap<std::string, std::string> server_initial_metadata;
  std::multimap<std::string, std::string> server_trailing_metadata;
  Status status =
      SendRpcGetTrailers(rpc_options, &response, &server_initial_metadata,
                         &server_trailing_metadata);
  EXPECT_THAT(
      status,
      GrpcStatusIs(StatusCode::INTERNAL,
                   "unhandled immediate response due to config disabled it"));
}

TEST_P(XdsExtProcEnd2endTest, DisableImmediateResponseForRequestHeaders) {
  auto mock_service = std::make_unique<GenericMockService>(
      [](const ::envoy::service::ext_proc::v3::ProcessingRequest& request,
         ::envoy::service::ext_proc::v3::ProcessingResponse* response) {
        if (request.has_request_headers()) {
          auto* immediate = response->mutable_immediate_response();
          immediate->mutable_grpc_status()->set_status(
              grpc::StatusCode::PERMISSION_DENIED);
          immediate->set_details("Access Denied by ExtProc (Request Headers)");
          auto* mutation = immediate->mutable_headers();
          auto* header = mutation->add_set_headers();
          header->mutable_header()->set_key(kImmediateResponseHeaderKey);
          header->mutable_header()->set_value(kHeaderMutatedValue);
        } else {
          SetDefaultEmptyResponse(request, response);
        }
        return grpc::Status::OK;
      });
  StartAlternativeServer(std::move(mock_service));
  CreateAndStartBackends(1);
  using envoy::extensions::filters::http::ext_proc::v3::ProcessingMode;
  auto ext_proc_config =
      ExternalProcessorBuilder()
          .SetTargetUri(alternative_ext_proc_server_->target())
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
  EchoResponse response;
  std::multimap<std::string, std::string> server_initial_metadata;
  std::multimap<std::string, std::string> server_trailing_metadata;
  Status status =
      SendRpcGetTrailers(rpc_options, &response, &server_initial_metadata,
                         &server_trailing_metadata);
  EXPECT_THAT(
      status,
      GrpcStatusIs(StatusCode::INTERNAL,
                   "unhandled immediate response due to config disabled it"));
}

TEST_P(XdsExtProcEnd2endTest, DisableImmediateResponseForResponseBody) {
  auto mock_service = std::make_unique<GenericMockService>(
      [](const ::envoy::service::ext_proc::v3::ProcessingRequest& request,
         ::envoy::service::ext_proc::v3::ProcessingResponse* response) {
        if (request.has_response_body()) {
          auto* immediate = response->mutable_immediate_response();
          immediate->mutable_grpc_status()->set_status(
              grpc::StatusCode::PERMISSION_DENIED);
          immediate->set_details("Access Denied by ExtProc (Response Body)");
          auto* mutation = immediate->mutable_headers();
          auto* header = mutation->add_set_headers();
          header->mutable_header()->set_key(kImmediateResponseHeaderKey);
          header->mutable_header()->set_value(kHeaderMutatedValue);
        } else {
          SetDefaultEmptyResponse(request, response);
        }
        return grpc::Status::OK;
      });
  StartAlternativeServer(std::move(mock_service));
  CreateAndStartBackends(1);
  using envoy::extensions::filters::http::ext_proc::v3::ProcessingMode;
  auto ext_proc_config =
      ExternalProcessorBuilder()
          .SetTargetUri(alternative_ext_proc_server_->target())
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
  EchoResponse response;
  std::multimap<std::string, std::string> server_initial_metadata;
  std::multimap<std::string, std::string> server_trailing_metadata;
  Status status =
      SendRpcGetTrailers(rpc_options, &response, &server_initial_metadata,
                         &server_trailing_metadata);
  EXPECT_THAT(
      status,
      GrpcStatusIs(StatusCode::INTERNAL,
                   "unhandled immediate response due to config disabled it"));
}

TEST_P(XdsExtProcEnd2endTest, DisableImmediateResponseForResponseHeaders) {
  auto mock_service = std::make_unique<GenericMockService>(
      [](const ::envoy::service::ext_proc::v3::ProcessingRequest& request,
         ::envoy::service::ext_proc::v3::ProcessingResponse* response) {
        if (request.has_response_headers()) {
          auto* immediate = response->mutable_immediate_response();
          immediate->mutable_grpc_status()->set_status(
              grpc::StatusCode::PERMISSION_DENIED);
          immediate->set_details("Access Denied by ExtProc (Response Headers)");
          auto* mutation = immediate->mutable_headers();
          auto* header = mutation->add_set_headers();
          header->mutable_header()->set_key(kImmediateResponseHeaderKey);
          header->mutable_header()->set_value(kHeaderMutatedValue);
        } else {
          SetDefaultEmptyResponse(request, response);
        }
        return grpc::Status::OK;
      });
  StartAlternativeServer(std::move(mock_service));
  CreateAndStartBackends(1);
  using envoy::extensions::filters::http::ext_proc::v3::ProcessingMode;
  auto ext_proc_config =
      ExternalProcessorBuilder()
          .SetTargetUri(alternative_ext_proc_server_->target())
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
  EchoResponse response;
  std::multimap<std::string, std::string> server_initial_metadata;
  std::multimap<std::string, std::string> server_trailing_metadata;
  Status status =
      SendRpcGetTrailers(rpc_options, &response, &server_initial_metadata,
                         &server_trailing_metadata);
  EXPECT_THAT(
      status,
      GrpcStatusIs(StatusCode::INTERNAL,
                   "unhandled immediate response due to config disabled it"));
}

TEST_P(XdsExtProcEnd2endTest, DisableImmediateResponseForResponseTrailers) {
  auto mock_service = std::make_unique<GenericMockService>(
      [](const ::envoy::service::ext_proc::v3::ProcessingRequest& request,
         ::envoy::service::ext_proc::v3::ProcessingResponse* response) {
        if (request.has_response_trailers()) {
          auto* immediate = response->mutable_immediate_response();
          immediate->mutable_grpc_status()->set_status(
              grpc::StatusCode::PERMISSION_DENIED);
          immediate->set_details(
              "Access Denied by ExtProc (Response Trailers)");
          auto* mutation = immediate->mutable_headers();
          auto* header = mutation->add_set_headers();
          header->mutable_header()->set_key(kImmediateResponseHeaderKey);
          header->mutable_header()->set_value(kHeaderMutatedValue);
        } else {
          SetDefaultEmptyResponse(request, response);
        }
        return grpc::Status::OK;
      });
  StartAlternativeServer(std::move(mock_service));
  CreateAndStartBackends(1);
  using envoy::extensions::filters::http::ext_proc::v3::ProcessingMode;
  auto ext_proc_config =
      ExternalProcessorBuilder()
          .SetTargetUri(alternative_ext_proc_server_->target())
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
  EchoResponse response;
  std::multimap<std::string, std::string> server_initial_metadata;
  std::multimap<std::string, std::string> server_trailing_metadata;
  Status status =
      SendRpcGetTrailers(rpc_options, &response, &server_initial_metadata,
                         &server_trailing_metadata);
  EXPECT_THAT(
      status,
      GrpcStatusIs(StatusCode::INTERNAL,
                   "unhandled immediate response due to config disabled it"));
}

//
// Immediate Response (Enabled) tests
//

TEST_P(XdsExtProcEnd2endTest, ImmediateResponseForRequestBody) {
  auto mock_service = std::make_unique<GenericMockService>(
      [](const ::envoy::service::ext_proc::v3::ProcessingRequest& request,
         ::envoy::service::ext_proc::v3::ProcessingResponse* response) {
        if (request.has_request_body()) {
          auto* immediate = response->mutable_immediate_response();
          immediate->mutable_grpc_status()->set_status(
              grpc::StatusCode::PERMISSION_DENIED);
          immediate->set_details("Access Denied by ExtProc (Request Body)");
          auto* mutation = immediate->mutable_headers();
          auto* header = mutation->add_set_headers();
          header->mutable_header()->set_key(kImmediateResponseHeaderKey);
          header->mutable_header()->set_value(kHeaderMutatedValue);
        } else {
          SetDefaultEmptyResponse(request, response);
        }
        return grpc::Status::OK;
      });
  StartAlternativeServer(std::move(mock_service));
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
  rpc_options.set_echo_metadata_initially(true);
  rpc_options.set_echo_metadata(true);
  EchoResponse response;
  std::multimap<std::string, std::string> server_initial_metadata;
  std::multimap<std::string, std::string> server_trailing_metadata;
  Status status =
      SendRpcGetTrailers(rpc_options, &response, &server_initial_metadata,
                         &server_trailing_metadata);
  EXPECT_THAT(
      status,
      GrpcStatusIs(
          StatusCode::INTERNAL,
          "Immediate response received but trailers not sent to ext_proc"));
}

TEST_P(XdsExtProcEnd2endTest, ImmediateResponseForRequestHeaders) {
  auto mock_service = std::make_unique<GenericMockService>(
      [](const ::envoy::service::ext_proc::v3::ProcessingRequest& request,
         ::envoy::service::ext_proc::v3::ProcessingResponse* response) {
        if (request.has_request_headers()) {
          auto* immediate = response->mutable_immediate_response();
          immediate->mutable_grpc_status()->set_status(
              grpc::StatusCode::PERMISSION_DENIED);
          immediate->set_details("Access Denied by ExtProc (Request Headers)");
          auto* mutation = immediate->mutable_headers();
          auto* header = mutation->add_set_headers();
          header->mutable_header()->set_key(kImmediateResponseHeaderKey);
          header->mutable_header()->set_value(kHeaderMutatedValue);
        } else {
          SetDefaultEmptyResponse(request, response);
        }
        return grpc::Status::OK;
      });
  StartAlternativeServer(std::move(mock_service));
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
  rpc_options.set_echo_metadata_initially(true);
  rpc_options.set_echo_metadata(true);
  EchoResponse response;
  std::multimap<std::string, std::string> server_initial_metadata;
  std::multimap<std::string, std::string> server_trailing_metadata;
  Status status =
      SendRpcGetTrailers(rpc_options, &response, &server_initial_metadata,
                         &server_trailing_metadata);
  EXPECT_THAT(
      status,
      GrpcStatusIs(
          StatusCode::INTERNAL,
          "Immediate response received but trailers not sent to ext_proc"));
}

TEST_P(XdsExtProcEnd2endTest, ImmediateResponseForResponseBody) {
  auto mock_service = std::make_unique<GenericMockService>(
      [](const ::envoy::service::ext_proc::v3::ProcessingRequest& request,
         ::envoy::service::ext_proc::v3::ProcessingResponse* response) {
        if (request.has_response_body()) {
          auto* immediate = response->mutable_immediate_response();
          immediate->mutable_grpc_status()->set_status(
              grpc::StatusCode::PERMISSION_DENIED);
          immediate->set_details("Access Denied by ExtProc (Response Body)");
          auto* mutation = immediate->mutable_headers();
          auto* header = mutation->add_set_headers();
          header->mutable_header()->set_key(kImmediateResponseHeaderKey);
          header->mutable_header()->set_value(kHeaderMutatedValue);
        } else {
          SetDefaultEmptyResponse(request, response);
        }
        return grpc::Status::OK;
      });
  StartAlternativeServer(std::move(mock_service));
  CreateAndStartBackends(1);
  using envoy::extensions::filters::http::ext_proc::v3::ProcessingMode;
  auto ext_proc_config =
      ExternalProcessorBuilder()
          .SetTargetUri(alternative_ext_proc_server_->target())
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
  EchoResponse response;
  std::multimap<std::string, std::string> server_initial_metadata;
  std::multimap<std::string, std::string> server_trailing_metadata;
  Status status =
      SendRpcGetTrailers(rpc_options, &response, &server_initial_metadata,
                         &server_trailing_metadata);
  EXPECT_THAT(
      status,
      GrpcStatusIs(
          StatusCode::INTERNAL,
          "Immediate response received but trailers not sent to ext_proc"));
}

TEST_P(XdsExtProcEnd2endTest, ImmediateResponseForResponseHeaders) {
  auto mock_service = std::make_unique<GenericMockService>(
      [](const ::envoy::service::ext_proc::v3::ProcessingRequest& request,
         ::envoy::service::ext_proc::v3::ProcessingResponse* response) {
        if (request.has_response_headers()) {
          auto* immediate = response->mutable_immediate_response();
          immediate->mutable_grpc_status()->set_status(
              grpc::StatusCode::PERMISSION_DENIED);
          immediate->set_details("Access Denied by ExtProc (Response Headers)");
          auto* mutation = immediate->mutable_headers();
          auto* header = mutation->add_set_headers();
          header->mutable_header()->set_key(kImmediateResponseHeaderKey);
          header->mutable_header()->set_value(kHeaderMutatedValue);
        } else {
          SetDefaultEmptyResponse(request, response);
        }
        return grpc::Status::OK;
      });
  StartAlternativeServer(std::move(mock_service));
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
  rpc_options.set_echo_metadata_initially(true);
  rpc_options.set_echo_metadata(true);
  EchoResponse response;
  std::multimap<std::string, std::string> server_initial_metadata;
  std::multimap<std::string, std::string> server_trailing_metadata;
  Status status =
      SendRpcGetTrailers(rpc_options, &response, &server_initial_metadata,
                         &server_trailing_metadata);
  EXPECT_THAT(
      status,
      GrpcStatusIs(
          StatusCode::INTERNAL,
          "Immediate response received but trailers not sent to ext_proc"));
}

TEST_P(XdsExtProcEnd2endTest, ImmediateResponseForResponseTrailers) {
  auto mock_service = std::make_unique<GenericMockService>(
      [](const ::envoy::service::ext_proc::v3::ProcessingRequest& request,
         ::envoy::service::ext_proc::v3::ProcessingResponse* response) {
        if (request.has_response_trailers()) {
          auto* immediate = response->mutable_immediate_response();
          immediate->mutable_grpc_status()->set_status(
              grpc::StatusCode::PERMISSION_DENIED);
          immediate->set_details(
              "Access Denied by ExtProc (Response Trailers)");
          auto* mutation = immediate->mutable_headers();
          auto* header = mutation->add_set_headers();
          header->mutable_header()->set_key(kImmediateResponseHeaderKey);
          header->mutable_header()->set_value(kHeaderMutatedValue);
        } else {
          SetDefaultEmptyResponse(request, response);
        }
        return grpc::Status::OK;
      });
  StartAlternativeServer(std::move(mock_service));
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
  rpc_options.set_echo_metadata_initially(true);
  rpc_options.set_echo_metadata(true);
  EchoResponse response;
  std::multimap<std::string, std::string> server_initial_metadata;
  std::multimap<std::string, std::string> server_trailing_metadata;
  Status status =
      SendRpcGetTrailers(rpc_options, &response, &server_initial_metadata,
                         &server_trailing_metadata);
  EXPECT_THAT(status,
              GrpcStatusIs(StatusCode::PERMISSION_DENIED,
                           "Access Denied by ExtProc (Response Trailers)"));
  auto it = server_trailing_metadata.find(kImmediateResponseHeaderKey);
  EXPECT_NE(it, server_trailing_metadata.end());
  EXPECT_EQ(it->second, kHeaderMutatedValue);
}

//
// Stream Drain tests
//

TEST_P(XdsExtProcEnd2endTest, StreamDrainRequestOnClientBody) {
  auto mock_service = std::make_unique<GenericMockService>(
      [](grpc::ServerReaderWriter<
          ::envoy::service::ext_proc::v3::ProcessingResponse,
          ::envoy::service::ext_proc::v3::ProcessingRequest>* stream) {
        ::envoy::service::ext_proc::v3::ProcessingRequest request;
        bool drain_triggered = false;
        while (stream->Read(&request)) {
          ::envoy::service::ext_proc::v3::ProcessingResponse response;
          if (request.has_request_headers()) {
            SetDefaultEmptyResponse(request, &response);
          } else if (request.has_request_body()) {
            if (drain_triggered) {
              return grpc::Status(grpc::StatusCode::INTERNAL,
                                  "Received request body after drain");
            }
            response.set_request_drain(true);
            drain_triggered = true;
            auto* body_mutation = response.mutable_request_body()
                                      ->mutable_response()
                                      ->mutable_body_mutation();
            grpc::testing::EchoRequest proto_req;
            if (proto_req.ParseFromString(request.request_body().body())) {
              proto_req.set_message(proto_req.message() + "_modified");
              body_mutation->mutable_streamed_response()->set_body(
                  proto_req.SerializeAsString());
            } else {
              body_mutation->mutable_streamed_response()->set_body(
                  request.request_body().body() + "_modified");
            }
          } else {
            SetDefaultEmptyResponse(request, &response);
          }
          stream->Write(response);
        }
        return grpc::Status::OK;
      });
  StartAlternativeServer(std::move(mock_service));
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
          .SetRequestBodyMode(ProcessingMode::GRPC)
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
  // Send message1. It should be modified by ext_proc.
  // The response to message1 will also trigger drain.
  request.set_message(kMessage1);
  EXPECT_TRUE(stream->Write(request));
  EXPECT_TRUE(stream->Read(&response));
  EXPECT_EQ(response.message(), "message1_modified");
  // Send message2. Since drain was triggered on message1, the ext_proc stream
  // should be closed by now, and message2 should bypass ext_proc (not
  // modified).
  request.set_message(kMessage2);
  EXPECT_TRUE(stream->Write(request));
  EXPECT_TRUE(stream->Read(&response));
  EXPECT_EQ(response.message(), kMessage2);
  stream->WritesDone();
  Status status = stream->Finish();
  EXPECT_TRUE(status.ok()) << status.error_message();
}

TEST_P(XdsExtProcEnd2endTest, StreamDrainRequestOnRequestHeaders) {
  auto mock_service = std::make_unique<GenericMockService>(
      [](grpc::ServerReaderWriter<
          ::envoy::service::ext_proc::v3::ProcessingResponse,
          ::envoy::service::ext_proc::v3::ProcessingRequest>* stream) {
        ::envoy::service::ext_proc::v3::ProcessingRequest request;
        while (stream->Read(&request)) {
          ::envoy::service::ext_proc::v3::ProcessingResponse response;
          if (request.has_request_headers()) {
            response.set_request_drain(true);
            SetDefaultEmptyResponse(request, &response);
          } else {
            SetDefaultEmptyResponse(request, &response);
          }
          stream->Write(response);
        }
        return grpc::Status::OK;
      });
  StartAlternativeServer(std::move(mock_service));
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
  ClientContext context;
  auto stream = stub_->BidiStream(&context);
  EchoRequest request;
  EchoResponse response;
  // Send message1. Since drain was triggered immediately on request headers,
  // the ext_proc stream should be half-closed/draining by now, and message1
  // should bypass ext_proc (not modified).
  request.set_message(kMessage1);
  EXPECT_TRUE(stream->Write(request));
  EXPECT_TRUE(stream->Read(&response));
  EXPECT_EQ(response.message(), kMessage1);
  stream->WritesDone();
  Status status = stream->Finish();
  EXPECT_TRUE(status.ok()) << status.error_message();
}

TEST_P(XdsExtProcEnd2endTest, StreamDrainRequestOnResponseHeaders) {
  auto mock_service = std::make_unique<GenericMockService>(
      [](grpc::ServerReaderWriter<
          ::envoy::service::ext_proc::v3::ProcessingResponse,
          ::envoy::service::ext_proc::v3::ProcessingRequest>* stream) {
        ::envoy::service::ext_proc::v3::ProcessingRequest request;
        bool drain_triggered = false;
        while (stream->Read(&request)) {
          ::envoy::service::ext_proc::v3::ProcessingResponse response;
          if (request.has_request_headers()) {
            SetDefaultEmptyResponse(request, &response);
          } else if (request.has_request_body()) {
            auto* body_mutation = response.mutable_request_body()
                                      ->mutable_response()
                                      ->mutable_body_mutation();
            grpc::testing::EchoRequest proto_req;
            if (proto_req.ParseFromString(request.request_body().body())) {
              proto_req.set_message(proto_req.message() + "_modified");
              body_mutation->mutable_streamed_response()->set_body(
                  proto_req.SerializeAsString());
            } else {
              body_mutation->mutable_streamed_response()->set_body(
                  request.request_body().body() + "_modified");
            }
          } else if (request.has_response_headers()) {
            response.set_request_drain(true);
            drain_triggered = true;
            SetDefaultEmptyResponse(request, &response);
          } else if (request.has_response_body()) {
            if (drain_triggered) {
              return grpc::Status(grpc::StatusCode::INTERNAL,
                                  "Received response body after drain");
            }
            SetDefaultEmptyResponse(request, &response);
          } else if (request.has_response_trailers()) {
            if (drain_triggered) {
              return grpc::Status(grpc::StatusCode::INTERNAL,
                                  "Received response trailers after drain");
            }
            SetDefaultEmptyResponse(request, &response);
          } else {
            SetDefaultEmptyResponse(request, &response);
          }
          stream->Write(response);
        }
        return grpc::Status::OK;
      });
  StartAlternativeServer(std::move(mock_service));
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
  ClientContext context;
  auto stream = stub_->BidiStream(&context);
  EchoRequest request;
  EchoResponse response;
  request.set_message(kMessage1);
  EXPECT_TRUE(stream->Write(request));
  EXPECT_TRUE(stream->Read(&response));
  stream->WritesDone();
  Status status = stream->Finish();
  EXPECT_TRUE(status.ok()) << status.error_message();
}

TEST_P(XdsExtProcEnd2endTest, StreamDrainRequestOnResponseTrailers) {
  auto mock_service = std::make_unique<GenericMockService>(
      [](grpc::ServerReaderWriter<
          ::envoy::service::ext_proc::v3::ProcessingResponse,
          ::envoy::service::ext_proc::v3::ProcessingRequest>* stream) {
        ::envoy::service::ext_proc::v3::ProcessingRequest request;
        while (stream->Read(&request)) {
          ::envoy::service::ext_proc::v3::ProcessingResponse response;
          if (request.has_request_headers() || request.has_response_headers()) {
            SetDefaultEmptyResponse(request, &response);
          } else if (request.has_request_body()) {
            auto* body_mutation = response.mutable_request_body()
                                      ->mutable_response()
                                      ->mutable_body_mutation();
            grpc::testing::EchoRequest proto_req;
            if (proto_req.ParseFromString(request.request_body().body())) {
              proto_req.set_message(proto_req.message() + "_modified");
              body_mutation->mutable_streamed_response()->set_body(
                  proto_req.SerializeAsString());
            } else {
              body_mutation->mutable_streamed_response()->set_body(
                  request.request_body().body() + "_modified");
            }
          } else if (request.has_response_body()) {
            auto* body_mutation = response.mutable_response_body()
                                      ->mutable_response()
                                      ->mutable_body_mutation();
            grpc::testing::EchoResponse proto_resp;
            if (proto_resp.ParseFromString(request.response_body().body())) {
              proto_resp.set_message(proto_resp.message() + "_modified");
              body_mutation->mutable_streamed_response()->set_body(
                  proto_resp.SerializeAsString());
            } else {
              body_mutation->mutable_streamed_response()->set_body(
                  request.response_body().body() + "_modified");
            }
          } else if (request.has_response_trailers()) {
            response.set_request_drain(true);
            SetDefaultEmptyResponse(request, &response);
          } else {
            SetDefaultEmptyResponse(request, &response);
          }
          stream->Write(response);
        }
        return grpc::Status::OK;
      });
  StartAlternativeServer(std::move(mock_service));
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
  ClientContext context;
  auto stream = stub_->BidiStream(&context);
  EchoRequest request;
  EchoResponse response;
  request.set_message(kMessage1);
  EXPECT_TRUE(stream->Write(request));
  EXPECT_TRUE(stream->Read(&response));
  stream->WritesDone();
  Status status = stream->Finish();
  EXPECT_TRUE(status.ok()) << status.error_message();
}

TEST_P(XdsExtProcEnd2endTest, StreamDrainRequestOnServerBody) {
  auto mock_service = std::make_unique<GenericMockService>(
      [](grpc::ServerReaderWriter<
          ::envoy::service::ext_proc::v3::ProcessingResponse,
          ::envoy::service::ext_proc::v3::ProcessingRequest>* stream) {
        ::envoy::service::ext_proc::v3::ProcessingRequest request;
        bool drain_triggered = false;
        while (stream->Read(&request)) {
          ::envoy::service::ext_proc::v3::ProcessingResponse response;
          if (request.has_request_headers() || request.has_response_headers()) {
            SetDefaultEmptyResponse(request, &response);
          } else if (request.has_response_body()) {
            if (drain_triggered) {
              return grpc::Status(grpc::StatusCode::INTERNAL,
                                  "Received response body after drain");
            }
            response.set_request_drain(true);
            drain_triggered = true;
            auto* body_mutation = response.mutable_response_body()
                                      ->mutable_response()
                                      ->mutable_body_mutation();
            grpc::testing::EchoResponse proto_resp;
            if (proto_resp.ParseFromString(request.response_body().body())) {
              proto_resp.set_message(proto_resp.message() + "_modified");
              body_mutation->mutable_streamed_response()->set_body(
                  proto_resp.SerializeAsString());
            } else {
              body_mutation->mutable_streamed_response()->set_body(
                  request.response_body().body() + "_modified");
            }
          } else {
            SetDefaultEmptyResponse(request, &response);
          }
          stream->Write(response);
        }
        return grpc::Status::OK;
      });
  StartAlternativeServer(std::move(mock_service));
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
  // Send request1. Response1 should be modified.
  request.set_message(kMessage1);
  EXPECT_TRUE(stream->Write(request));
  EXPECT_TRUE(stream->Read(&response));
  EXPECT_EQ(response.message(), "message1_modified");
  // Send request2. Response2 should bypass ext_proc.
  request.set_message(kMessage2);
  EXPECT_TRUE(stream->Write(request));
  EXPECT_TRUE(stream->Read(&response));
  EXPECT_EQ(response.message(), kMessage2);
  stream->WritesDone();
  Status status = stream->Finish();
  EXPECT_TRUE(status.ok()) << status.error_message();
}

//
// Ordering tests
//

TEST_P(XdsExtProcEnd2endTest,
       ClientToServerOrderingHeadersResponseWhenDisabled) {
  auto mock_service = std::make_unique<GenericMockService>(
      [](const ::envoy::service::ext_proc::v3::ProcessingRequest& request,
         ::envoy::service::ext_proc::v3::ProcessingResponse* response) {
        if (request.has_request_body()) {
          // Respond with request_headers instead of request_body!
          response->mutable_request_headers()->mutable_response();
        } else {
          SetDefaultEmptyResponse(request, response);
        }
        return grpc::Status::OK;
      });
  StartAlternativeServer(std::move(mock_service));
  CreateAndStartBackends(1);
  using envoy::extensions::filters::http::ext_proc::v3::ProcessingMode;
  auto ext_proc_config =
      ExternalProcessorBuilder()
          .SetTargetUri(alternative_ext_proc_server_->target())
          .SetInsecureChannelCredentials()
          .SetFailureModeAllow(false)
          .SetRequestHeaderMode(ProcessingMode::SKIP)
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
  request.set_message(kRequestMessage);
  stream->Write(request);
  EchoResponse response;
  EXPECT_FALSE(stream->Read(&response));
  Status status = stream->Finish();
  EXPECT_THAT(status, GrpcStatusIs(StatusCode::INTERNAL,
                                   "Received request headers response but "
                                   "request headers are disabled"));
}

TEST_P(XdsExtProcEnd2endTest, ClientToServerOrderingResponseBodyBeforeHeaders) {
  auto mock_service = std::make_unique<GenericMockService>(
      [](const ::envoy::service::ext_proc::v3::ProcessingRequest& request,
         ::envoy::service::ext_proc::v3::ProcessingResponse* response) {
        if (request.has_request_headers()) {
          // Respond with request_body instead of request_headers!
          auto* body_response = response->mutable_request_body();
          auto* common_response = body_response->mutable_response();
          common_response->mutable_body_mutation()->mutable_streamed_response();
        } else {
          SetDefaultEmptyResponse(request, response);
        }
        return grpc::Status::OK;
      });
  StartAlternativeServer(std::move(mock_service));
  CreateAndStartBackends(1);
  using envoy::extensions::filters::http::ext_proc::v3::ProcessingMode;
  auto ext_proc_config =
      ExternalProcessorBuilder()
          .SetTargetUri(alternative_ext_proc_server_->target())
          .SetInsecureChannelCredentials()
          .SetFailureModeAllow(true)
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
  request.set_message(kRequestMessage);
  stream->Write(request);
  EchoResponse response;
  EXPECT_FALSE(stream->Read(&response));
  Status status = stream->Finish();
  EXPECT_THAT(status, GrpcStatusIs(StatusCode::INTERNAL,
                                   "Received request body response before "
                                   "request headers response"));
}

TEST_P(XdsExtProcEnd2endTest,
       ServerToClientOrderingHeadersResponseWhenDisabled) {
  auto mock_service = std::make_unique<GenericMockService>(
      [](const ::envoy::service::ext_proc::v3::ProcessingRequest& request,
         ::envoy::service::ext_proc::v3::ProcessingResponse* response) {
        if (request.has_response_body()) {
          // Respond with response_headers instead of response_body!
          response->mutable_response_headers()->mutable_response();
        } else {
          SetDefaultEmptyResponse(request, response);
        }
        return grpc::Status::OK;
      });
  StartAlternativeServer(std::move(mock_service));
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
  request.set_message(kRequestMessage);
  stream->Write(request);
  EchoResponse response;
  EXPECT_FALSE(stream->Read(&response));
  Status status = stream->Finish();
  EXPECT_THAT(status, GrpcStatusIs(StatusCode::INTERNAL,
                                   "Received response headers response but "
                                   "response headers are disabled"));
}

TEST_P(XdsExtProcEnd2endTest, ServerToClientOrderingResponseBodyBeforeHeaders) {
  auto mock_service = std::make_unique<GenericMockService>(
      [](const ::envoy::service::ext_proc::v3::ProcessingRequest& request,
         ::envoy::service::ext_proc::v3::ProcessingResponse* response) {
        if (request.has_response_headers()) {
          // Respond with response_body instead of response_headers!
          auto* body_response = response->mutable_response_body();
          auto* common_response = body_response->mutable_response();
          common_response->mutable_body_mutation()->mutable_streamed_response();
        } else {
          SetDefaultEmptyResponse(request, response);
        }
        return grpc::Status::OK;
      });
  StartAlternativeServer(std::move(mock_service));
  CreateAndStartBackends(1);
  using envoy::extensions::filters::http::ext_proc::v3::ProcessingMode;
  auto ext_proc_config =
      ExternalProcessorBuilder()
          .SetTargetUri(alternative_ext_proc_server_->target())
          .SetInsecureChannelCredentials()
          .SetFailureModeAllow(false)
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
  // Use WritesDone() to trigger S2C headers without sending body messages,
  // avoiding race conditions.
  stream->WritesDone();
  EchoResponse response;
  EXPECT_FALSE(stream->Read(&response));
  Status status = stream->Finish();
  EXPECT_THAT(status, GrpcStatusIs(StatusCode::INTERNAL,
                                   "Received response body response before "
                                   "response headers response"));
}

TEST_P(XdsExtProcEnd2endTest, ServerToClientOrderingTrailersBeforeHeaders) {
  auto mock_service = std::make_unique<GenericMockService>(
      [](const ::envoy::service::ext_proc::v3::ProcessingRequest& request,
         ::envoy::service::ext_proc::v3::ProcessingResponse* response) {
        if (request.has_response_headers()) {
          // Respond with response_trailers instead of response_headers!
          response->mutable_response_trailers();
        } else {
          SetDefaultEmptyResponse(request, response);
        }
        return grpc::Status::OK;
      });
  StartAlternativeServer(std::move(mock_service));
  CreateAndStartBackends(1);
  using envoy::extensions::filters::http::ext_proc::v3::ProcessingMode;
  auto ext_proc_config =
      ExternalProcessorBuilder()
          .SetTargetUri(alternative_ext_proc_server_->target())
          .SetInsecureChannelCredentials()
          .SetFailureModeAllow(false)
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
  EXPECT_THAT(status, GrpcStatusIs(StatusCode::INTERNAL,
                                   "Received response trailers response before "
                                   "response headers response"));
}

TEST_P(XdsExtProcEnd2endTest,
       ServerToClientOrderingTrailersBeforeResponseBody) {
  auto mock_service = std::make_unique<GenericMockService>(
      [](const ::envoy::service::ext_proc::v3::ProcessingRequest& request,
         ::envoy::service::ext_proc::v3::ProcessingResponse* response) {
        if (request.has_response_body()) {
          // Respond with response_trailers instead of response_body!
          response->mutable_response_trailers();
        } else {
          SetDefaultEmptyResponse(request, response);
        }
        return grpc::Status::OK;
      });
  StartAlternativeServer(std::move(mock_service));
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
  request.set_message(kRequestMessage);
  stream->Write(request);
  EchoResponse response;
  EXPECT_FALSE(stream->Read(&response));
  Status status = stream->Finish();
  EXPECT_THAT(status, GrpcStatusIs(StatusCode::INTERNAL,
                                   "Received response trailers response before "
                                   "all outstanding response body responses "
                                   "were received"));
}

TEST_P(XdsExtProcEnd2endTest,
       ServerToClientOrderingTrailersResponseWhenDisabled) {
  auto mock_service = std::make_unique<GenericMockService>(
      [](const ::envoy::service::ext_proc::v3::ProcessingRequest& request,
         ::envoy::service::ext_proc::v3::ProcessingResponse* response) {
        if (request.has_response_headers()) {
          // Respond with response_trailers instead of response_headers!
          response->mutable_response_trailers();
        } else {
          SetDefaultEmptyResponse(request, response);
        }
        return grpc::Status::OK;
      });
  StartAlternativeServer(std::move(mock_service));
  CreateAndStartBackends(1);
  using envoy::extensions::filters::http::ext_proc::v3::ProcessingMode;
  auto ext_proc_config =
      ExternalProcessorBuilder()
          .SetTargetUri(alternative_ext_proc_server_->target())
          .SetInsecureChannelCredentials()
          .SetFailureModeAllow(false)
          .SetRequestHeaderMode(ProcessingMode::SEND)
          .SetRequestBodyMode(ProcessingMode::GRPC)
          .SetResponseHeaderMode(ProcessingMode::SEND)
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
  EXPECT_THAT(status, GrpcStatusIs(StatusCode::INTERNAL,
                                   "Received response trailers response but "
                                   "response trailers are disabled"));
}

TEST_P(XdsExtProcEnd2endTest, ServerToClientResponseBodyHalfClose) {
  auto mock_service = std::make_unique<GenericMockService>(
      [](grpc::ServerReaderWriter<
          ::envoy::service::ext_proc::v3::ProcessingResponse,
          ::envoy::service::ext_proc::v3::ProcessingRequest>* stream) {
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
      });
  StartAlternativeServer(std::move(mock_service));
  CreateAndStartBackends(1);
  using envoy::extensions::filters::http::ext_proc::v3::ProcessingMode;
  auto ext_proc_config =
      ExternalProcessorBuilder()
          .SetTargetUri(alternative_ext_proc_server_->target())
          .SetInsecureChannelCredentials()
          .SetFailureModeAllow(true)
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
  request.set_message(kRequestMessage);
  EXPECT_TRUE(stream->Write(request));
  EchoResponse response;
  EXPECT_FALSE(stream->Read(&response));
  Status status = stream->Finish();
  EXPECT_THAT(status,
              GrpcStatusIs(StatusCode::INTERNAL,
                           "end_of_stream / end_of_stream_without_message "
                           "is not supported for response_body"));
}

//
// Stream Clean Close tests
//

TEST_P(XdsExtProcEnd2endTest, StreamCleanCloseRequestBodyFailureModeFalse) {
  auto mock_service = std::make_unique<GenericMockService>(
      [](grpc::ServerReaderWriter<
          ::envoy::service::ext_proc::v3::ProcessingResponse,
          ::envoy::service::ext_proc::v3::ProcessingRequest>* stream) {
        ::envoy::service::ext_proc::v3::ProcessingRequest request;
        if (stream->Read(&request)) {
          if (request.has_request_headers()) {
            ::envoy::service::ext_proc::v3::ProcessingResponse response;
            SetDefaultEmptyResponse(request, &response);
            stream->Write(response);
            return grpc::Status::OK;
          }
        }
        return grpc::Status::OK;
      });
  StartAlternativeServer(std::move(mock_service));
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
          .SetRequestBodyMode(ProcessingMode::GRPC)
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
  request.set_message(kMessage1);
  stream->Write(request);
  EXPECT_FALSE(stream->Read(&response));
  stream->WritesDone();
  Status status = stream->Finish();
  EXPECT_THAT(status, GrpcStatusIs(StatusCode::INTERNAL,
                                   "Stream closed cleanly without drain"));
}

TEST_P(XdsExtProcEnd2endTest, StreamCleanCloseRequestBodyFailureModeTrue) {
  auto mock_service = std::make_unique<GenericMockService>(
      [](grpc::ServerReaderWriter<
          ::envoy::service::ext_proc::v3::ProcessingResponse,
          ::envoy::service::ext_proc::v3::ProcessingRequest>* stream) {
        ::envoy::service::ext_proc::v3::ProcessingRequest request;
        if (stream->Read(&request)) {
          if (request.has_request_headers()) {
            ::envoy::service::ext_proc::v3::ProcessingResponse response;
            SetDefaultEmptyResponse(request, &response);
            stream->Write(response);
            return grpc::Status::OK;
          }
        }
        return grpc::Status::OK;
      });
  StartAlternativeServer(std::move(mock_service));
  CreateAndStartBackends(1);
  ResetStubWithUniqueArg();
  using envoy::extensions::filters::http::ext_proc::v3::ProcessingMode;
  auto ext_proc_config =
      ExternalProcessorBuilder()
          .SetTargetUri(alternative_ext_proc_server_->target())
          .SetInsecureChannelCredentials()
          .SetFailureModeAllow(true)
          .SetRequestHeaderMode(ProcessingMode::SEND)
          .SetResponseHeaderMode(ProcessingMode::SEND)
          .SetRequestBodyMode(ProcessingMode::GRPC)
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
  request.set_message(kMessage1);
  EXPECT_TRUE(stream->Write(request));
  EXPECT_TRUE(stream->Read(&response));
  EXPECT_EQ(response.message(), kMessage1);
  stream->WritesDone();
  Status status = stream->Finish();
  EXPECT_TRUE(status.ok()) << status.error_message();
}

TEST_P(XdsExtProcEnd2endTest,
       StreamCleanCloseRequestBodyWithInFlightFailureModeFalse) {
  auto mock_service = std::make_unique<GenericMockService>(
      [](grpc::ServerReaderWriter<
          ::envoy::service::ext_proc::v3::ProcessingResponse,
          ::envoy::service::ext_proc::v3::ProcessingRequest>* stream) {
        ::envoy::service::ext_proc::v3::ProcessingRequest request;
        while (stream->Read(&request)) {
          ::envoy::service::ext_proc::v3::ProcessingResponse response;
          if (request.has_request_body()) {
            auto* body_mutation = response.mutable_request_body()
                                      ->mutable_response()
                                      ->mutable_body_mutation();
            body_mutation->mutable_streamed_response()->set_body(
                request.request_body().body());
            stream->Write(response);
            return grpc::Status::OK;
          }
          SetDefaultEmptyResponse(request, &response);
          stream->Write(response);
        }
        return grpc::Status::OK;
      });
  StartAlternativeServer(std::move(mock_service));
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
          .SetRequestBodyMode(ProcessingMode::GRPC)
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
  request.set_message(kMessage1);
  stream->Write(request);
  stream->WritesDone();
  Status status = stream->Finish();
  EXPECT_THAT(status, GrpcStatusIs(StatusCode::INTERNAL,
                                   "Stream closed cleanly without drain"));
}

TEST_P(XdsExtProcEnd2endTest,
       StreamCleanCloseRequestBodyWithInFlightFailureModeTrue) {
  auto mock_service = std::make_unique<GenericMockService>(
      [](grpc::ServerReaderWriter<
          ::envoy::service::ext_proc::v3::ProcessingResponse,
          ::envoy::service::ext_proc::v3::ProcessingRequest>* stream) {
        ::envoy::service::ext_proc::v3::ProcessingRequest request;
        while (stream->Read(&request)) {
          ::envoy::service::ext_proc::v3::ProcessingResponse response;
          if (request.has_request_body()) {
            return grpc::Status::OK;
          }
          SetDefaultEmptyResponse(request, &response);
          stream->Write(response);
        }
        return grpc::Status::OK;
      });
  StartAlternativeServer(std::move(mock_service));
  CreateAndStartBackends(1);
  ResetStubWithUniqueArg();
  using envoy::extensions::filters::http::ext_proc::v3::ProcessingMode;
  auto ext_proc_config =
      ExternalProcessorBuilder()
          .SetTargetUri(alternative_ext_proc_server_->target())
          .SetInsecureChannelCredentials()
          .SetFailureModeAllow(true)
          .SetRequestHeaderMode(ProcessingMode::SEND)
          .SetResponseHeaderMode(ProcessingMode::SEND)
          .SetRequestBodyMode(ProcessingMode::GRPC)
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
  request.set_message(kMessage1);
  // The first body message is successfully sent to ext_proc, which sets
  // c2s_first_body_message_sent_ to true. Once this is set, the filter
  // is committed to body processing and cannot fail-open.
  // So when the ext_proc stream closes cleanly without drain during the body
  // phase, the RPC should fail even though failure_mode_allow is true.
  stream->Write(request);
  stream->WritesDone();
  Status status = stream->Finish();
  EXPECT_THAT(status, GrpcStatusIs(StatusCode::INTERNAL,
                                   "Stream closed cleanly without drain"));
}

TEST_P(XdsExtProcEnd2endTest, StreamCleanCloseRequestHeadersFailureModeFalse) {
  auto mock_service = std::make_unique<GenericMockService>(
      [](grpc::ServerReaderWriter<
          ::envoy::service::ext_proc::v3::ProcessingResponse,
          ::envoy::service::ext_proc::v3::ProcessingRequest>* /*stream*/) {
        return grpc::Status::OK;
      });
  StartAlternativeServer(std::move(mock_service));
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
  request.set_message(kMessage1);
  EXPECT_TRUE(stream->Write(request));
  EXPECT_TRUE(stream->Read(&response));
  EXPECT_EQ(response.message(), kMessage1);
  stream->WritesDone();
  Status status = stream->Finish();
  EXPECT_TRUE(status.ok()) << status.error_message();
}

TEST_P(XdsExtProcEnd2endTest, StreamCleanCloseRequestHeadersFailureModeTrue) {
  auto mock_service = std::make_unique<GenericMockService>(
      [](grpc::ServerReaderWriter<
          ::envoy::service::ext_proc::v3::ProcessingResponse,
          ::envoy::service::ext_proc::v3::ProcessingRequest>* /*stream*/) {
        return grpc::Status::OK;
      });
  StartAlternativeServer(std::move(mock_service));
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
  request.set_message(kMessage1);
  EXPECT_TRUE(stream->Write(request));
  EXPECT_TRUE(stream->Read(&response));
  EXPECT_EQ(response.message(), kMessage1);
  stream->WritesDone();
  Status status = stream->Finish();
  EXPECT_TRUE(status.ok()) << status.error_message();
}

TEST_P(XdsExtProcEnd2endTest, StreamCleanCloseResponseBodyFailureModeFalse) {
  auto mock_service = std::make_unique<GenericMockService>(
      [](grpc::ServerReaderWriter<
          ::envoy::service::ext_proc::v3::ProcessingResponse,
          ::envoy::service::ext_proc::v3::ProcessingRequest>* stream) {
        ::envoy::service::ext_proc::v3::ProcessingRequest request;
        while (stream->Read(&request)) {
          ::envoy::service::ext_proc::v3::ProcessingResponse response;
          SetDefaultEmptyResponse(request, &response);
          stream->Write(response);
          if (request.has_response_headers()) {
            return grpc::Status::OK;
          }
        }
        return grpc::Status::OK;
      });
  StartAlternativeServer(std::move(mock_service));
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
  ClientContext context;
  auto stream = stub_->BidiStream(&context);
  EchoRequest request;
  EchoResponse response;
  request.set_message(kMessage1);
  EXPECT_TRUE(stream->Write(request));
  EXPECT_FALSE(stream->Read(&response));
  stream->WritesDone();
  Status status = stream->Finish();
  EXPECT_THAT(status, GrpcStatusIs(StatusCode::INTERNAL,
                                   "Stream closed cleanly without drain"));
}

TEST_P(XdsExtProcEnd2endTest, StreamCleanCloseResponseBodyFailureModeTrue) {
  auto mock_service = std::make_unique<GenericMockService>(
      [](grpc::ServerReaderWriter<
          ::envoy::service::ext_proc::v3::ProcessingResponse,
          ::envoy::service::ext_proc::v3::ProcessingRequest>* stream) {
        ::envoy::service::ext_proc::v3::ProcessingRequest request;
        while (stream->Read(&request)) {
          ::envoy::service::ext_proc::v3::ProcessingResponse response;
          SetDefaultEmptyResponse(request, &response);
          stream->Write(response);
          if (request.has_response_headers()) {
            return grpc::Status::OK;
          }
        }
        return grpc::Status::OK;
      });
  StartAlternativeServer(std::move(mock_service));
  CustomBidiStreamServiceImpl custom_backend_service;
  ServerBuilder backend_builder;
  int custom_backend_port = grpc_pick_unused_port_or_die();
  backend_builder.AddListeningPort(
      absl::StrCat("localhost:", custom_backend_port),
      CreateFakeServerCredentials());
  backend_builder.RegisterService(&custom_backend_service);
  auto custom_backend_server = backend_builder.BuildAndStart();
  ResetStubWithUniqueArg();
  using envoy::extensions::filters::http::ext_proc::v3::ProcessingMode;
  auto ext_proc_config =
      ExternalProcessorBuilder()
          .SetTargetUri(alternative_ext_proc_server_->target())
          .SetInsecureChannelCredentials()
          .SetFailureModeAllow(true)
          .SetRequestHeaderMode(ProcessingMode::SEND)
          .SetResponseHeaderMode(ProcessingMode::SEND)
          .SetResponseTrailerMode(ProcessingMode::SEND)
          .SetResponseBodyMode(ProcessingMode::GRPC)
          .Build();
  Listener listener = BuildListenerWithExtProcFilter(ext_proc_config);
  RouteConfiguration route_config = default_route_config_;
  SetListenerAndRouteConfiguration(balancer_.get(), listener, route_config);
  balancer_->ads_service()->SetCdsResource(default_cluster_);
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(EdsResourceArgs({
      {"locality0", {EdsResourceArgs::Endpoint(custom_backend_port)}},
  })));
  ClientContext context;
  auto stream = stub_->BidiStream(&context);
  EchoRequest request;
  EchoResponse response;
  request.set_message(kMessage1);
  EXPECT_TRUE(stream->Write(request));
  stream->WaitForInitialMetadata();
  request.set_message(kMessage2);
  EXPECT_TRUE(stream->Write(request));
  EXPECT_TRUE(stream->Read(&response));
  EXPECT_EQ(response.message(), kMessage1);
  stream->WritesDone();
  Status status = stream->Finish();
  EXPECT_TRUE(status.ok()) << status.error_message();
  custom_backend_server->Shutdown();
}

TEST_P(XdsExtProcEnd2endTest,
       StreamCleanCloseResponseBodyWithInFlightFailureModeFalse) {
  auto mock_service = std::make_unique<GenericMockService>(
      [](grpc::ServerReaderWriter<
          ::envoy::service::ext_proc::v3::ProcessingResponse,
          ::envoy::service::ext_proc::v3::ProcessingRequest>* stream) {
        ::envoy::service::ext_proc::v3::ProcessingRequest request;
        int response_body_count = 0;
        while (stream->Read(&request)) {
          if (request.has_response_body()) {
            response_body_count++;
            if (response_body_count == 2) {
              return grpc::Status::OK;
            }
          }
          ::envoy::service::ext_proc::v3::ProcessingResponse response;
          SetDefaultEmptyResponse(request, &response);
          stream->Write(response);
        }
        return grpc::Status::OK;
      });
  StartAlternativeServer(std::move(mock_service));
  CreateAndStartBackends(1);
  ResetStubWithUniqueArg();
  using envoy::extensions::filters::http::ext_proc::v3::ProcessingMode;
  auto ext_proc_config =
      ExternalProcessorBuilder()
          .SetTargetUri(alternative_ext_proc_server_->target())
          .SetInsecureChannelCredentials()
          .SetFailureModeAllow(false)
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
  request.set_message(kMessage1);
  EXPECT_TRUE(stream->Write(request));
  EchoResponse response;
  EXPECT_TRUE(stream->Read(&response));
  EXPECT_EQ(response.message(), kMessage1);
  // Send second message. It will be sent to ext_proc, and stream will close
  // before ext_proc responds. Since the filter is committed, the RPC should
  // fail.
  request.set_message(kMessage2);
  if (stream->Write(request)) {
    // Read should fail because the RPC will be failed.
    EXPECT_FALSE(stream->Read(&response));
  }
  stream->WritesDone();
  Status status = stream->Finish();
  EXPECT_THAT(status, GrpcStatusIs(StatusCode::INTERNAL,
                                   "Stream closed cleanly without drain"));
}

TEST_P(XdsExtProcEnd2endTest,
       StreamCleanCloseResponseBodyWithInFlightFailureModeTrue) {
  auto mock_service = std::make_unique<GenericMockService>(
      [](grpc::ServerReaderWriter<
          ::envoy::service::ext_proc::v3::ProcessingResponse,
          ::envoy::service::ext_proc::v3::ProcessingRequest>* stream) {
        ::envoy::service::ext_proc::v3::ProcessingRequest request;
        int response_body_count = 0;
        while (stream->Read(&request)) {
          if (request.has_response_body()) {
            response_body_count++;
            if (response_body_count == 2) {
              return grpc::Status::OK;
            }
          }
          ::envoy::service::ext_proc::v3::ProcessingResponse response;
          SetDefaultEmptyResponse(request, &response);
          stream->Write(response);
        }
        return grpc::Status::OK;
      });
  StartAlternativeServer(std::move(mock_service));
  CreateAndStartBackends(1);
  ResetStubWithUniqueArg();
  using envoy::extensions::filters::http::ext_proc::v3::ProcessingMode;
  auto ext_proc_config =
      ExternalProcessorBuilder()
          .SetTargetUri(alternative_ext_proc_server_->target())
          .SetInsecureChannelCredentials()
          .SetFailureModeAllow(true)
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
  request.set_message(kMessage1);
  EXPECT_TRUE(stream->Write(request));
  EchoResponse response;
  EXPECT_TRUE(stream->Read(&response));
  EXPECT_EQ(response.message(), kMessage1);
  request.set_message(kMessage2);
  if (stream->Write(request)) {
    EXPECT_FALSE(stream->Read(&response));
  }
  stream->WritesDone();
  Status status = stream->Finish();
  EXPECT_THAT(status, GrpcStatusIs(StatusCode::INTERNAL,
                                   "Stream closed cleanly without drain"));
}

TEST_P(XdsExtProcEnd2endTest, StreamCleanCloseResponseHeadersFailureModeFalse) {
  auto mock_service = std::make_unique<GenericMockService>(
      [](grpc::ServerReaderWriter<
          ::envoy::service::ext_proc::v3::ProcessingResponse,
          ::envoy::service::ext_proc::v3::ProcessingRequest>* stream) {
        ::envoy::service::ext_proc::v3::ProcessingRequest request;
        if (stream->Read(&request)) {
          if (request.has_request_headers()) {
            ::envoy::service::ext_proc::v3::ProcessingResponse response;
            SetDefaultEmptyResponse(request, &response);
            stream->Write(response);
            return grpc::Status::OK;
          }
        }
        return grpc::Status::OK;
      });
  StartAlternativeServer(std::move(mock_service));
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
  ClientContext context;
  auto stream = stub_->BidiStream(&context);
  EchoRequest request;
  EchoResponse response;
  request.set_message(kMessage1);
  stream->Write(request);
  EXPECT_FALSE(stream->Read(&response));
  stream->WritesDone();
  Status status = stream->Finish();
  EXPECT_THAT(status, GrpcStatusIs(StatusCode::INTERNAL,
                                   "Stream closed cleanly without drain"));
}

TEST_P(XdsExtProcEnd2endTest, StreamCleanCloseResponseHeadersFailureModeTrue) {
  auto mock_service = std::make_unique<GenericMockService>(
      [](grpc::ServerReaderWriter<
          ::envoy::service::ext_proc::v3::ProcessingResponse,
          ::envoy::service::ext_proc::v3::ProcessingRequest>* stream) {
        ::envoy::service::ext_proc::v3::ProcessingRequest request;
        if (stream->Read(&request)) {
          if (request.has_request_headers()) {
            ::envoy::service::ext_proc::v3::ProcessingResponse response;
            SetDefaultEmptyResponse(request, &response);
            stream->Write(response);
            return grpc::Status::OK;
          }
        }
        return grpc::Status::OK;
      });
  StartAlternativeServer(std::move(mock_service));
  CreateAndStartBackends(1);
  ResetStubWithUniqueArg();
  using envoy::extensions::filters::http::ext_proc::v3::ProcessingMode;
  auto ext_proc_config =
      ExternalProcessorBuilder()
          .SetTargetUri(alternative_ext_proc_server_->target())
          .SetInsecureChannelCredentials()
          .SetFailureModeAllow(true)
          .SetRequestHeaderMode(ProcessingMode::SEND)
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
  ClientContext context;
  auto stream = stub_->BidiStream(&context);
  EchoRequest request;
  EchoResponse response;
  request.set_message(kMessage1);
  EXPECT_TRUE(stream->Write(request));
  EXPECT_TRUE(stream->Read(&response));
  EXPECT_EQ(response.message(), kMessage1);
  stream->WritesDone();
  Status status = stream->Finish();
  EXPECT_TRUE(status.ok()) << status.error_message();
}

TEST_P(XdsExtProcEnd2endTest,
       StreamCleanCloseResponseTrailersFailureModeFalse) {
  auto mock_service = std::make_unique<GenericMockService>(
      [](grpc::ServerReaderWriter<
          ::envoy::service::ext_proc::v3::ProcessingResponse,
          ::envoy::service::ext_proc::v3::ProcessingRequest>* stream) {
        return grpc::Status::OK;
      });
  StartAlternativeServer(std::move(mock_service));
  CreateAndStartBackends(1);
  ResetStubWithUniqueArg();
  using envoy::extensions::filters::http::ext_proc::v3::ProcessingMode;
  auto ext_proc_config =
      ExternalProcessorBuilder()
          .SetTargetUri(alternative_ext_proc_server_->target())
          .SetInsecureChannelCredentials()
          .SetFailureModeAllow(false)
          .SetObservabilityMode(false)
          .SetResponseTrailerMode(ProcessingMode::SEND)
          .Build();
  Listener listener = BuildListenerWithExtProcFilter(ext_proc_config);
  RouteConfiguration route_config = default_route_config_;
  SetListenerAndRouteConfiguration(balancer_.get(), listener, route_config);
  balancer_->ads_service()->SetCdsResource(default_cluster_);
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(EdsResourceArgs({
      {"locality0", CreateEndpointsForBackends(0, 1)},
  })));
  CheckRpcSendOk(DEBUG_LOCATION);
}

TEST_P(XdsExtProcEnd2endTest, StreamCleanCloseResponseTrailersFailureModeTrue) {
  auto mock_service = std::make_unique<GenericMockService>(
      [](grpc::ServerReaderWriter<
          ::envoy::service::ext_proc::v3::ProcessingResponse,
          ::envoy::service::ext_proc::v3::ProcessingRequest>* stream) {
        return grpc::Status::OK;
      });
  StartAlternativeServer(std::move(mock_service));
  CreateAndStartBackends(1);
  ResetStubWithUniqueArg();
  using envoy::extensions::filters::http::ext_proc::v3::ProcessingMode;
  auto ext_proc_config =
      ExternalProcessorBuilder()
          .SetTargetUri(alternative_ext_proc_server_->target())
          .SetInsecureChannelCredentials()
          .SetFailureModeAllow(true)
          .SetObservabilityMode(false)
          .SetResponseTrailerMode(ProcessingMode::SEND)
          .Build();
  Listener listener = BuildListenerWithExtProcFilter(ext_proc_config);
  RouteConfiguration route_config = default_route_config_;
  SetListenerAndRouteConfiguration(balancer_.get(), listener, route_config);
  balancer_->ads_service()->SetCdsResource(default_cluster_);
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(EdsResourceArgs({
      {"locality0", CreateEndpointsForBackends(0, 1)},
  })));
  CheckRpcSendOk(DEBUG_LOCATION);
}

//
// Stream Clean Close Observability tests
//

TEST_P(XdsExtProcEnd2endTest, StreamCleanCloseRequestBodyObservability) {
  auto mock_service = std::make_unique<GenericMockService>(
      [](grpc::ServerReaderWriter<
          ::envoy::service::ext_proc::v3::ProcessingResponse,
          ::envoy::service::ext_proc::v3::ProcessingRequest>* stream) {
        ::envoy::service::ext_proc::v3::ProcessingRequest request;
        if (stream->Read(&request)) {
          if (request.has_request_headers()) {
            ::envoy::service::ext_proc::v3::ProcessingResponse response;
            SetDefaultEmptyResponse(request, &response);
            stream->Write(response);
            return grpc::Status::OK;
          }
        }
        return grpc::Status::OK;
      });
  StartAlternativeServer(std::move(mock_service));
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
          .SetRequestBodyMode(ProcessingMode::GRPC)
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
  request.set_message(kMessage1);
  EXPECT_TRUE(stream->Write(request));
  EXPECT_TRUE(stream->Read(&response));
  EXPECT_EQ(response.message(), kMessage1);
  stream->WritesDone();
  Status status = stream->Finish();
  EXPECT_TRUE(status.ok()) << status.error_message();
}

TEST_P(XdsExtProcEnd2endTest,
       StreamCleanCloseRequestBodyWithInFlightObservability) {
  auto mock_service = std::make_unique<GenericMockService>(
      [](grpc::ServerReaderWriter<
          ::envoy::service::ext_proc::v3::ProcessingResponse,
          ::envoy::service::ext_proc::v3::ProcessingRequest>* stream) {
        ::envoy::service::ext_proc::v3::ProcessingRequest request;
        while (stream->Read(&request)) {
          ::envoy::service::ext_proc::v3::ProcessingResponse response;
          if (request.has_request_body()) {
            auto* body_mutation = response.mutable_request_body()
                                      ->mutable_response()
                                      ->mutable_body_mutation();
            body_mutation->mutable_streamed_response()->set_body(
                request.request_body().body());
            stream->Write(response);
            return grpc::Status::OK;
          }
          SetDefaultEmptyResponse(request, &response);
          stream->Write(response);
        }
        return grpc::Status::OK;
      });
  StartAlternativeServer(std::move(mock_service));
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
          .SetRequestBodyMode(ProcessingMode::GRPC)
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
  request.set_message(kMessage1);
  EXPECT_TRUE(stream->Write(request));
  EXPECT_TRUE(stream->Read(&response));
  EXPECT_EQ(response.message(), kMessage1);
  // Stream closed. In observability mode, we can still send more messages.
  request.set_message(kMessage2);
  EXPECT_TRUE(stream->Write(request));
  EXPECT_TRUE(stream->Read(&response));
  EXPECT_EQ(response.message(), kMessage2);
  stream->WritesDone();
  Status status = stream->Finish();
  EXPECT_TRUE(status.ok()) << status.error_message();
}

TEST_P(XdsExtProcEnd2endTest, StreamCleanCloseRequestHeadersObservability) {
  auto mock_service = std::make_unique<GenericMockService>(
      [](grpc::ServerReaderWriter<
          ::envoy::service::ext_proc::v3::ProcessingResponse,
          ::envoy::service::ext_proc::v3::ProcessingRequest>* /*stream*/) {
        return grpc::Status::OK;
      });
  StartAlternativeServer(std::move(mock_service));
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
  request.set_message(kMessage1);
  EXPECT_TRUE(stream->Write(request));
  EXPECT_TRUE(stream->Read(&response));
  EXPECT_EQ(response.message(), kMessage1);
  stream->WritesDone();
  Status status = stream->Finish();
  EXPECT_TRUE(status.ok()) << status.error_message();
}

TEST_P(XdsExtProcEnd2endTest, StreamCleanCloseResponseBodyObservability) {
  auto mock_service = std::make_unique<GenericMockService>(
      [](grpc::ServerReaderWriter<
          ::envoy::service::ext_proc::v3::ProcessingResponse,
          ::envoy::service::ext_proc::v3::ProcessingRequest>* stream) {
        ::envoy::service::ext_proc::v3::ProcessingRequest request;
        while (stream->Read(&request)) {
          ::envoy::service::ext_proc::v3::ProcessingResponse response;
          SetDefaultEmptyResponse(request, &response);
          stream->Write(response);
          if (request.has_response_headers()) {
            return grpc::Status::OK;
          }
        }
        return grpc::Status::OK;
      });
  StartAlternativeServer(std::move(mock_service));
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
  ClientContext context;
  auto stream = stub_->BidiStream(&context);
  EchoRequest request;
  EchoResponse response;
  request.set_message(kMessage1);
  EXPECT_TRUE(stream->Write(request));
  EXPECT_TRUE(stream->Read(&response));
  EXPECT_EQ(response.message(), kMessage1);
  stream->WritesDone();
  Status status = stream->Finish();
  EXPECT_TRUE(status.ok()) << status.error_message();
}

TEST_P(XdsExtProcEnd2endTest,
       StreamCleanCloseResponseBodyWithInFlightObservability) {
  auto mock_service = std::make_unique<GenericMockService>(
      [](grpc::ServerReaderWriter<
          ::envoy::service::ext_proc::v3::ProcessingResponse,
          ::envoy::service::ext_proc::v3::ProcessingRequest>* stream) {
        ::envoy::service::ext_proc::v3::ProcessingRequest request;
        int response_body_count = 0;
        while (stream->Read(&request)) {
          if (request.has_response_body()) {
            response_body_count++;
            if (response_body_count == 2) {
              return grpc::Status::OK;
            }
          }
          ::envoy::service::ext_proc::v3::ProcessingResponse response;
          SetDefaultEmptyResponse(request, &response);
          stream->Write(response);
        }
        return grpc::Status::OK;
      });
  StartAlternativeServer(std::move(mock_service));
  CreateAndStartBackends(1);
  ResetStubWithUniqueArg();
  using envoy::extensions::filters::http::ext_proc::v3::ProcessingMode;
  auto ext_proc_config =
      ExternalProcessorBuilder()
          .SetTargetUri(alternative_ext_proc_server_->target())
          .SetInsecureChannelCredentials()
          .SetObservabilityMode(true)
          .SetFailureModeAllow(false)
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
  // Send message1.
  request.set_message(kMessage1);
  EXPECT_TRUE(stream->Write(request));
  EXPECT_TRUE(stream->Read(&response));
  EXPECT_EQ(response.message(), kMessage1);
  // Send message2. It will be sent to ext_proc, and stream will close
  // before ext_proc responds. In observability mode, this should not fail the
  // RPC. The message should be forwarded.
  request.set_message(kMessage2);
  EXPECT_TRUE(stream->Write(request));
  EXPECT_TRUE(stream->Read(&response));
  EXPECT_EQ(response.message(), kMessage2);
  // Send message3. Stream is closed, should be bypassed.
  request.set_message("message3");
  EXPECT_TRUE(stream->Write(request));
  EXPECT_TRUE(stream->Read(&response));
  EXPECT_EQ(response.message(), "message3");
  stream->WritesDone();
  Status status = stream->Finish();
  EXPECT_TRUE(status.ok()) << status.error_message();
}

TEST_P(XdsExtProcEnd2endTest, StreamCleanCloseResponseHeadersObservability) {
  auto mock_service = std::make_unique<GenericMockService>(
      [](grpc::ServerReaderWriter<
          ::envoy::service::ext_proc::v3::ProcessingResponse,
          ::envoy::service::ext_proc::v3::ProcessingRequest>* stream) {
        ::envoy::service::ext_proc::v3::ProcessingRequest request;
        if (stream->Read(&request)) {
          if (request.has_request_headers()) {
            ::envoy::service::ext_proc::v3::ProcessingResponse response;
            SetDefaultEmptyResponse(request, &response);
            stream->Write(response);
            return grpc::Status::OK;
          }
        }
        return grpc::Status::OK;
      });
  StartAlternativeServer(std::move(mock_service));
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
  ClientContext context;
  auto stream = stub_->BidiStream(&context);
  EchoRequest request;
  EchoResponse response;
  request.set_message(kMessage1);
  EXPECT_TRUE(stream->Write(request));
  EXPECT_TRUE(stream->Read(&response));
  EXPECT_EQ(response.message(), kMessage1);
  stream->WritesDone();
  Status status = stream->Finish();
  EXPECT_TRUE(status.ok()) << status.error_message();
}

TEST_P(XdsExtProcEnd2endTest, StreamCleanCloseResponseTrailersObservability) {
  auto mock_service = std::make_unique<GenericMockService>(
      [](grpc::ServerReaderWriter<
          ::envoy::service::ext_proc::v3::ProcessingResponse,
          ::envoy::service::ext_proc::v3::ProcessingRequest>* stream) {
        return grpc::Status::OK;
      });
  StartAlternativeServer(std::move(mock_service));
  CreateAndStartBackends(1);
  ResetStubWithUniqueArg();
  using envoy::extensions::filters::http::ext_proc::v3::ProcessingMode;
  auto ext_proc_config =
      ExternalProcessorBuilder()
          .SetTargetUri(alternative_ext_proc_server_->target())
          .SetInsecureChannelCredentials()
          .SetFailureModeAllow(false)
          .SetObservabilityMode(true)
          .SetResponseTrailerMode(ProcessingMode::SEND)
          .Build();
  Listener listener = BuildListenerWithExtProcFilter(ext_proc_config);
  RouteConfiguration route_config = default_route_config_;
  SetListenerAndRouteConfiguration(balancer_.get(), listener, route_config);
  balancer_->ads_service()->SetCdsResource(default_cluster_);
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(EdsResourceArgs({
      {"locality0", CreateEndpointsForBackends(0, 1)},
  })));
  CheckRpcSendOk(DEBUG_LOCATION);
}

//
// ExtProcClientMetrics
//

TEST_P(XdsExtProcEnd2endTest, ExtProcClientHeadersDurationMetric) {
  // Register a fake stats plugin to capture optional/disabled-by-default
  // metrics.
  auto stats_plugin = grpc_core::FakeStatsPluginBuilder()
                          .UseDisabledByDefaultMetrics(true)
                          .BuildAndRegister();
  ResetStubWithUniqueArg();
  CreateAndStartBackends(1);
  using envoy::extensions::filters::http::ext_proc::v3::ProcessingMode;
  // Enable request headers processing mode in ext_proc config.
  auto ext_proc_config = ExternalProcessorBuilder()
                             .SetTargetUri(ext_proc_server_->target())
                             .SetInsecureChannelCredentials()
                             .SetFailureModeAllow(false)
                             .SetRequestHeaderMode(ProcessingMode::SEND)
                             .Build();
  Listener listener = BuildListenerWithExtProcFilter(ext_proc_config);
  RouteConfiguration route_config = DefaultRouteConfig();
  SetListenerAndRouteConfiguration(balancer_.get(), listener, route_config);
  balancer_->ads_service()->SetCdsResource(DefaultCluster());
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(EdsResourceArgs({
      {"locality0", CreateEndpointsForBackends(0, 1)},
  })));
  RpcOptions rpc_options;
  rpc_options.set_echo_metadata_initially(true);
  EchoResponse response;
  Status status = SendRpcGetTrailers(rpc_options, &response, nullptr, nullptr);
  EXPECT_TRUE(status.ok()) << status.error_message();
  // Wait for ext_proc server to receive request headers processing request.
  MockExternalProcessorService::RequestCounts expected_counts;
  expected_counts.request_headers = 1;
  ext_proc_server_->ext_proc_service()->WaitForRequestCounts(expected_counts);
  // Retrieve the client headers duration histogram instrument and verify metric
  // recording.
  const std::string expected_target = absl::StrCat("xds:", kServerName);
  auto handle = grpc_core::GlobalInstrumentsRegistryTestPeer::
                    FindDoubleHistogramHandleByName(
                        "grpc.client_ext_proc.client_headers_duration")
                        .value();
  auto get_histogram =
      [&](grpc_core::GlobalInstrumentsRegistry::GlobalInstrumentHandle handle) {
        auto deadline =
            absl::Now() + absl::Seconds(10) * grpc_test_slowdown_factor();
        while (absl::Now() < deadline) {
          auto val = stats_plugin->GetDoubleHistogramValue(
              handle, {expected_target}, {});
          if (val.has_value()) return val;
          absl::SleepFor(absl::Milliseconds(20));
        }
        return stats_plugin->GetDoubleHistogramValue(handle, {expected_target},
                                                     {});
      };
  EXPECT_TRUE(get_histogram(handle).has_value());
}

TEST_P(XdsExtProcEnd2endTest, ExtProcClientHalfCloseDurationMetric) {
  // Register a fake stats plugin to capture optional/disabled-by-default
  // metrics.
  auto stats_plugin = grpc_core::FakeStatsPluginBuilder()
                          .UseDisabledByDefaultMetrics(true)
                          .BuildAndRegister();
  ResetStubWithUniqueArg();
  CreateAndStartBackends(1);
  using envoy::extensions::filters::http::ext_proc::v3::ProcessingMode;
  // Enable full processing mode including request body and response handling.
  auto ext_proc_config = ExternalProcessorBuilder()
                             .SetTargetUri(ext_proc_server_->target())
                             .SetInsecureChannelCredentials()
                             .SetFailureModeAllow(false)
                             .SetRequestHeaderMode(ProcessingMode::SEND)
                             .SetResponseHeaderMode(ProcessingMode::SEND)
                             .SetResponseTrailerMode(ProcessingMode::SEND)
                             .SetRequestBodyMode(ProcessingMode::GRPC)
                             .SetResponseBodyMode(ProcessingMode::GRPC)
                             .Build();
  Listener listener = BuildListenerWithExtProcFilter(ext_proc_config);
  RouteConfiguration route_config = DefaultRouteConfig();
  SetListenerAndRouteConfiguration(balancer_.get(), listener, route_config);
  balancer_->ads_service()->SetCdsResource(DefaultCluster());
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(EdsResourceArgs({
      {"locality0", CreateEndpointsForBackends(0, 1)},
  })));
  RpcOptions rpc_options;
  rpc_options.set_echo_metadata_initially(true);
  rpc_options.set_echo_metadata(true);
  EchoResponse response;
  Status status = SendRpcGetTrailers(rpc_options, &response, nullptr, nullptr);
  EXPECT_TRUE(status.ok()) << status.error_message();
  // Wait for all ext_proc stream messages to be processed.
  MockExternalProcessorService::RequestCounts expected_counts;
  expected_counts.request_headers = 1;
  expected_counts.response_headers = 1;
  expected_counts.response_trailers = 1;
  expected_counts.request_body = 1;
  expected_counts.response_body = 1;
  ext_proc_server_->ext_proc_service()->WaitForRequestCounts(expected_counts);
  // Retrieve the client half-close duration histogram instrument and verify
  // metric recording.
  const std::string expected_target = absl::StrCat("xds:", kServerName);
  auto handle = grpc_core::GlobalInstrumentsRegistryTestPeer::
                    FindDoubleHistogramHandleByName(
                        "grpc.client_ext_proc.client_half_close_duration")
                        .value();
  auto get_histogram =
      [&](grpc_core::GlobalInstrumentsRegistry::GlobalInstrumentHandle handle) {
        auto deadline =
            absl::Now() + absl::Seconds(10) * grpc_test_slowdown_factor();
        while (absl::Now() < deadline) {
          auto val = stats_plugin->GetDoubleHistogramValue(
              handle, {expected_target}, {});
          if (val.has_value()) return val;
          absl::SleepFor(absl::Milliseconds(20));
        }
        return stats_plugin->GetDoubleHistogramValue(handle, {expected_target},
                                                     {});
      };
  EXPECT_TRUE(get_histogram(handle).has_value());
}

TEST_P(XdsExtProcEnd2endTest, ExtProcServerHeadersDurationMetric) {
  // Register a fake stats plugin to capture optional/disabled-by-default
  // metrics.
  auto stats_plugin = grpc_core::FakeStatsPluginBuilder()
                          .UseDisabledByDefaultMetrics(true)
                          .BuildAndRegister();
  ResetStubWithUniqueArg();
  CreateAndStartBackends(1);
  using envoy::extensions::filters::http::ext_proc::v3::ProcessingMode;
  // Enable response headers processing mode in ext_proc config.
  auto ext_proc_config = ExternalProcessorBuilder()
                             .SetTargetUri(ext_proc_server_->target())
                             .SetInsecureChannelCredentials()
                             .SetFailureModeAllow(false)
                             .SetResponseHeaderMode(ProcessingMode::SEND)
                             .Build();
  Listener listener = BuildListenerWithExtProcFilter(ext_proc_config);
  RouteConfiguration route_config = DefaultRouteConfig();
  SetListenerAndRouteConfiguration(balancer_.get(), listener, route_config);
  balancer_->ads_service()->SetCdsResource(DefaultCluster());
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(EdsResourceArgs({
      {"locality0", CreateEndpointsForBackends(0, 1)},
  })));
  RpcOptions rpc_options;
  rpc_options.set_echo_metadata_initially(true);
  EchoResponse response;
  Status status = SendRpcGetTrailers(rpc_options, &response, nullptr, nullptr);
  EXPECT_TRUE(status.ok()) << status.error_message();
  // Wait for ext_proc server to receive response headers processing request.
  MockExternalProcessorService::RequestCounts expected_counts;
  expected_counts.response_headers = 1;
  ext_proc_server_->ext_proc_service()->WaitForRequestCounts(expected_counts);
  // Retrieve the server headers duration histogram instrument and verify metric
  // recording.
  const std::string expected_target = absl::StrCat("xds:", kServerName);
  auto handle = grpc_core::GlobalInstrumentsRegistryTestPeer::
                    FindDoubleHistogramHandleByName(
                        "grpc.client_ext_proc.server_headers_duration")
                        .value();
  auto get_histogram =
      [&](grpc_core::GlobalInstrumentsRegistry::GlobalInstrumentHandle handle) {
        auto deadline =
            absl::Now() + absl::Seconds(10) * grpc_test_slowdown_factor();
        while (absl::Now() < deadline) {
          auto val = stats_plugin->GetDoubleHistogramValue(
              handle, {expected_target}, {});
          if (val.has_value()) return val;
          absl::SleepFor(absl::Milliseconds(20));
        }
        return stats_plugin->GetDoubleHistogramValue(handle, {expected_target},
                                                     {});
      };
  EXPECT_TRUE(get_histogram(handle).has_value());
}

TEST_P(XdsExtProcEnd2endTest, ExtProcServerTrailersDurationMetric) {
  // Register a fake stats plugin to capture optional/disabled-by-default
  // metrics.
  auto stats_plugin = grpc_core::FakeStatsPluginBuilder()
                          .UseDisabledByDefaultMetrics(true)
                          .BuildAndRegister();
  ResetStubWithUniqueArg();
  CreateAndStartBackends(1);
  using envoy::extensions::filters::http::ext_proc::v3::ProcessingMode;
  // Enable response trailers processing mode in ext_proc config.
  auto ext_proc_config = ExternalProcessorBuilder()
                             .SetTargetUri(ext_proc_server_->target())
                             .SetInsecureChannelCredentials()
                             .SetFailureModeAllow(false)
                             .SetResponseTrailerMode(ProcessingMode::SEND)
                             .Build();
  Listener listener = BuildListenerWithExtProcFilter(ext_proc_config);
  RouteConfiguration route_config = DefaultRouteConfig();
  SetListenerAndRouteConfiguration(balancer_.get(), listener, route_config);
  balancer_->ads_service()->SetCdsResource(DefaultCluster());
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(EdsResourceArgs({
      {"locality0", CreateEndpointsForBackends(0, 1)},
  })));
  RpcOptions rpc_options;
  rpc_options.set_echo_metadata(true);
  EchoResponse response;
  Status status = SendRpcGetTrailers(rpc_options, &response, nullptr, nullptr);
  EXPECT_TRUE(status.ok()) << status.error_message();
  // Wait for ext_proc server to receive response trailers processing request.
  MockExternalProcessorService::RequestCounts expected_counts;
  expected_counts.response_trailers = 1;
  ext_proc_server_->ext_proc_service()->WaitForRequestCounts(expected_counts);
  // Retrieve the server trailers duration histogram instrument and verify
  // metric recording.
  const std::string expected_target = absl::StrCat("xds:", kServerName);
  auto handle = grpc_core::GlobalInstrumentsRegistryTestPeer::
                    FindDoubleHistogramHandleByName(
                        "grpc.client_ext_proc.server_trailers_duration")
                        .value();
  auto get_histogram =
      [&](grpc_core::GlobalInstrumentsRegistry::GlobalInstrumentHandle handle) {
        auto deadline =
            absl::Now() + absl::Seconds(10) * grpc_test_slowdown_factor();
        while (absl::Now() < deadline) {
          auto val = stats_plugin->GetDoubleHistogramValue(
              handle, {expected_target}, {});
          if (val.has_value()) return val;
          absl::SleepFor(absl::Milliseconds(20));
        }
        return stats_plugin->GetDoubleHistogramValue(handle, {expected_target},
                                                     {});
      };
  EXPECT_TRUE(get_histogram(handle).has_value());
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
  grpc_core::ForceEnableExperiment("recv_message_filter_bypass_fix", true);
  grpc_init();
  const auto result = RUN_ALL_TESTS();
  grpc_shutdown();
  return result;
}
