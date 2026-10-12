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

#include <map>
#include <memory>
#include <optional>
#include <queue>
#include <string>
#include <utility>
#include <vector>

#include "envoy/extensions/filters/http/ext_proc/v3/ext_proc.pb.h"
#include "envoy/extensions/filters/network/http_connection_manager/v3/http_connection_manager.pb.h"
#include "envoy/extensions/grpc_service/channel_credentials/insecure/v3/insecure_credentials.pb.h"
#include "envoy/service/ext_proc/v3/external_processor.grpc.pb.h"
#include "src/core/config/config_vars.h"
#include "src/core/lib/experiments/config.h"
#include "src/core/util/orphanable.h"
#include "src/core/util/sync.h"
#include "test/core/test_util/scoped_env_var.h"
#include "test/core/test_util/test_config.h"
#include "test/cpp/end2end/xds/xds_end2end_test_lib.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/log/check.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"

namespace grpc {
namespace testing {
namespace {

using ::envoy::extensions::filters::http::ext_proc::v3::ExternalProcessor;
using ::envoy::extensions::filters::http::ext_proc::v3::ExtProcPerRoute;
using ::envoy::extensions::filters::network::http_connection_manager::v3::
    HttpFilter;
using ::envoy::service::ext_proc::v3::ProcessingRequest;
using ExtProcService = ::envoy::service::ext_proc::v3::ExternalProcessor;

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
constexpr bool kEndOfStream = true;
constexpr char kEmptyBody[] = "";

// A stream-based fake external processor service that provides fine-grained,
// sequential control over incoming ext_proc stream requests and outgoing
// responses/statuses for test assertions.
class FakeExtProcService final : public ExtProcService::CallbackService {
 public:
  // Represents a single bidirectional stream between the client ext_proc filter
  // and this service, implemented as a ServerBidiReactor.
  class Stream final : public grpc::ServerBidiReactor<
                           ::envoy::service::ext_proc::v3::ProcessingRequest,
                           ::envoy::service::ext_proc::v3::ProcessingResponse>,
                       public grpc_core::InternallyRefCounted<Stream> {
   public:
    // Starts with two refs: one held by the owning OrphanablePtr (i.e., the
    // test, once it calls GetStream()), and one held by the gRPC server, which
    // is released in OnDone().
    Stream()
        : grpc_core::InternallyRefCounted<Stream>(/*trace=*/nullptr,
                                                  /*initial_refcount=*/2) {
      grpc_core::MutexLock lock(mu_);
      StartRead(&request_);
    }

    // Called when the owner (the test) releases the stream.  Cancels the
    // stream if it has not already been finished.
    void Orphan() override {
      MaybeFinish(grpc::Status::CANCELLED);
      Unref();
    }

    // Returns the next request received from the client, or std::nullopt
    // if stream finished or if the timeout elapses without receiving another
    // request.
    std::optional<::envoy::service::ext_proc::v3::ProcessingRequest>
    GetNextRequest(absl::Duration timeout = absl::Seconds(10)) {
      grpc_core::MutexLock lock(mu_);
      const absl::Time deadline =
          absl::Now() + timeout * grpc_test_slowdown_factor();
      while (requests_.empty() && !is_done_) {
        if (cv_.WaitWithDeadline(&mu_, deadline)) {
          return std::nullopt;
        }
      }
      if (requests_.empty()) {
        return std::nullopt;
      }
      auto req = std::move(requests_.front());
      requests_.pop();
      return req;
    }

    // Sends a response on the stream.
    void SendResponse(
        ::envoy::service::ext_proc::v3::ProcessingResponse response) {
      grpc_core::MutexLock lock(mu_);
      response_ = std::move(response);
      write_in_flight_ = true;
      StartWrite(&response_);
      while (write_in_flight_ && !is_done_) {
        cv_.Wait(&mu_);
      }
    }

    // Closes the stream with the specified status.
    void SendStatus(const grpc::Status& status) {
      grpc_core::MutexLock lock(mu_);
      MaybeFinishLocked(status);
      while (!is_done_) {
        cv_.Wait(&mu_);
      }
    }

    void MaybeFinish(const grpc::Status& status) {
      grpc_core::MutexLock lock(mu_);
      MaybeFinishLocked(status);
    }

   private:
    void MaybeFinishLocked(const grpc::Status& status)
        ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu_) {
      if (!called_finish_) {
        called_finish_ = true;
        Finish(status);
      }
    }

    void OnReadDone(bool ok) override {
      grpc_core::MutexLock lock(mu_);
      if (ok) {
        requests_.push(std::move(request_));
        cv_.SignalAll();
        StartRead(&request_);
      } else {
        MaybeFinishLocked(grpc::Status::OK);
      }
    }

    void OnWriteDone(bool /*ok*/) override {
      grpc_core::MutexLock lock(mu_);
      write_in_flight_ = false;
      cv_.SignalAll();
    }

    void OnCancel() override { MaybeFinish(grpc::Status::CANCELLED); }

    void OnDone() override {
      {
        grpc_core::MutexLock lock(mu_);
        is_done_ = true;
        cv_.SignalAll();
      }
      Unref();
    }

    grpc_core::Mutex mu_;
    grpc_core::CondVar cv_;
    std::queue<::envoy::service::ext_proc::v3::ProcessingRequest> requests_
        ABSL_GUARDED_BY(mu_);
    ::envoy::service::ext_proc::v3::ProcessingRequest request_
        ABSL_GUARDED_BY(mu_);
    ::envoy::service::ext_proc::v3::ProcessingResponse response_
        ABSL_GUARDED_BY(mu_);
    bool write_in_flight_ ABSL_GUARDED_BY(mu_) = false;
    bool is_done_ ABSL_GUARDED_BY(mu_) = false;
    bool called_finish_ ABSL_GUARDED_BY(mu_) = false;
  };

  // Returns the next incoming stream, or nullptr if no stream starts within
  // timeout or service is shutdown.  The caller takes ownership of the
  // stream; the stream is cancelled when the returned pointer is destroyed.
  grpc_core::OrphanablePtr<Stream> GetStream(
      absl::Duration timeout = absl::Seconds(10)) {
    grpc_core::MutexLock lock(mu_);
    const absl::Time deadline =
        absl::Now() + timeout * grpc_test_slowdown_factor();
    while (streams_.empty() && !is_shutdown_) {
      if (cv_.WaitWithDeadline(&mu_, deadline)) {
        return nullptr;
      }
    }
    if (streams_.empty()) {
      return nullptr;
    }
    auto stream = std::move(streams_.front());
    streams_.pop();
    return stream;
  }

  void Shutdown() {
    // Cancels any streams that were never consumed via GetStream().  Streams
    // already handed to the test are owned by the test.
    std::queue<grpc_core::OrphanablePtr<Stream>> streams;
    grpc_core::MutexLock lock(mu_);
    is_shutdown_ = true;
    streams = std::move(streams_);
    cv_.SignalAll();
  }

  Stream* Process(grpc::CallbackServerContext* /*context*/) override {
    auto stream = grpc_core::MakeOrphanable<Stream>();
    Stream* active_stream = stream.get();
    grpc_core::MutexLock lock(mu_);
    if (is_shutdown_) {
      stream->MaybeFinish(
          grpc::Status(grpc::StatusCode::UNAVAILABLE, "Server shutdown"));
      return active_stream;
    }
    streams_.push(std::move(stream));
    cv_.SignalAll();
    return active_stream;
  }

 private:
  grpc_core::Mutex mu_;
  grpc_core::CondVar cv_;
  // FIFO queue of newly arrived streams waiting to be consumed by the test
  // thread via GetStream(). Once popped by GetStream(), the stream is owned by
  // the test.
  std::queue<grpc_core::OrphanablePtr<Stream>> streams_ ABSL_GUARDED_BY(mu_);
  bool is_shutdown_ ABSL_GUARDED_BY(mu_) = false;
};

//
// Test fixture
//

class XdsExtProcEnd2endTest : public XdsEnd2endTest {
 public:
  class ExtProcFilterConfigBuilder {
   public:
    ExtProcFilterConfigBuilder() {
      auto* google_grpc =
          ext_proc_.mutable_grpc_service()->mutable_google_grpc();
      google_grpc->add_channel_credentials_plugin()->PackFrom(
          envoy::extensions::grpc_service::channel_credentials::insecure::v3::
              InsecureCredentials());
    }

    ExtProcFilterConfigBuilder& SetTargetUri(const std::string& target_uri) {
      auto* google_grpc =
          ext_proc_.mutable_grpc_service()->mutable_google_grpc();
      google_grpc->set_target_uri(target_uri);
      return *this;
    }

    ExtProcFilterConfigBuilder& SetFailureModeAllow(bool allow) {
      ext_proc_.set_failure_mode_allow(allow);
      return *this;
    }

    ExtProcFilterConfigBuilder& SetRequestHeaderMode(bool send) {
      ext_proc_.mutable_processing_mode()->set_request_header_mode(
          send ? envoy::extensions::filters::http::ext_proc::v3::
                     ProcessingMode::SEND
               : envoy::extensions::filters::http::ext_proc::v3::
                     ProcessingMode::SKIP);
      return *this;
    }

    ExtProcFilterConfigBuilder& SetResponseHeaderMode(bool send) {
      ext_proc_.mutable_processing_mode()->set_response_header_mode(
          send ? envoy::extensions::filters::http::ext_proc::v3::
                     ProcessingMode::SEND
               : envoy::extensions::filters::http::ext_proc::v3::
                     ProcessingMode::SKIP);
      return *this;
    }

    ExtProcFilterConfigBuilder& SetRequestBodyMode(bool send) {
      ext_proc_.mutable_processing_mode()->set_request_body_mode(
          send ? envoy::extensions::filters::http::ext_proc::v3::
                     ProcessingMode::GRPC
               : envoy::extensions::filters::http::ext_proc::v3::
                     ProcessingMode::NONE);
      return *this;
    }

    ExtProcFilterConfigBuilder& SetResponseBodyMode(bool send) {
      ext_proc_.mutable_processing_mode()->set_response_body_mode(
          send ? envoy::extensions::filters::http::ext_proc::v3::
                     ProcessingMode::GRPC
               : envoy::extensions::filters::http::ext_proc::v3::
                     ProcessingMode::NONE);
      return *this;
    }

    ExtProcFilterConfigBuilder& SetResponseTrailerMode(bool send) {
      ext_proc_.mutable_processing_mode()->set_response_trailer_mode(
          send ? envoy::extensions::filters::http::ext_proc::v3::
                     ProcessingMode::SEND
               : envoy::extensions::filters::http::ext_proc::v3::
                     ProcessingMode::SKIP);
      return *this;
    }

    ExtProcFilterConfigBuilder& SetDisableImmediateResponse(bool disable) {
      ext_proc_.set_disable_immediate_response(disable);
      return *this;
    }

    ExtProcFilterConfigBuilder& SetObservabilityMode(bool observability_mode) {
      ext_proc_.set_observability_mode(observability_mode);
      return *this;
    }

    envoy::extensions::filters::http::ext_proc::v3::ExternalProcessor Build() {
      return ext_proc_;
    }

   private:
    envoy::extensions::filters::http::ext_proc::v3::ExternalProcessor ext_proc_;
  };

  class ExtProcServerThread : public ServerThread {
   public:
    explicit ExtProcServerThread(XdsEnd2endTest* test_obj)
        : ServerThread(test_obj, /*use_xds_enabled_server=*/false,
                       grpc::InsecureServerCredentials()),
          service_(std::make_shared<FakeExtProcService>()) {}

    FakeExtProcService* ext_proc_service() { return service_.get(); }

   private:
    const char* Type() override { return "ExtProc"; }

    void RegisterAllServices(ServerBuilder* builder) override {
      builder->RegisterService(service_.get());
    }

    void StartAllServices() override {}
    void ShutdownAllServices() override { service_->Shutdown(); }

    std::shared_ptr<FakeExtProcService> service_;
  };

  // Helper for handling the client half-close event, which races with the
  // events on the response path and can therefore show up at any point once
  // the request body has been processed.
  class ClientHalfCloseHandler {
   public:
    explicit ClientHalfCloseHandler(FakeExtProcService::Stream* stream)
        : stream_(stream) {}

    void Handle(const ProcessingRequest& request);

    // Wrapper for GetNextRequest() that handles the client half-close event
    // if that's the event we got, in which case it returns the event after it.
    std::optional<ProcessingRequest> MaybeHandle(
        std::optional<ProcessingRequest> request);

    // Handles the half-close if it arrived after all of the response-path
    // events.  This must be called before waiting for the RPC to complete,
    // which cannot happen until we respond.
    void HandleIfNotYetSeen();

   private:
    FakeExtProcService::Stream* stream_;
    bool seen_ = false;
  };

  static std::multimap<std::string, std::string> HeaderMapToMultimap(
      const envoy::config::core::v3::HeaderMap& header_map) {
    std::multimap<std::string, std::string> map;
    for (const auto& header : header_map.headers()) {
      std::string val =
          !header.raw_value().empty() ? header.raw_value() : header.value();
      map.emplace(header.key(), std::move(val));
    }
    return map;
  }

  // Response construction helper functions

  static void PopulateHeaderMutation(
      ::envoy::service::ext_proc::v3::HeaderMutation* mutation,
      const std::vector<std::pair<std::string, std::string>>& set_headers) {
    for (const auto& [key, value] : set_headers) {
      auto* header = mutation->add_set_headers();
      header->mutable_header()->set_key(key);
      header->mutable_header()->set_value(value);
    }
  }

  static ::envoy::service::ext_proc::v3::ProcessingResponse
  MakeRequestHeadersMutationResponse(
      const std::vector<std::pair<std::string, std::string>>& set_headers) {
    ::envoy::service::ext_proc::v3::ProcessingResponse response;
    PopulateHeaderMutation(response.mutable_request_headers()
                               ->mutable_response()
                               ->mutable_header_mutation(),
                           set_headers);
    return response;
  }

  static ::envoy::service::ext_proc::v3::ProcessingResponse
  MakeResponseHeadersMutationResponse(
      const std::vector<std::pair<std::string, std::string>>& set_headers) {
    ::envoy::service::ext_proc::v3::ProcessingResponse response;
    PopulateHeaderMutation(response.mutable_response_headers()
                               ->mutable_response()
                               ->mutable_header_mutation(),
                           set_headers);
    return response;
  }

  static void PopulateBodyMutation(
      ::envoy::service::ext_proc::v3::BodyMutation* body_mutation,
      absl::string_view body, bool end_of_stream) {
    body_mutation->mutable_streamed_response()->set_body(std::string(body));
    body_mutation->mutable_streamed_response()->set_end_of_stream(
        end_of_stream);
  }

  static ::envoy::service::ext_proc::v3::ProcessingResponse
  MakeRequestBodyMutationResponse(absl::string_view body, bool end_of_stream) {
    ::envoy::service::ext_proc::v3::ProcessingResponse response;
    PopulateBodyMutation(response.mutable_request_body()
                             ->mutable_response()
                             ->mutable_body_mutation(),
                         body, end_of_stream);
    return response;
  }

  static ::envoy::service::ext_proc::v3::ProcessingResponse
  MakeResponseBodyMutationResponse(absl::string_view body, bool end_of_stream) {
    ::envoy::service::ext_proc::v3::ProcessingResponse response;
    PopulateBodyMutation(response.mutable_response_body()
                             ->mutable_response()
                             ->mutable_body_mutation(),
                         body, end_of_stream);
    return response;
  }

  static ::envoy::service::ext_proc::v3::ProcessingResponse
  MakeResponseTrailersMutationResponse(
      const std::vector<std::pair<std::string, std::string>>& set_headers) {
    ::envoy::service::ext_proc::v3::ProcessingResponse response;
    PopulateHeaderMutation(
        response.mutable_response_trailers()->mutable_header_mutation(),
        set_headers);
    return response;
  }

  static std::string ModifyEchoRequest(absl::string_view body,
                                       absl::string_view add_suffix) {
    EchoRequest echo_req;
    CHECK(echo_req.ParseFromString(body));
    echo_req.set_message(absl::StrCat(echo_req.message(), add_suffix));
    std::string mutated;
    CHECK(echo_req.SerializeToString(&mutated));
    return mutated;
  }

  static std::string ModifyEchoResponse(absl::string_view body,
                                        absl::string_view add_suffix) {
    EchoResponse echo_resp;
    CHECK(echo_resp.ParseFromString(body));
    echo_resp.set_message(absl::StrCat(echo_resp.message(), add_suffix));
    std::string mutated;
    CHECK(echo_resp.SerializeToString(&mutated));
    return mutated;
  }

  void SetUp() override {
    InitClient(MakeBootstrapBuilder().SetTrustedXdsServer(),
               /*lb_expected_authority=*/"",
               /*xds_resource_does_not_exist_timeout_ms=*/0,
               /*balancer_authority_override=*/"", /*args=*/nullptr);
    CreateAndStartBackends(1);
    balancer_->ads_service()->SetEdsResource(BuildEdsResource(EdsResourceArgs({
        {"locality0", CreateEndpointsForBackends(0, 1)},
    })));
    ext_proc_server_ = std::make_unique<ExtProcServerThread>(this);
    ext_proc_server_->Start();
  }

  void TearDown() override {
    ext_proc_server_->Shutdown();
    XdsEnd2endTest::TearDown();
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

  RouteConfiguration BuildRouteConfigurationWithExtProcFilter(
      const ExternalProcessor& ext_proc) {
    ExtProcPerRoute per_route;
    auto* overrides = per_route.mutable_overrides();
    if (ext_proc.has_processing_mode()) {
      *overrides->mutable_processing_mode() = ext_proc.processing_mode();
    }
    if (ext_proc.has_grpc_service()) {
      *overrides->mutable_grpc_service() = ext_proc.grpc_service();
    }
    overrides->mutable_failure_mode_allow()->set_value(
        ext_proc.failure_mode_allow());
    *overrides->mutable_request_attributes() = ext_proc.request_attributes();
    *overrides->mutable_response_attributes() = ext_proc.response_attributes();
    google::protobuf::Any filter_config;
    filter_config.PackFrom(per_route);
    RouteConfiguration new_route_config = default_route_config_;
    auto* config_map = new_route_config.mutable_virtual_hosts(0)
                           ->mutable_routes(0)
                           ->mutable_typed_per_filter_config();
    (*config_map)[std::string(kFilterInstanceName)] = std::move(filter_config);
    return new_route_config;
  }

  void SetFilterConfig(const ExternalProcessor& ext_proc) {
    switch (GetParam().filter_config_setup()) {
      case XdsTestType::HttpFilterConfigLocation::kHttpFilterConfigInRoute: {
        ExternalProcessor top_level_ext_proc =
            ExtProcFilterConfigBuilder()
                .SetTargetUri("invalid-target")
                .SetObservabilityMode(ext_proc.observability_mode())
                .SetDisableImmediateResponse(
                    ext_proc.disable_immediate_response())
                .Build();
        Listener listener = BuildListenerWithExtProcFilter(top_level_ext_proc);
        RouteConfiguration route =
            BuildRouteConfigurationWithExtProcFilter(ext_proc);
        SetListenerAndRouteConfiguration(balancer_.get(), listener, route);
        break;
      }
      case XdsTestType::HttpFilterConfigLocation::kHttpFilterConfigInListener: {
        Listener listener = BuildListenerWithExtProcFilter(ext_proc);
        SetListenerAndRouteConfiguration(balancer_.get(), listener,
                                         default_route_config_);
        break;
      }
    }
  }

  ExtProcFilterConfigBuilder MakeFilterConfigBuilder() {
    return ExtProcFilterConfigBuilder().SetTargetUri(
        ext_proc_server_->target());
  }

  FakeExtProcService& ext_proc_service() {
    return *ext_proc_server_->ext_proc_service();
  }

  grpc_core::testing::ScopedExperimentalEnvVar env_var_{
      "GRPC_EXPERIMENTAL_XDS_EXT_PROC_ON_CLIENT"};
  std::unique_ptr<ExtProcServerThread> ext_proc_server_;
};

INSTANTIATE_TEST_SUITE_P(
    XdsTest, XdsExtProcEnd2endTest,
    ::testing::Values(XdsTestType(),
                      XdsTestType().set_filter_config_setup(
                          XdsTestType::kHttpFilterConfigInRoute)),
    &XdsTestType::Name);

MATCHER(IsStatusOk, "") {
  if (!arg.ok()) {
    *result_listener << "code=" << arg.error_code()
                     << ", message=" << arg.error_message();
    return false;
  }
  return true;
}

MATCHER_P2(GrpcStatusIs, code, message_matcher, "") {
  *result_listener << "actual code=" << arg.error_code()
                   << ", actual message=\"" << arg.error_message() << "\"";
  return ::testing::ExplainMatchResult(code, arg.error_code(),
                                       result_listener) &&
         ::testing::ExplainMatchResult(message_matcher, arg.error_message(),
                                       result_listener);
}

MATCHER_P(MatchesRequestHeaders, headers_matcher,
          "matches request_headers with given headers") {
  if (!arg.has_request_headers()) {
    *result_listener << "request does not have request_headers";
    return false;
  }
  auto actual_headers = XdsExtProcEnd2endTest::HeaderMapToMultimap(
      arg.request_headers().headers());
  return ::testing::ExplainMatchResult(headers_matcher, actual_headers,
                                       result_listener);
}

MATCHER_P2(MatchesRequestBody, body_matcher, end_of_stream,
           "matches request_body with given body and end_of_stream") {
  if (!arg.has_request_body()) {
    *result_listener << "request does not have request_body";
    return false;
  }
  const auto& request_body = arg.request_body();
  if (request_body.end_of_stream() != end_of_stream) {
    *result_listener << "expected end_of_stream " << end_of_stream
                     << " but got " << request_body.end_of_stream();
    return false;
  }
  return ::testing::ExplainMatchResult(body_matcher, request_body.body(),
                                       result_listener);
}

MATCHER_P2(MatchesResponseHeaders, headers_matcher, end_of_stream,
           "matches response_headers with given headers and end_of_stream") {
  if (!arg.has_response_headers()) {
    *result_listener << "request does not have response_headers";
    return false;
  }
  const auto& response_headers = arg.response_headers();
  if (response_headers.end_of_stream() != end_of_stream) {
    *result_listener << "expected end_of_stream " << end_of_stream
                     << " but got " << response_headers.end_of_stream();
    return false;
  }
  auto actual_headers =
      XdsExtProcEnd2endTest::HeaderMapToMultimap(response_headers.headers());
  return ::testing::ExplainMatchResult(headers_matcher, actual_headers,
                                       result_listener);
}

MATCHER_P(MatchesResponseHeaders, headers_matcher,
          "matches response_headers with given headers") {
  return ::testing::ExplainMatchResult(
      MatchesResponseHeaders(headers_matcher, !kEndOfStream), arg,
      result_listener);
}

MATCHER_P2(MatchesResponseBody, body_matcher, end_of_stream,
           "matches response_body with given body and end_of_stream") {
  if (!arg.has_response_body()) {
    *result_listener << "request does not have response_body";
    return false;
  }
  const auto& response_body = arg.response_body();
  if (response_body.end_of_stream() != end_of_stream) {
    *result_listener << "expected end_of_stream " << end_of_stream
                     << " but got " << response_body.end_of_stream();
    return false;
  }
  return ::testing::ExplainMatchResult(body_matcher, response_body.body(),
                                       result_listener);
}

MATCHER_P(MatchesResponseTrailers, trailers_matcher,
          "matches response_trailers with given trailers") {
  if (!arg.has_response_trailers()) {
    *result_listener << "request does not have response_trailers";
    return false;
  }
  const auto& response_trailers = arg.response_trailers();
  std::multimap<std::string, std::string> actual_trailers =
      XdsExtProcEnd2endTest::HeaderMapToMultimap(response_trailers.trailers());
  return ::testing::ExplainMatchResult(trailers_matcher, actual_trailers,
                                       result_listener);
}

MATCHER_P(EchoRequestMessageIs, message_matcher,
          "matches serialized EchoRequest with given message") {
  EchoRequest echo_req;
  if (!echo_req.ParseFromString(arg)) {
    *result_listener << "could not be parsed as EchoRequest";
    return false;
  }
  return ::testing::ExplainMatchResult(message_matcher, echo_req.message(),
                                       result_listener);
}

MATCHER_P(EchoResponseMessageIs, message_matcher,
          "matches serialized EchoResponse with given message") {
  EchoResponse echo_resp;
  if (!echo_resp.ParseFromString(arg)) {
    *result_listener << "could not be parsed as EchoResponse";
    return false;
  }
  return ::testing::ExplainMatchResult(message_matcher, echo_resp.message(),
                                       result_listener);
}

//
// XdsExtProcEnd2endTest::ClientHalfCloseHandler
//
// These methods are defined here rather than inline in the class, because
// they use matchers defined above.
//

void XdsExtProcEnd2endTest::ClientHalfCloseHandler::Handle(
    const ProcessingRequest& request) {
  EXPECT_FALSE(seen_) << "duplicate client half-close event";
  seen_ = true;
  EXPECT_THAT(request, MatchesRequestBody(kEmptyBody, kEndOfStream));
  stream_->SendResponse(
      MakeRequestBodyMutationResponse(/*body=*/"", kEndOfStream));
}

std::optional<ProcessingRequest>
XdsExtProcEnd2endTest::ClientHalfCloseHandler::MaybeHandle(
    std::optional<ProcessingRequest> request) {
  if (!request.has_value() || !request->has_request_body()) return request;
  Handle(*request);
  return stream_->GetNextRequest();
}

void XdsExtProcEnd2endTest::ClientHalfCloseHandler::HandleIfNotYetSeen() {
  if (seen_) return;
  auto request = stream_->GetNextRequest();
  ASSERT_TRUE(request.has_value()) << "timed out waiting for client half-close";
  Handle(*request);
}

//
// Tests
//

TEST_P(XdsExtProcEnd2endTest, ProcessingModeAllEnabledSuccess) {
  auto ext_proc_config = MakeFilterConfigBuilder()
                             .SetRequestHeaderMode(true)
                             .SetResponseHeaderMode(true)
                             .SetResponseTrailerMode(true)
                             .SetRequestBodyMode(true)
                             .SetResponseBodyMode(true)
                             .Build();
  SetFilterConfig(ext_proc_config);
  RpcOptions rpc_options;
  rpc_options.set_echo_metadata_initially(true);
  AsyncRpc rpc;
  rpc.StartRpc(stub_.get(), rpc_options);
  auto ext_proc_stream = ext_proc_service().GetStream();
  ASSERT_NE(ext_proc_stream, nullptr);
  // ext_proc server sees request headers and sends them back.
  auto req = ext_proc_stream->GetNextRequest();
  ASSERT_THAT(
      req,
      ::testing::Optional(MatchesRequestHeaders(::testing::Contains(
          ::testing::Pair(":path", "/grpc.testing.EchoTestService/Echo")))));
  ext_proc_stream->SendResponse(MakeRequestHeadersMutationResponse(
      {{kRequestHeadersMutatedHeaderKey, kHeaderMutatedValue}}));
  // ext_proc server sees client message and sends it back.
  req = ext_proc_stream->GetNextRequest();
  ASSERT_THAT(req, ::testing::Optional(MatchesRequestBody(
                       EchoRequestMessageIs(kRequestMessage), !kEndOfStream)));
  ext_proc_stream->SendResponse(MakeRequestBodyMutationResponse(
      ModifyEchoRequest(req->request_body().body(), kRequestBodyMutatedSuffix),
      !kEndOfStream));
  ClientHalfCloseHandler half_close_handler(ext_proc_stream.get());
  // ext_proc server sees response headers and sends them back.
  req = half_close_handler.MaybeHandle(ext_proc_stream->GetNextRequest());
  ASSERT_THAT(req, ::testing::Optional(MatchesResponseHeaders(::testing::_)));
  ext_proc_stream->SendResponse(MakeResponseHeadersMutationResponse(
      {{kResponseHeadersMutatedHeaderKey, kHeaderMutatedValue}}));
  // ext_proc server sees response body and sends it back.
  req = half_close_handler.MaybeHandle(ext_proc_stream->GetNextRequest());
  ASSERT_THAT(req, ::testing::Optional(MatchesResponseBody(
                       EchoResponseMessageIs(absl::StrCat(
                           kRequestMessage, kRequestBodyMutatedSuffix)),
                       !kEndOfStream)));
  ext_proc_stream->SendResponse(MakeResponseBodyMutationResponse(
      ModifyEchoResponse(req->response_body().body(),
                         kResponseBodyMutatedSuffix),
      !kEndOfStream));
  // ext_proc server sees response trailers and sends them back.
  req = half_close_handler.MaybeHandle(ext_proc_stream->GetNextRequest());
  ASSERT_THAT(req, ::testing::Optional(MatchesResponseTrailers(::testing::_)));
  ext_proc_stream->SendResponse(MakeResponseTrailersMutationResponse(
      {{kResponseTrailersMutatedHeaderKey, kHeaderMutatedValue}}));
  half_close_handler.HandleIfNotYetSeen();
  Status status = rpc.GetStatus();
  EXPECT_THAT(status, IsStatusOk());
  EXPECT_THAT(rpc.GetServerInitialMetadata(),
              ::testing::AllOf(
                  ::testing::Contains(::testing::Pair(
                      kRequestHeadersMutatedHeaderKey, kHeaderMutatedValue)),
                  ::testing::Contains(::testing::Pair(
                      kResponseHeadersMutatedHeaderKey, kHeaderMutatedValue))));
  EXPECT_THAT(rpc.GetServerTrailingMetadata(),
              ::testing::Contains(::testing::Pair(
                  kResponseTrailersMutatedHeaderKey, kHeaderMutatedValue)));
  EXPECT_EQ(rpc.response().message(),
            absl::StrCat(kRequestMessage, kRequestBodyMutatedSuffix,
                         kResponseBodyMutatedSuffix));
}

// With failure_mode_allow=false, an ext_proc stream failure fails the RPC.
TEST_P(XdsExtProcEnd2endTest, StreamErrorFailureModeFalseFails) {
  auto ext_proc_config = MakeFilterConfigBuilder()
                             .SetFailureModeAllow(false)
                             .SetRequestHeaderMode(true)
                             .SetResponseHeaderMode(false)
                             .Build();
  SetFilterConfig(ext_proc_config);
  AsyncRpc rpc;
  rpc.StartRpc(stub_.get());
  auto ext_proc_stream = ext_proc_service().GetStream();
  ASSERT_NE(ext_proc_stream, nullptr);
  auto req = ext_proc_stream->GetNextRequest();
  ASSERT_THAT(
      req,
      ::testing::Optional(MatchesRequestHeaders(::testing::Contains(
          ::testing::Pair(":path", "/grpc.testing.EchoTestService/Echo")))));
  ext_proc_stream->SendStatus(
      grpc::Status(StatusCode::UNAVAILABLE,
                   "Call closed by ext_proc server on request headers"));
  Status status = rpc.GetStatus();
  EXPECT_THAT(status, GrpcStatusIs(
                          StatusCode::INTERNAL,
                          "External processor stream failed: UNAVAILABLE: Call "
                          "closed by ext_proc server on request headers"));
}

// With failure_mode_allow=true, an ext_proc stream failure is ignored and the
// RPC proceeds without ext_proc processing.
TEST_P(XdsExtProcEnd2endTest,
       StreamErrorFailureModeAllowBodiesNotConfiguredSuccess) {
  ResetStub();
  auto ext_proc_config = MakeFilterConfigBuilder()
                             .SetFailureModeAllow(true)
                             .SetRequestHeaderMode(true)
                             .SetResponseHeaderMode(true)
                             .Build();
  SetFilterConfig(ext_proc_config);
  AsyncRpc rpc;
  rpc.StartRpc(stub_.get());
  auto ext_proc_stream = ext_proc_service().GetStream();
  ASSERT_NE(ext_proc_stream, nullptr);
  auto req = ext_proc_stream->GetNextRequest();
  ASSERT_THAT(
      req,
      ::testing::Optional(MatchesRequestHeaders(::testing::Contains(
          ::testing::Pair(":path", "/grpc.testing.EchoTestService/Echo")))));
  ext_proc_stream->SendStatus(grpc::Status(
      StatusCode::UNAVAILABLE, "Call closed by ext_proc server on headers"));
  Status status = rpc.GetStatus();
  EXPECT_THAT(status, IsStatusOk());
  EXPECT_EQ(rpc.response().message(), kRequestMessage);
}

}  // namespace
}  // namespace testing
}  // namespace grpc

int main(int argc, char** argv) {
  grpc_core::ForceEnableExperiment("v2_non_owning_waker_implementation", true);
  grpc_core::ForceEnableExperiment("recv_message_filter_bypass_fix", true);
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
