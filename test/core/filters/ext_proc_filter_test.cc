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

// Behaviour tests for the ext_proc filter.
//
// ExtProcFilter runs in a v3 interception chain built by FilterTest. The test
// plays all three remote parties:
// - the client, via the implicit initiator (PushClient*/PullServer*);
// - the backend, via the implicit handler (PullClient*/PushServer*);
// - the ext_proc server, via a FakeXdsTransportFactory standing in for the
//   side-channel transport (the *SideStream* helpers below).
//
// xDS config parsing is covered by test/core/xds/xds_http_filters_test.cc,
// and end-to-end wiring by test/cpp/end2end/xds/xds_ext_proc_end2end_test.cc.

#include "src/core/filter/ext_proc/ext_proc_filter.h"

#include <grpc/status.h>
#include <grpc/support/metrics.h>
#include <stdint.h>

#include <initializer_list>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "envoy/config/core/v3/base.pb.h"
#include "envoy/service/ext_proc/v3/external_processor.pb.h"
#include "google/protobuf/struct.pb.h"
#include "src/core/call/metadata.h"
#include "src/core/client_channel/client_channel_args.h"
#include "src/core/filter/ext_proc/ext_proc_messages.h"
#include "src/core/filter/filter_args.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/event_engine/channel_args_endpoint_config.h"
#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/telemetry/metrics.h"
#include "src/core/util/ref_counted_ptr.h"
#include "src/core/util/time.h"
#include "src/core/xds/grpc/xds_server_grpc.h"
#include "src/core/xds/xds_client/xds_transport.h"
#include "test/core/filters/filter_matchers.h"
#include "test/core/filters/filter_test.h"
#include "test/core/test_util/fake_stats_plugin.h"
#include "test/core/xds/xds_transport_fake.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/functional/function_ref.h"
#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"

namespace grpc_core {
namespace {

using ::envoy::service::ext_proc::v3::ProcessingRequest;
using ::envoy::service::ext_proc::v3::ProcessingResponse;
using ::testing::_;
using ::testing::AllOf;
using ::testing::Contains;
using ::testing::Not;
using ::testing::Optional;
using ::testing::Pair;

constexpr char kExtProcMethod[] =
    "/envoy.service.ext_proc.v3.ExternalProcessor/Process";
// Target of the data plane channel; also the "target" label on metrics.
constexpr char kServerUri[] = "dns:///server.example.com";
constexpr char kPath[] = "/package.Service/Method";

constexpr char kRequestHeadersMutatedHeaderKey[] =
    "x-extproc-request-headers-mutated";
constexpr char kResponseHeadersMutatedHeaderKey[] =
    "x-extproc-response-headers-mutated";
constexpr char kResponseTrailersMutatedHeaderKey[] =
    "x-extproc-response-trailers-mutated";
constexpr char kHeaderMutatedValue[] = "yes";
constexpr char kImmediateResponseHeaderKey[] =
    "x-extproc-immediate-response-added";
constexpr char kCustomHeaderKey[] = "custom-header-key";
constexpr char kCustomHeaderValue[] = "custom-header-value";
constexpr char kMessage1[] = "message1";
constexpr char kMessage2[] = "message2";
constexpr char kMessage1Mutated[] = "message1-mutated";
constexpr char kMessage1DoubleMutated[] = "message1-mutated-mutated";
constexpr bool kEndOfStream = true;
constexpr char kEmptyBody[] = "";

//
// Test fixture
//

class ExtProcFilterTest : public FilterTest {
 public:
  // Flattens a HeaderMap from a ProcessingRequest. Public for use by the
  // matchers below.
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

 protected:
  using FilterTest::FilterTest;

  // Builds an ExtProcFilter::Config directly, bypassing xDS parsing.
  // Setters mirror the fields of the envoy ExternalProcessor proto. Unset
  // fields take the values the xDS parser would produce for an unset proto
  // field: in particular, the processing mode defaults to sending request and
  // response headers only.
  class ConfigBuilder {
   public:
    ConfigBuilder& SetFailureModeAllow(bool allow) {
      failure_mode_allow_ = allow;
      return *this;
    }
    ConfigBuilder& SetRequestHeaderMode(bool send) {
      processing_mode_.send_request_headers = send;
      return *this;
    }
    ConfigBuilder& SetResponseHeaderMode(bool send) {
      processing_mode_.send_response_headers = send;
      return *this;
    }
    ConfigBuilder& SetResponseTrailerMode(bool send) {
      processing_mode_.send_response_trailers = send;
      return *this;
    }
    ConfigBuilder& SetRequestBodyMode(bool send) {
      processing_mode_.send_request_body = send;
      return *this;
    }
    ConfigBuilder& SetResponseBodyMode(bool send) {
      processing_mode_.send_response_body = send;
      return *this;
    }
    // Sends every event (headers, bodies and trailers) to the ext_proc
    // server.
    ConfigBuilder& EnableAllProcessingModes() {
      return SetRequestHeaderMode(true)
          .SetResponseHeaderMode(true)
          .SetResponseTrailerMode(true)
          .SetRequestBodyMode(true)
          .SetResponseBodyMode(true);
    }
    ConfigBuilder& AddRequestAttribute(std::string attribute) {
      request_attributes_.push_back(std::move(attribute));
      return *this;
    }
    ConfigBuilder& SetDisableImmediateResponse(bool disable) {
      disable_immediate_response_ = disable;
      return *this;
    }
    ConfigBuilder& SetObservabilityMode(bool observability_mode) {
      observability_mode_ = observability_mode;
      return *this;
    }
    ConfigBuilder& SetDeferredCloseTimeout(Duration timeout) {
      deferred_close_timeout_ = timeout;
      return *this;
    }

    RefCountedPtr<ExtProcFilter::Config> Build(
        RefCountedPtr<ExtProcFilter::ExtProcChannel> channel) const {
      auto config = MakeRefCounted<ExtProcFilter::Config>();
      config->channel_info = std::move(channel);
      config->failure_mode_allow = failure_mode_allow_;
      config->processing_mode = processing_mode_;
      config->request_attributes = request_attributes_;
      config->disable_immediate_response = disable_immediate_response_;
      config->observability_mode = observability_mode_;
      config->deferred_close_timeout = deferred_close_timeout_;
      return config;
    }

   private:
    bool failure_mode_allow_ = false;
    ExtProcProcessingMode processing_mode_;
    std::vector<std::string> request_attributes_;
    bool disable_immediate_response_ = false;
    bool observability_mode_ = false;
    Duration deferred_close_timeout_ = Duration::Zero();
  };

  // Returns the value of `attribute_name` from the ext_proc attributes of
  // `request`, or "" if it is not set.
  static std::string GetExtProcAttribute(const ProcessingRequest& request,
                                         absl::string_view attribute_name) {
    auto it = request.attributes().find("envoy.filters.http.ext_proc");
    if (it == request.attributes().end()) return "";
    const auto& fields = it->second.fields();
    auto field_it = fields.find(std::string(attribute_name));
    if (field_it == fields.end()) return "";
    return field_it->second.string_value();
  }

  // ProcessingResponse construction

  static ProcessingResponse MakeRequestHeadersMutationResponse(
      const std::vector<std::pair<std::string, std::string>>& set_headers,
      bool request_drain = false) {
    ProcessingResponse response;
    if (request_drain) response.set_request_drain(true);
    PopulateHeaderMutation(response.mutable_request_headers()
                               ->mutable_response()
                               ->mutable_header_mutation(),
                           set_headers);
    return response;
  }

  static ProcessingResponse MakeResponseHeadersMutationResponse(
      const std::vector<std::pair<std::string, std::string>>& set_headers,
      bool request_drain = false) {
    ProcessingResponse response;
    if (request_drain) response.set_request_drain(true);
    PopulateHeaderMutation(response.mutable_response_headers()
                               ->mutable_response()
                               ->mutable_header_mutation(),
                           set_headers);
    return response;
  }

  static ProcessingResponse MakeResponseTrailersMutationResponse(
      const std::vector<std::pair<std::string, std::string>>& set_headers,
      bool request_drain = false) {
    ProcessingResponse response;
    if (request_drain) response.set_request_drain(true);
    PopulateHeaderMutation(
        response.mutable_response_trailers()->mutable_header_mutation(),
        set_headers);
    return response;
  }

  static ProcessingResponse MakeRequestBodyMutationResponse(
      absl::string_view body, bool end_of_stream = false,
      bool request_drain = false) {
    ProcessingResponse response;
    if (request_drain) response.set_request_drain(true);
    PopulateBodyMutation(response.mutable_request_body()
                             ->mutable_response()
                             ->mutable_body_mutation(),
                         body, end_of_stream);
    return response;
  }

  // The response to the request_body event the filter sends for the client's
  // half-close: passes the half-close through without adding a message.
  static ProcessingResponse MakeRequestHalfCloseResponse() {
    ProcessingResponse response;
    PopulateBodyMutation(response.mutable_request_body()
                             ->mutable_response()
                             ->mutable_body_mutation(),
                         /*body=*/"", /*end_of_stream=*/true,
                         /*end_of_stream_without_message=*/true);
    return response;
  }

  static ProcessingResponse MakeResponseBodyMutationResponse(
      absl::string_view body, bool end_of_stream = false,
      bool request_drain = false) {
    ProcessingResponse response;
    if (request_drain) response.set_request_drain(true);
    PopulateBodyMutation(response.mutable_response_body()
                             ->mutable_response()
                             ->mutable_body_mutation(),
                         body, end_of_stream);
    return response;
  }

  static ProcessingResponse MakeImmediateResponse(
      grpc_status_code code, absl::string_view details,
      const std::vector<std::pair<std::string, std::string>>& set_headers) {
    ProcessingResponse response;
    auto* immediate = response.mutable_immediate_response();
    immediate->mutable_grpc_status()->set_status(static_cast<uint32_t>(code));
    immediate->set_details(std::string(details));
    PopulateHeaderMutation(immediate->mutable_headers(), set_headers);
    return response;
  }

  // Builds the chain [ExtProcFilter] -> backend, with the filter's side
  // stream going to a fake transport.
  void Init(const ConfigBuilder& builder, ChannelArgs args = ChannelArgs()) {
    transport_factory_ = MakeRefCounted<FakeXdsTransportFactory>(
        [] {
          ADD_FAILURE() << "too many pending reads on ext_proc side stream";
        },
        event_engine());
    // A failed call may leave requests the test never reads.
    transport_factory_->SetAbortOnUndrainedMessages(false);
    absl::Status status;
    auto transport = transport_factory_->GetTransport(target_, &status);
    ASSERT_TRUE(status.ok()) << status;
    absl::Status chain_status = CreateFilterChain<ExtProcFilter>(
        args.Set(GRPC_ARG_USE_V3_STACK, true)
            .Set(GRPC_ARG_SERVER_URI, kServerUri),
        builder.Build(MakeRefCounted<ExtProcFilter::ExtProcChannel>(
            target_, std::move(transport))));
    ASSERT_TRUE(chain_status.ok()) << chain_status;
  }

  // As Init(), but also registers a stats plugin for the data plane channel
  // and returns it.
  std::shared_ptr<FakeStatsPlugin> InitWithStatsPlugin(
      const ConfigBuilder& builder) {
    auto stats_plugin = FakeStatsPluginBuilder()
                            .UseDisabledByDefaultMetrics(true)
                            .BuildAndRegister();
    stats_plugin_group_ = GlobalStatsPluginRegistry::GetStatsPluginsForChannel(
        experimental::StatsPluginChannelScope(
            kServerUri, /*default_authority=*/"",
            grpc_event_engine::experimental::ChannelArgsEndpointConfig(
                ChannelArgs())));
    Init(builder, ChannelArgs().SetObject(stats_plugin_group_));
    return stats_plugin;
  }

  // Starts the call under test. The filter creates its child call later, so
  // WaitForHandler() must be called before touching the backend.
  void StartRpc(
      std::initializer_list<std::pair<absl::string_view, absl::string_view>>
          client_initial_metadata = {}) {
    StartCallWithDeferredHandler(
        NewClientMetadata(client_initial_metadata, kPath));
  }

  //
  // Driving the event engine
  //

  void TickUntilIdle() {
    TickUntilDoneOrIdle([]() { return false; });
  }

  // Moves the clock forward by `duration`, running anything that becomes
  // due along the way.
  void AdvanceTime(
      grpc_event_engine::experimental::EventEngine::Duration duration) {
    ExecCtx exec_ctx;
    event_engine()->TickForDuration(duration);
  }

  //
  // Side stream (fake ext_proc server)
  //

  // Waits for the filter to open its side stream. Returns nullptr if it
  // doesn't.
  FakeXdsTransportFactory::FakeStreamingCall* WaitForSideStream() {
    TickUntilIdle();
    // The engine is idle, so this doesn't tick: it just looks up the stream.
    side_stream_ = transport_factory_->WaitForStream(target_, kExtProcMethod);
    return side_stream_.get();
  }

  // Returns the next ProcessingRequest the filter sent, or nullopt if it
  // doesn't send one.
  std::optional<ProcessingRequest> NextSideStreamRequest() {
    if (side_stream_ == nullptr) {
      ADD_FAILURE() << "no side stream: call WaitForSideStream() first";
      return std::nullopt;
    }
    if (!TickUntilDoneOrIdle(
            [this]() { return side_stream_->HaveMessageFromClient(); })) {
      return std::nullopt;
    }
    std::optional<std::string> payload =
        side_stream_->WaitForMessageFromClient();
    ProcessingRequest request;
    if (!payload.has_value() || !request.ParseFromString(*payload)) {
      ADD_FAILURE() << "failed to read ProcessingRequest from side stream";
      return std::nullopt;
    }
    return request;
  }

  void SendSideStreamResponse(const ProcessingResponse& response) {
    ASSERT_NE(side_stream_, nullptr);
    side_stream_->SendMessageToClient(response.SerializeAsString());
  }

  // Ends the side stream with `status`, as if sent by the ext_proc server.
  void CloseSideStream(absl::Status status) {
    ASSERT_NE(side_stream_, nullptr);
    side_stream_->MaybeSendStatusToClient(std::move(status));
  }

  // Returns true once the filter has half-closed the side stream, or false if
  // it doesn't.
  bool WaitForSideStreamHalfClose() {
    if (side_stream_ == nullptr) return false;
    return TickUntilDoneOrIdle(
        [this]() { return side_stream_->half_closed(); });
  }

  //
  // Data plane helpers
  //

  // Pulls the client's initial metadata at the backend.
  ClientMetadataHandle PullRequiredClientInitialMetadata() {
    ValueOrFailure<ClientMetadataHandle> md = PullClientInitialMetadata();
    if (!md.ok()) {
      ADD_FAILURE() << "backend got no client initial metadata";
      return nullptr;
    }
    return std::move(*md);
  }

  // Pulls the server's initial metadata at the client; there must be some
  // (i.e. not a trailers-only response).
  ServerMetadataHandle PullRequiredServerInitialMetadata() {
    ValueOrFailure<std::optional<ServerMetadataHandle>> md =
        PullServerInitialMetadata();
    if (!md.ok() || !md->has_value()) {
      ADD_FAILURE() << "client got no server initial metadata";
      return nullptr;
    }
    return std::move(**md);
  }

  // Drops everything holding a ref to event_engine() before YodelTest waits
  // for it to have a single owner. Runs after FilterTest has cancelled the
  // call under test.
  void Cleanup() override {
    {
      ExecCtx exec_ctx;
      // A real ext_proc server would see the side stream cancelled and finish
      // it; the fake never does so on its own. Finish it here so the filter
      // drops the side call, which holds references to the fake transport
      // and, through it, to event_engine().
      if (side_stream_ == nullptr && transport_factory_ != nullptr) {
        side_stream_ =
            transport_factory_->WaitForStream(target_, kExtProcMethod);
      }
      if (side_stream_ != nullptr) {
        side_stream_->MaybeSendStatusToClient(absl::CancelledError());
      }
      side_stream_.reset();
    }
    transport_factory_.reset();
    if (stats_plugin_group_ != nullptr) {
      stats_plugin_group_.reset();
      GlobalStatsPluginRegistryTestPeer::ResetGlobalStatsPluginRegistry();
    }
  }

 private:
  static void PopulateHeaderMutation(
      ::envoy::service::ext_proc::v3::HeaderMutation* mutation,
      const std::vector<std::pair<std::string, std::string>>& set_headers) {
    for (const auto& [key, value] : set_headers) {
      auto* header = mutation->add_set_headers();
      header->mutable_header()->set_key(key);
      header->mutable_header()->set_value(value);
    }
  }

  static void PopulateBodyMutation(
      ::envoy::service::ext_proc::v3::BodyMutation* mutation,
      absl::string_view body, bool end_of_stream,
      bool end_of_stream_without_message = false) {
    auto* streamed_response = mutation->mutable_streamed_response();
    streamed_response->set_body(std::string(body));
    streamed_response->set_end_of_stream(end_of_stream);
    streamed_response->set_end_of_stream_without_message(
        end_of_stream_without_message);
  }

  // Ticks the event engine until `done()` returns true, or until there is no
  // more work to do -- i.e. nothing can happen without further input from the
  // test. Returns the final value of `done()`.
  bool TickUntilDoneOrIdle(absl::FunctionRef<bool()> done) {
    // Upper bound on ticks, so that a bug makes the test fail instead of hang.
    constexpr int kMaxTicks = 1000000;
    for (int i = 0; i < kMaxTicks; ++i) {
      if (done()) return true;
      if (event_engine()->IsIdle()) return false;
      ExecCtx exec_ctx;
      event_engine()->Tick();
    }
    ADD_FAILURE() << "event engine did not go idle";
    return done();
  }

  const GrpcXdsServerTarget target_{"dns:///ext_proc.example.com",
                                    /*channel_creds_config=*/nullptr,
                                    /*call_creds_configs=*/{}};
  RefCountedPtr<FakeXdsTransportFactory> transport_factory_;
  RefCountedPtr<FakeXdsTransportFactory::FakeStreamingCall> side_stream_;
  // Set by InitWithStatsPlugin(). Held for the whole test, as a channel holds
  // it via its args: the filter's metrics are only visible to the stats plugin
  // while the group's collection scope is alive.
  std::shared_ptr<GlobalStatsPluginRegistry::StatsPluginGroup>
      stats_plugin_group_;
};

//
// Matchers
//

MATCHER_P(MatchesRequestHeaders, headers_matcher,
          "matches request_headers with given headers") {
  if (!arg.has_request_headers()) {
    *result_listener << "request does not have request_headers";
    return false;
  }
  return ::testing::ExplainMatchResult(
      headers_matcher,
      ExtProcFilterTest::HeaderMapToMultimap(arg.request_headers().headers()),
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
  return ::testing::ExplainMatchResult(
      headers_matcher,
      ExtProcFilterTest::HeaderMapToMultimap(response_headers.headers()),
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
  return ::testing::ExplainMatchResult(trailers_matcher,
                                       ExtProcFilterTest::HeaderMapToMultimap(
                                           arg.response_trailers().trailers()),
                                       result_listener);
}

// A pulled message (ClientToServerNextMessage or ServerToClientNextMessage)
// that carries a message whose payload matches `payload_matcher`.
MATCHER_P(IsMessage, payload_matcher, "is a message") {
  if (!arg.ok()) {
    *result_listener << "pull failed";
    return false;
  }
  if (!arg.has_value()) {
    *result_listener << "got end-of-stream";
    return false;
  }
  std::string payload = arg.value().payload()->JoinIntoString();
  *result_listener << "payload: \"" << payload << "\"";
  return ::testing::Matches(payload_matcher)(payload);
}

// A pulled message that is a clean end-of-stream (i.e. a half-close, or the
// server's trailing metadata).
MATCHER(IsEndOfStream, "is end-of-stream") {
  if (!arg.ok()) {
    *result_listener << "pull failed";
    return false;
  }
  if (arg.has_value()) {
    *result_listener << "got message with payload \""
                     << arg.value().payload()->JoinIntoString() << "\"";
    return false;
  }
  return true;
}

// Bucket counts from a duration histogram holding exactly one sample, which is
// longer than one second. The histograms use ExponentialDoubleHistogramShape
// with the default min of 1.0, so the first bucket holds samples <= 1s.
MATCHER(HasOneSampleOverOneSecond, "has one sample of more than one second") {
  if (!arg.has_value()) {
    *result_listener << "histogram not found";
    return false;
  }
  const std::vector<uint64_t>& counts = *arg;
  uint64_t total = 0;
  for (uint64_t count : counts) total += count;
  *result_listener << "total samples: " << total
                   << ", samples <= 1s: " << (counts.empty() ? 0 : counts[0]);
  return total == 1 && counts[0] == 0;
}

//
// Processing mode tests
//

FILTER_TEST(ExtProcFilterTest, ProcessingModeAllDisabledSuccess) {
  Init(
      ConfigBuilder().SetRequestHeaderMode(false).SetResponseHeaderMode(false));
  StartRpc();
  ASSERT_TRUE(WaitForHandler());
  EXPECT_EQ(WaitForSideStream(), nullptr);
  PullRequiredClientInitialMetadata();
  PushClientMessage(NewMessage(kMessage1));
  EXPECT_THAT(PullClientMessage(), IsMessage(kMessage1));
  PushClientHalfClose();
  EXPECT_THAT(PullClientMessage(), IsEndOfStream());
  PushServerInitialMetadata(NewServerMetadata());
  PushServerMessage(NewMessage(kMessage1));
  PullRequiredServerInitialMetadata();
  EXPECT_THAT(PullServerMessage(), IsMessage(kMessage1));
  PushServerTrailingMetadata(ServerMetadataFromStatus(GRPC_STATUS_OK));
  EXPECT_EQ(PullServerTrailingStatus(), absl::OkStatus());
  WaitForAllPendingWork();
}

// processing_mode unset in the xDS config yields the default ProcessingMode:
// request and response headers are sent.
FILTER_TEST(ExtProcFilterTest, ProcessingModeNotSetSuccess) {
  Init(ConfigBuilder());
  StartRpc();
  ASSERT_NE(WaitForSideStream(), nullptr);
  // Request headers.
  EXPECT_THAT(NextSideStreamRequest(),
              Optional(MatchesRequestHeaders(Contains(Pair(":path", kPath)))));
  SendSideStreamResponse(MakeRequestHeadersMutationResponse(
      {{kRequestHeadersMutatedHeaderKey, kHeaderMutatedValue}}));
  ASSERT_TRUE(WaitForHandler());
  auto client_md = PullRequiredClientInitialMetadata();
  ASSERT_NE(client_md, nullptr);
  EXPECT_THAT(*client_md, HasMetadataKeyValue(kRequestHeadersMutatedHeaderKey,
                                              kHeaderMutatedValue));
  // Request body is not sent.
  PushClientMessage(NewMessage(kMessage1));
  EXPECT_THAT(PullClientMessage(), IsMessage(kMessage1));
  PushClientHalfClose();
  EXPECT_THAT(PullClientMessage(), IsEndOfStream());
  // Response headers.
  PushServerInitialMetadata(NewServerMetadata());
  EXPECT_THAT(NextSideStreamRequest(), Optional(MatchesResponseHeaders(_)));
  SendSideStreamResponse(MakeResponseHeadersMutationResponse(
      {{kResponseHeadersMutatedHeaderKey, kHeaderMutatedValue}}));
  auto server_md = PullRequiredServerInitialMetadata();
  ASSERT_NE(server_md, nullptr);
  EXPECT_THAT(*server_md, HasMetadataKeyValue(kResponseHeadersMutatedHeaderKey,
                                              kHeaderMutatedValue));
  // Response body and trailers are not sent.
  PushServerMessage(NewMessage(kMessage1));
  EXPECT_THAT(PullServerMessage(), IsMessage(kMessage1));
  PushServerTrailingMetadata(ServerMetadataFromStatus(GRPC_STATUS_OK));
  EXPECT_EQ(PullServerTrailingStatus(), absl::OkStatus());
  EXPECT_EQ(NextSideStreamRequest(), std::nullopt);
  WaitForAllPendingWork();
}

FILTER_TEST(ExtProcFilterTest, ProcessingModeAllEnabledSuccess) {
  Init(ConfigBuilder().EnableAllProcessingModes());
  StartRpc();
  ASSERT_NE(WaitForSideStream(), nullptr);
  // Request headers.
  EXPECT_THAT(NextSideStreamRequest(),
              Optional(MatchesRequestHeaders(Contains(Pair(":path", kPath)))));
  SendSideStreamResponse(MakeRequestHeadersMutationResponse(
      {{kRequestHeadersMutatedHeaderKey, kHeaderMutatedValue}}));
  ASSERT_TRUE(WaitForHandler());
  auto client_md = PullRequiredClientInitialMetadata();
  ASSERT_NE(client_md, nullptr);
  EXPECT_THAT(*client_md, HasMetadataKeyValue(kRequestHeadersMutatedHeaderKey,
                                              kHeaderMutatedValue));
  // Request body.
  PushClientMessage(NewMessage(kMessage1));
  EXPECT_THAT(NextSideStreamRequest(),
              Optional(MatchesRequestBody(kMessage1, !kEndOfStream)));
  SendSideStreamResponse(MakeRequestBodyMutationResponse(kMessage1Mutated));
  EXPECT_THAT(PullClientMessage(), IsMessage(kMessage1Mutated));
  // Client half-close.
  PushClientHalfClose();
  EXPECT_THAT(NextSideStreamRequest(),
              Optional(MatchesRequestBody(kEmptyBody, kEndOfStream)));
  SendSideStreamResponse(MakeRequestHalfCloseResponse());
  EXPECT_THAT(PullClientMessage(), IsEndOfStream());
  // Response headers.
  PushServerInitialMetadata(NewServerMetadata());
  EXPECT_THAT(NextSideStreamRequest(), Optional(MatchesResponseHeaders(_)));
  SendSideStreamResponse(MakeResponseHeadersMutationResponse(
      {{kResponseHeadersMutatedHeaderKey, kHeaderMutatedValue}}));
  // Response body.
  PushServerMessage(NewMessage(kMessage1Mutated));
  EXPECT_THAT(NextSideStreamRequest(),
              Optional(MatchesResponseBody(kMessage1Mutated, !kEndOfStream)));
  SendSideStreamResponse(
      MakeResponseBodyMutationResponse(kMessage1DoubleMutated));
  // Response trailers.
  PushServerTrailingMetadata(ServerMetadataFromStatus(GRPC_STATUS_OK));
  EXPECT_THAT(NextSideStreamRequest(), Optional(MatchesResponseTrailers(_)));
  SendSideStreamResponse(MakeResponseTrailersMutationResponse(
      {{kResponseTrailersMutatedHeaderKey, kHeaderMutatedValue}}));
  // The client sees all of the response mutations.
  auto server_md = PullRequiredServerInitialMetadata();
  ASSERT_NE(server_md, nullptr);
  EXPECT_THAT(*server_md, HasMetadataKeyValue(kResponseHeadersMutatedHeaderKey,
                                              kHeaderMutatedValue));
  EXPECT_THAT(PullServerMessage(), IsMessage(kMessage1DoubleMutated));
  auto trailers = PullServerTrailingMetadata();
  ASSERT_TRUE(trailers.ok());
  EXPECT_THAT(**trailers, HasMetadataResult(absl::OkStatus()));
  EXPECT_THAT(**trailers, HasMetadataKeyValue(kResponseTrailersMutatedHeaderKey,
                                              kHeaderMutatedValue));
  WaitForAllPendingWork();
}

FILTER_TEST(ExtProcFilterTest,
            ProcessingModeAllEnabledWithObservabilityModeSuccess) {
  Init(ConfigBuilder()
           .SetObservabilityMode(true)
           .SetDeferredCloseTimeout(Duration::Seconds(1))
           .EnableAllProcessingModes());
  StartRpc({{kCustomHeaderKey, kCustomHeaderValue}});
  ASSERT_NE(WaitForSideStream(), nullptr);
  // Every event is sent to the ext_proc server, but its responses are
  // ignored and nothing waits for them.
  // Request headers.
  EXPECT_THAT(NextSideStreamRequest(),
              Optional(MatchesRequestHeaders(AllOf(
                  Contains(Pair(":path", kPath)),
                  Contains(Pair(kCustomHeaderKey, kCustomHeaderValue))))));
  SendSideStreamResponse(MakeRequestHeadersMutationResponse(
      {{kRequestHeadersMutatedHeaderKey, kHeaderMutatedValue}}));
  ASSERT_TRUE(WaitForHandler());
  auto client_md = PullRequiredClientInitialMetadata();
  ASSERT_NE(client_md, nullptr);
  EXPECT_THAT(*client_md,
              HasMetadataKeyValue(kCustomHeaderKey, kCustomHeaderValue));
  EXPECT_THAT(*client_md, LacksMetadataKey(kRequestHeadersMutatedHeaderKey));
  // Request body.
  PushClientMessage(NewMessage(kMessage1));
  EXPECT_THAT(NextSideStreamRequest(),
              Optional(MatchesRequestBody(kMessage1, !kEndOfStream)));
  SendSideStreamResponse(MakeRequestBodyMutationResponse(kMessage1Mutated));
  EXPECT_THAT(PullClientMessage(), IsMessage(kMessage1));
  // Client half-close.
  PushClientHalfClose();
  EXPECT_THAT(NextSideStreamRequest(),
              Optional(MatchesRequestBody(kEmptyBody, kEndOfStream)));
  EXPECT_THAT(PullClientMessage(), IsEndOfStream());
  // Response headers.
  PushServerInitialMetadata(
      NewServerMetadata({{kCustomHeaderKey, kCustomHeaderValue}}));
  EXPECT_THAT(NextSideStreamRequest(),
              Optional(MatchesResponseHeaders(AllOf(
                  Contains(Pair(kCustomHeaderKey, kCustomHeaderValue)),
                  Not(Contains(Pair(kRequestHeadersMutatedHeaderKey, _)))))));
  SendSideStreamResponse(MakeResponseHeadersMutationResponse(
      {{kResponseHeadersMutatedHeaderKey, kHeaderMutatedValue}}));
  auto server_md = PullRequiredServerInitialMetadata();
  ASSERT_NE(server_md, nullptr);
  EXPECT_THAT(*server_md,
              HasMetadataKeyValue(kCustomHeaderKey, kCustomHeaderValue));
  EXPECT_THAT(*server_md, LacksMetadataKey(kResponseHeadersMutatedHeaderKey));
  // Response body.
  PushServerMessage(NewMessage(kMessage1));
  EXPECT_THAT(NextSideStreamRequest(),
              Optional(MatchesResponseBody(kMessage1, !kEndOfStream)));
  SendSideStreamResponse(MakeResponseBodyMutationResponse(kMessage1Mutated));
  EXPECT_THAT(PullServerMessage(), IsMessage(kMessage1));
  // Response trailers.
  PushServerTrailingMetadata(ServerMetadataFromStatus(GRPC_STATUS_OK));
  EXPECT_THAT(NextSideStreamRequest(), Optional(MatchesResponseTrailers(_)));
  SendSideStreamResponse(MakeResponseTrailersMutationResponse(
      {{kResponseTrailersMutatedHeaderKey, kHeaderMutatedValue}}));
  auto trailers = PullServerTrailingMetadata();
  ASSERT_TRUE(trailers.ok());
  EXPECT_THAT(**trailers, HasMetadataResult(absl::OkStatus()));
  EXPECT_THAT(**trailers, LacksMetadataKey(kResponseTrailersMutatedHeaderKey));
  WaitForAllPendingWork();
}

//
// Trailers-only tests
//

FILTER_TEST(ExtProcFilterTest, TrailersOnlyProcessingModeAllEnabled) {
  Init(ConfigBuilder().EnableAllProcessingModes());
  StartRpc();
  ASSERT_NE(WaitForSideStream(), nullptr);
  EXPECT_THAT(NextSideStreamRequest(),
              Optional(MatchesRequestHeaders(Contains(Pair(":path", kPath)))));
  SendSideStreamResponse(MakeRequestHeadersMutationResponse(
      {{kRequestHeadersMutatedHeaderKey, kHeaderMutatedValue}}));
  ASSERT_TRUE(WaitForHandler());
  PullRequiredClientInitialMetadata();
  PushClientMessage(NewMessage(kMessage1));
  EXPECT_THAT(NextSideStreamRequest(),
              Optional(MatchesRequestBody(kMessage1, !kEndOfStream)));
  SendSideStreamResponse(MakeRequestBodyMutationResponse(kMessage1));
  EXPECT_THAT(PullClientMessage(), IsMessage(kMessage1));
  PushClientHalfClose();
  EXPECT_THAT(NextSideStreamRequest(),
              Optional(MatchesRequestBody(kEmptyBody, kEndOfStream)));
  SendSideStreamResponse(MakeRequestHalfCloseResponse());
  EXPECT_THAT(PullClientMessage(), IsEndOfStream());
  // The backend fails the call with a trailers-only response, which the
  // filter sends to the ext_proc server as response headers.
  PushServerTrailingMetadata(
      ServerMetadataFromStatus(GRPC_STATUS_FAILED_PRECONDITION));
  EXPECT_THAT(NextSideStreamRequest(),
              Optional(MatchesResponseHeaders(_, kEndOfStream)));
  SendSideStreamResponse(MakeResponseHeadersMutationResponse(
      {{kResponseHeadersMutatedHeaderKey, kHeaderMutatedValue}}));
  // No further events are sent to the ext_proc server.
  EXPECT_EQ(NextSideStreamRequest(), std::nullopt);
  auto trailers = PullServerTrailingMetadata();
  ASSERT_TRUE(trailers.ok());
  EXPECT_EQ(ServerMetadataToStatus(**trailers),
            absl::FailedPreconditionError(""));
  EXPECT_THAT(**trailers, HasMetadataKeyValue(kResponseHeadersMutatedHeaderKey,
                                              kHeaderMutatedValue));
  WaitForAllPendingWork();
}

FILTER_TEST(ExtProcFilterTest,
            TrailersOnlyProcessingModeAllEnabledWithObservabilityMode) {
  Init(ConfigBuilder().SetObservabilityMode(true).EnableAllProcessingModes());
  StartRpc();
  ASSERT_NE(WaitForSideStream(), nullptr);
  EXPECT_THAT(NextSideStreamRequest(),
              Optional(MatchesRequestHeaders(Contains(Pair(":path", kPath)))));
  ASSERT_TRUE(WaitForHandler());
  PullRequiredClientInitialMetadata();
  PushClientMessage(NewMessage(kMessage1));
  EXPECT_THAT(NextSideStreamRequest(),
              Optional(MatchesRequestBody(kMessage1, !kEndOfStream)));
  EXPECT_THAT(PullClientMessage(), IsMessage(kMessage1));
  PushClientHalfClose();
  EXPECT_THAT(NextSideStreamRequest(),
              Optional(MatchesRequestBody(kEmptyBody, kEndOfStream)));
  EXPECT_THAT(PullClientMessage(), IsEndOfStream());
  // Response headers are sent in observability mode even for a
  // trailers-only response.
  PushServerTrailingMetadata(
      ServerMetadataFromStatus(GRPC_STATUS_FAILED_PRECONDITION));
  EXPECT_THAT(NextSideStreamRequest(),
              Optional(MatchesResponseHeaders(_, kEndOfStream)));
  EXPECT_EQ(NextSideStreamRequest(), std::nullopt);
  EXPECT_EQ(PullServerTrailingStatus(), absl::FailedPreconditionError(""));
  WaitForAllPendingWork();
}

//
// Request headers / body tests
//

FILTER_TEST(ExtProcFilterTest, ContinueAndReplaceFails) {
  Init(ConfigBuilder().EnableAllProcessingModes());
  StartRpc();
  ASSERT_NE(WaitForSideStream(), nullptr);
  EXPECT_THAT(NextSideStreamRequest(),
              Optional(MatchesRequestHeaders(Contains(Pair(":path", kPath)))));
  ProcessingResponse response;
  response.mutable_request_headers()->mutable_response()->set_status(
      ::envoy::service::ext_proc::v3::CommonResponse::CONTINUE_AND_REPLACE);
  SendSideStreamResponse(response);
  EXPECT_EQ(PullServerTrailingStatus(),
            absl::InternalError("CONTINUE_AND_REPLACE is not supported"));
  WaitForAllPendingWork();
}

FILTER_TEST(ExtProcFilterTest, RequestHeadersInvalidHeaderMutationFails) {
  Init(ConfigBuilder().EnableAllProcessingModes());
  StartRpc();
  ASSERT_NE(WaitForSideStream(), nullptr);
  EXPECT_THAT(NextSideStreamRequest(),
              Optional(MatchesRequestHeaders(Contains(Pair(":path", kPath)))));
  SendSideStreamResponse(
      MakeRequestHeadersMutationResponse({{"host", "invalid-host"}}));
  EXPECT_EQ(PullServerTrailingStatus(),
            absl::InternalError(
                "Failed to parse XdsHeaderValueOption: [field:header.key "
                "error:header \"host\" not allowed]"));
  WaitForAllPendingWork();
}

FILTER_TEST(ExtProcFilterTest, RequestHeadersRequestAttributesSent) {
  Init(ConfigBuilder()
           .SetRequestHeaderMode(true)
           .SetResponseHeaderMode(false)
           .AddRequestAttribute("request.path")
           .AddRequestAttribute("request.method"));
  StartRpc();
  ASSERT_NE(WaitForSideStream(), nullptr);
  auto request = NextSideStreamRequest();
  ASSERT_THAT(request,
              Optional(MatchesRequestHeaders(Contains(Pair(":path", kPath)))));
  EXPECT_EQ(GetExtProcAttribute(*request, "request.path"), kPath);
  // The method is not in the metadata, so the filter falls back to POST.
  EXPECT_EQ(GetExtProcAttribute(*request, "request.method"), "POST");
  SendSideStreamResponse(MakeRequestHeadersMutationResponse({}));
  ASSERT_TRUE(WaitForHandler());
  PullRequiredClientInitialMetadata();
  PushServerInitialMetadata(NewServerMetadata());
  PullRequiredServerInitialMetadata();
  PushServerTrailingMetadata(ServerMetadataFromStatus(GRPC_STATUS_OK));
  EXPECT_EQ(PullServerTrailingStatus(), absl::OkStatus());
  WaitForAllPendingWork();
}

FILTER_TEST(ExtProcFilterTest,
            RequestAttributesSentInRequestBodyWhenRequestHeaderIsSkip) {
  Init(ConfigBuilder()
           .SetRequestHeaderMode(false)
           .SetResponseHeaderMode(false)
           .SetRequestBodyMode(true)
           .AddRequestAttribute("request.path")
           .AddRequestAttribute("request.method"));
  StartRpc();
  ASSERT_TRUE(WaitForHandler());
  ASSERT_NE(WaitForSideStream(), nullptr);
  PullRequiredClientInitialMetadata();
  // The request attributes are attached to the first request body event.
  PushClientMessage(NewMessage(kMessage1));
  auto request = NextSideStreamRequest();
  ASSERT_THAT(request, Optional(MatchesRequestBody(kMessage1, !kEndOfStream)));
  EXPECT_EQ(GetExtProcAttribute(*request, "request.path"), kPath);
  EXPECT_EQ(GetExtProcAttribute(*request, "request.method"), "POST");
  SendSideStreamResponse(MakeRequestBodyMutationResponse(kMessage1));
  EXPECT_THAT(PullClientMessage(), IsMessage(kMessage1));
  PushClientHalfClose();
  EXPECT_THAT(NextSideStreamRequest(),
              Optional(MatchesRequestBody(kEmptyBody, kEndOfStream)));
  SendSideStreamResponse(MakeRequestHalfCloseResponse());
  EXPECT_THAT(PullClientMessage(), IsEndOfStream());
  PushServerInitialMetadata(NewServerMetadata());
  PullRequiredServerInitialMetadata();
  PushServerTrailingMetadata(ServerMetadataFromStatus(GRPC_STATUS_OK));
  EXPECT_EQ(PullServerTrailingStatus(), absl::OkStatus());
  WaitForAllPendingWork();
}

FILTER_TEST(ExtProcFilterTest, RequestBodyGrpcMessageCompressed) {
  // failure_mode_allow applies only to side stream errors: an invalid
  // response from the ext_proc server still fails the call.
  Init(ConfigBuilder()
           .SetFailureModeAllow(true)
           .SetRequestHeaderMode(true)
           .SetResponseHeaderMode(false)
           .SetRequestBodyMode(true));
  StartRpc();
  ASSERT_NE(WaitForSideStream(), nullptr);
  EXPECT_THAT(NextSideStreamRequest(),
              Optional(MatchesRequestHeaders(Contains(Pair(":path", kPath)))));
  SendSideStreamResponse(MakeRequestHeadersMutationResponse({}));
  ASSERT_TRUE(WaitForHandler());
  PushClientMessage(NewMessage(kMessage1));
  EXPECT_THAT(NextSideStreamRequest(),
              Optional(MatchesRequestBody(kMessage1, !kEndOfStream)));
  ProcessingResponse response;
  response.mutable_request_body()
      ->mutable_response()
      ->mutable_body_mutation()
      ->mutable_streamed_response()
      ->set_grpc_message_compressed(true);
  SendSideStreamResponse(response);
  EXPECT_EQ(PullServerTrailingStatus(),
            absl::InternalError("grpc_message_compressed is not supported"));
  WaitForAllPendingWork();
}

//
// Bidi stream / early half-close tests
//

FILTER_TEST(ExtProcFilterTest,
            BidiStreamExtProcEarlyHalfCloseSubsequentWriteFails) {
  Init(ConfigBuilder()
           .SetRequestHeaderMode(true)
           .SetResponseHeaderMode(false)
           .SetRequestBodyMode(true));
  StartRpc();
  ASSERT_NE(WaitForSideStream(), nullptr);
  EXPECT_THAT(NextSideStreamRequest(),
              Optional(MatchesRequestHeaders(Contains(Pair(":path", kPath)))));
  SendSideStreamResponse(MakeRequestHeadersMutationResponse({}));
  ASSERT_TRUE(WaitForHandler());
  PullRequiredClientInitialMetadata();
  PushClientMessage(NewMessage(kMessage1));
  EXPECT_THAT(NextSideStreamRequest(),
              Optional(MatchesRequestBody(kMessage1, !kEndOfStream)));
  // The ext_proc server half-closes the request stream along with the
  // mutated message.
  SendSideStreamResponse(
      MakeRequestBodyMutationResponse(kMessage1Mutated, kEndOfStream));
  EXPECT_THAT(PullClientMessage(), IsMessage(kMessage1Mutated));
  PushServerInitialMetadata(NewServerMetadata());
  PushServerMessage(NewMessage(kMessage1Mutated));
  PullRequiredServerInitialMetadata();
  EXPECT_THAT(PullServerMessage(), IsMessage(kMessage1Mutated));
  // A further message from the client can't be delivered.
  PushClientMessage(NewMessage(kMessage2));
  EXPECT_EQ(PullServerTrailingStatus(),
            absl::InternalError(
                "Client tried to send a message but external processor "
                "server has already force sent half close to the "
                "server"));
  WaitForAllPendingWork();
}

FILTER_TEST(ExtProcFilterTest,
            BidiStreamExtProcEarlyHalfCloseSubsequentHalfCloseSuccess) {
  Init(ConfigBuilder()
           .SetRequestHeaderMode(true)
           .SetResponseHeaderMode(false)
           .SetRequestBodyMode(true));
  StartRpc();
  ASSERT_NE(WaitForSideStream(), nullptr);
  EXPECT_THAT(NextSideStreamRequest(),
              Optional(MatchesRequestHeaders(Contains(Pair(":path", kPath)))));
  SendSideStreamResponse(MakeRequestHeadersMutationResponse({}));
  ASSERT_TRUE(WaitForHandler());
  PullRequiredClientInitialMetadata();
  PushClientMessage(NewMessage(kMessage1));
  EXPECT_THAT(NextSideStreamRequest(),
              Optional(MatchesRequestBody(kMessage1, !kEndOfStream)));
  SendSideStreamResponse(
      MakeRequestBodyMutationResponse(kMessage1Mutated, kEndOfStream));
  EXPECT_THAT(PullClientMessage(), IsMessage(kMessage1Mutated));
  PushServerInitialMetadata(NewServerMetadata());
  PushServerMessage(NewMessage(kMessage1Mutated));
  PullRequiredServerInitialMetadata();
  EXPECT_THAT(PullServerMessage(), IsMessage(kMessage1Mutated));
  // The client's own half-close goes straight to the backend.
  PushClientHalfClose();
  EXPECT_THAT(PullClientMessage(), IsEndOfStream());
  PushServerTrailingMetadata(ServerMetadataFromStatus(GRPC_STATUS_OK));
  EXPECT_THAT(PullServerMessage(), IsEndOfStream());
  EXPECT_EQ(PullServerTrailingStatus(), absl::OkStatus());
  EXPECT_EQ(NextSideStreamRequest(), std::nullopt);
  WaitForAllPendingWork();
}

FILTER_TEST(ExtProcFilterTest, BidiStreamNormalHalfCloseSuccess) {
  Init(ConfigBuilder()
           .SetRequestHeaderMode(true)
           .SetResponseHeaderMode(false)
           .SetRequestBodyMode(true));
  StartRpc();
  ASSERT_NE(WaitForSideStream(), nullptr);
  EXPECT_THAT(NextSideStreamRequest(),
              Optional(MatchesRequestHeaders(Contains(Pair(":path", kPath)))));
  SendSideStreamResponse(MakeRequestHeadersMutationResponse({}));
  ASSERT_TRUE(WaitForHandler());
  PullRequiredClientInitialMetadata();
  PushServerInitialMetadata(NewServerMetadata());
  PullRequiredServerInitialMetadata();
  for (int i = 1; i <= 3; ++i) {
    const std::string message = absl::StrCat("message", i);
    PushClientMessage(NewMessage(message));
    EXPECT_THAT(NextSideStreamRequest(),
                Optional(MatchesRequestBody(message, !kEndOfStream)));
    SendSideStreamResponse(MakeRequestBodyMutationResponse(message));
    EXPECT_THAT(PullClientMessage(), IsMessage(message));
    PushServerMessage(NewMessage(message));
    EXPECT_THAT(PullServerMessage(), IsMessage(message));
  }
  PushClientHalfClose();
  auto request = NextSideStreamRequest();
  ASSERT_THAT(request, Optional(MatchesRequestBody(kEmptyBody, kEndOfStream)));
  EXPECT_TRUE(request->request_body().end_of_stream_without_message());
  SendSideStreamResponse(MakeRequestHalfCloseResponse());
  EXPECT_THAT(PullClientMessage(), IsEndOfStream());
  PushServerTrailingMetadata(ServerMetadataFromStatus(GRPC_STATUS_OK));
  EXPECT_THAT(PullServerMessage(), IsEndOfStream());
  EXPECT_EQ(PullServerTrailingStatus(), absl::OkStatus());
  WaitForAllPendingWork();
}

//
// Response headers / body / trailers tests
//

FILTER_TEST(ExtProcFilterTest, ResponseHeadersInvalidHeaderMutationFails) {
  Init(ConfigBuilder().SetRequestHeaderMode(false).SetResponseHeaderMode(true));
  StartRpc();
  ASSERT_TRUE(WaitForHandler());
  ASSERT_NE(WaitForSideStream(), nullptr);
  PushServerInitialMetadata(NewServerMetadata());
  EXPECT_THAT(NextSideStreamRequest(), Optional(MatchesResponseHeaders(_)));
  SendSideStreamResponse(
      MakeResponseHeadersMutationResponse({{"host", "invalid-host"}}));
  EXPECT_EQ(PullServerTrailingStatus(),
            absl::InternalError(
                "Failed to parse XdsHeaderValueOption: [field:header.key "
                "error:header \"host\" not allowed]"));
  WaitForAllPendingWork();
}

FILTER_TEST(ExtProcFilterTest, ResponseBodyGrpcMessageCompressed) {
  // failure_mode_allow applies only to side stream errors: an invalid
  // response from the ext_proc server still fails the call.
  Init(ConfigBuilder()
           .SetFailureModeAllow(true)
           .SetRequestHeaderMode(false)
           .SetResponseHeaderMode(true)
           .SetResponseTrailerMode(true)
           .SetResponseBodyMode(true));
  StartRpc();
  ASSERT_TRUE(WaitForHandler());
  ASSERT_NE(WaitForSideStream(), nullptr);
  PushServerInitialMetadata(NewServerMetadata());
  EXPECT_THAT(NextSideStreamRequest(), Optional(MatchesResponseHeaders(_)));
  SendSideStreamResponse(MakeResponseHeadersMutationResponse({}));
  PushServerMessage(NewMessage(kMessage1));
  EXPECT_THAT(NextSideStreamRequest(),
              Optional(MatchesResponseBody(kMessage1, !kEndOfStream)));
  ProcessingResponse response;
  response.mutable_response_body()
      ->mutable_response()
      ->mutable_body_mutation()
      ->mutable_streamed_response()
      ->set_grpc_message_compressed(true);
  SendSideStreamResponse(response);
  EXPECT_EQ(PullServerTrailingStatus(),
            absl::InternalError("grpc_message_compressed is not supported"));
  WaitForAllPendingWork();
}

FILTER_TEST(ExtProcFilterTest, ResponseTrailersInvalidHeaderMutationFails) {
  Init(ConfigBuilder()
           .SetRequestHeaderMode(false)
           .SetResponseHeaderMode(false)
           .SetResponseTrailerMode(true));
  StartRpc();
  ASSERT_TRUE(WaitForHandler());
  ASSERT_NE(WaitForSideStream(), nullptr);
  PushServerInitialMetadata(NewServerMetadata());
  PullRequiredServerInitialMetadata();
  PushServerTrailingMetadata(ServerMetadataFromStatus(GRPC_STATUS_OK));
  EXPECT_THAT(NextSideStreamRequest(), Optional(MatchesResponseTrailers(_)));
  SendSideStreamResponse(
      MakeResponseTrailersMutationResponse({{"host", "invalid-host"}}));
  EXPECT_EQ(PullServerTrailingStatus(),
            absl::InternalError(
                "Failed to parse XdsHeaderValueOption: [field:header.key "
                "error:header \"host\" not allowed]"));
  WaitForAllPendingWork();
}

//
// Immediate response tests
//

FILTER_TEST(ExtProcFilterTest, DisableImmediateResponse) {
  Init(ConfigBuilder()
           .SetDisableImmediateResponse(true)
           .EnableAllProcessingModes());
  StartRpc();
  ASSERT_NE(WaitForSideStream(), nullptr);
  EXPECT_THAT(NextSideStreamRequest(),
              Optional(MatchesRequestHeaders(Contains(Pair(":path", kPath)))));
  SendSideStreamResponse(MakeImmediateResponse(
      GRPC_STATUS_PERMISSION_DENIED,
      "Access Denied by ExtProc (Request Headers)",
      {{kImmediateResponseHeaderKey, kHeaderMutatedValue}}));
  EXPECT_EQ(
      PullServerTrailingStatus(),
      absl::InternalError("unhandled immediate response due to config disabled "
                          "it"));
  WaitForAllPendingWork();
}

FILTER_TEST(ExtProcFilterTest, ImmediateResponse) {
  Init(ConfigBuilder().EnableAllProcessingModes());
  StartRpc();
  ASSERT_NE(WaitForSideStream(), nullptr);
  EXPECT_THAT(NextSideStreamRequest(),
              Optional(MatchesRequestHeaders(Contains(Pair(":path", kPath)))));
  SendSideStreamResponse(MakeImmediateResponse(
      GRPC_STATUS_PERMISSION_DENIED,
      "Access Denied by ExtProc (Request Headers)",
      {{kImmediateResponseHeaderKey, kHeaderMutatedValue}}));
  auto trailers = PullServerTrailingMetadata();
  ASSERT_TRUE(trailers.ok());
  EXPECT_EQ(ServerMetadataToStatus(**trailers),
            absl::PermissionDeniedError(
                "Access Denied by ExtProc (Request Headers)"));
  EXPECT_THAT(**trailers, HasMetadataKeyValue(kImmediateResponseHeaderKey,
                                              kHeaderMutatedValue));
  WaitForAllPendingWork();
}

//
// Stream drain tests
//
// After a request_drain, the filter half-closes the side stream and stops
// sending events; the ext_proc server then finishes the stream, after which
// all traffic passes through unchanged.
//

FILTER_TEST(ExtProcFilterTest, StreamDrainRequestOnClientBody) {
  Init(ConfigBuilder()
           .SetRequestHeaderMode(false)
           .SetResponseHeaderMode(false)
           .SetRequestBodyMode(true));
  StartRpc();
  ASSERT_TRUE(WaitForHandler());
  ASSERT_NE(WaitForSideStream(), nullptr);
  PullRequiredClientInitialMetadata();
  PushClientMessage(NewMessage(kMessage1));
  EXPECT_THAT(NextSideStreamRequest(),
              Optional(MatchesRequestBody(kMessage1, !kEndOfStream)));
  SendSideStreamResponse(MakeRequestBodyMutationResponse(
      kMessage1Mutated, !kEndOfStream, /*request_drain=*/true));
  EXPECT_THAT(PullClientMessage(), IsMessage(kMessage1Mutated));
  // The filter half-closes the side stream once draining is done, and the
  // ext_proc server finishes it with OK.
  ASSERT_TRUE(WaitForSideStreamHalfClose());
  CloseSideStream(absl::OkStatus());
  PushServerInitialMetadata(NewServerMetadata());
  PushServerMessage(NewMessage(kMessage1Mutated));
  PullRequiredServerInitialMetadata();
  EXPECT_THAT(PullServerMessage(), IsMessage(kMessage1Mutated));
  PushClientMessage(NewMessage(kMessage2));
  EXPECT_THAT(PullClientMessage(), IsMessage(kMessage2));
  PushServerMessage(NewMessage(kMessage2));
  EXPECT_THAT(PullServerMessage(), IsMessage(kMessage2));
  PushClientHalfClose();
  EXPECT_THAT(PullClientMessage(), IsEndOfStream());
  PushServerTrailingMetadata(ServerMetadataFromStatus(GRPC_STATUS_OK));
  EXPECT_EQ(PullServerTrailingStatus(), absl::OkStatus());
  EXPECT_EQ(NextSideStreamRequest(), std::nullopt);
  WaitForAllPendingWork();
}

FILTER_TEST(ExtProcFilterTest, StreamDrainRequestOnRequestHeaders) {
  Init(ConfigBuilder().SetRequestHeaderMode(true).SetResponseHeaderMode(false));
  StartRpc();
  ASSERT_NE(WaitForSideStream(), nullptr);
  EXPECT_THAT(NextSideStreamRequest(),
              Optional(MatchesRequestHeaders(Contains(Pair(":path", kPath)))));
  SendSideStreamResponse(
      MakeRequestHeadersMutationResponse({}, /*request_drain=*/true));
  ASSERT_TRUE(WaitForHandler());
  // The filter half-closes the side stream once draining is done, and the
  // ext_proc server finishes it with OK.
  ASSERT_TRUE(WaitForSideStreamHalfClose());
  CloseSideStream(absl::OkStatus());
  PullRequiredClientInitialMetadata();
  PushClientMessage(NewMessage(kMessage1));
  EXPECT_THAT(PullClientMessage(), IsMessage(kMessage1));
  PushClientHalfClose();
  EXPECT_THAT(PullClientMessage(), IsEndOfStream());
  PushServerInitialMetadata(NewServerMetadata());
  PushServerMessage(NewMessage(kMessage1));
  PullRequiredServerInitialMetadata();
  EXPECT_THAT(PullServerMessage(), IsMessage(kMessage1));
  PushServerTrailingMetadata(ServerMetadataFromStatus(GRPC_STATUS_OK));
  EXPECT_EQ(PullServerTrailingStatus(), absl::OkStatus());
  EXPECT_EQ(NextSideStreamRequest(), std::nullopt);
  WaitForAllPendingWork();
}

FILTER_TEST(ExtProcFilterTest, StreamDrainRequestOnResponseHeaders) {
  Init(ConfigBuilder().SetRequestHeaderMode(false).SetResponseHeaderMode(true));
  StartRpc();
  ASSERT_TRUE(WaitForHandler());
  ASSERT_NE(WaitForSideStream(), nullptr);
  PullRequiredClientInitialMetadata();
  PushClientMessage(NewMessage(kMessage1));
  EXPECT_THAT(PullClientMessage(), IsMessage(kMessage1));
  PushServerInitialMetadata(NewServerMetadata());
  EXPECT_THAT(NextSideStreamRequest(), Optional(MatchesResponseHeaders(_)));
  SendSideStreamResponse(
      MakeResponseHeadersMutationResponse({}, /*request_drain=*/true));
  // The filter half-closes the side stream once draining is done, and the
  // ext_proc server finishes it with OK.
  ASSERT_TRUE(WaitForSideStreamHalfClose());
  CloseSideStream(absl::OkStatus());
  PushServerMessage(NewMessage(kMessage1));
  PullRequiredServerInitialMetadata();
  EXPECT_THAT(PullServerMessage(), IsMessage(kMessage1));
  PushClientHalfClose();
  EXPECT_THAT(PullClientMessage(), IsEndOfStream());
  PushServerTrailingMetadata(ServerMetadataFromStatus(GRPC_STATUS_OK));
  EXPECT_EQ(PullServerTrailingStatus(), absl::OkStatus());
  EXPECT_EQ(NextSideStreamRequest(), std::nullopt);
  WaitForAllPendingWork();
}

FILTER_TEST(ExtProcFilterTest, StreamDrainRequestOnResponseTrailers) {
  Init(ConfigBuilder()
           .SetRequestHeaderMode(false)
           .SetResponseHeaderMode(false)
           .SetResponseTrailerMode(true));
  StartRpc();
  ASSERT_TRUE(WaitForHandler());
  ASSERT_NE(WaitForSideStream(), nullptr);
  PullRequiredClientInitialMetadata();
  PushClientMessage(NewMessage(kMessage1));
  EXPECT_THAT(PullClientMessage(), IsMessage(kMessage1));
  PushServerInitialMetadata(NewServerMetadata());
  PushServerMessage(NewMessage(kMessage1));
  PullRequiredServerInitialMetadata();
  EXPECT_THAT(PullServerMessage(), IsMessage(kMessage1));
  PushClientHalfClose();
  EXPECT_THAT(PullClientMessage(), IsEndOfStream());
  PushServerTrailingMetadata(ServerMetadataFromStatus(GRPC_STATUS_OK));
  EXPECT_THAT(NextSideStreamRequest(), Optional(MatchesResponseTrailers(_)));
  SendSideStreamResponse(
      MakeResponseTrailersMutationResponse({}, /*request_drain=*/true));
  // The filter half-closes the side stream once draining is done, and the
  // ext_proc server finishes it with OK.
  ASSERT_TRUE(WaitForSideStreamHalfClose());
  CloseSideStream(absl::OkStatus());
  EXPECT_EQ(PullServerTrailingStatus(), absl::OkStatus());
  WaitForAllPendingWork();
}

FILTER_TEST(ExtProcFilterTest, StreamDrainRequestOnServerBody) {
  Init(ConfigBuilder()
           .SetRequestHeaderMode(false)
           .SetResponseHeaderMode(false)
           .SetResponseTrailerMode(true)
           .SetResponseBodyMode(true));
  StartRpc();
  ASSERT_TRUE(WaitForHandler());
  ASSERT_NE(WaitForSideStream(), nullptr);
  PullRequiredClientInitialMetadata();
  PushClientMessage(NewMessage(kMessage1));
  EXPECT_THAT(PullClientMessage(), IsMessage(kMessage1));
  PushServerInitialMetadata(NewServerMetadata());
  PushServerMessage(NewMessage(kMessage1));
  EXPECT_THAT(NextSideStreamRequest(),
              Optional(MatchesResponseBody(kMessage1, !kEndOfStream)));
  SendSideStreamResponse(MakeResponseBodyMutationResponse(
      kMessage1Mutated, !kEndOfStream, /*request_drain=*/true));
  // The filter half-closes the side stream once draining is done, and the
  // ext_proc server finishes it with OK.
  ASSERT_TRUE(WaitForSideStreamHalfClose());
  CloseSideStream(absl::OkStatus());
  PullRequiredServerInitialMetadata();
  EXPECT_THAT(PullServerMessage(), IsMessage(kMessage1Mutated));
  PushClientMessage(NewMessage(kMessage2));
  EXPECT_THAT(PullClientMessage(), IsMessage(kMessage2));
  PushServerMessage(NewMessage(kMessage2));
  EXPECT_THAT(PullServerMessage(), IsMessage(kMessage2));
  PushClientHalfClose();
  EXPECT_THAT(PullClientMessage(), IsEndOfStream());
  PushServerTrailingMetadata(ServerMetadataFromStatus(GRPC_STATUS_OK));
  EXPECT_EQ(PullServerTrailingStatus(), absl::OkStatus());
  EXPECT_EQ(NextSideStreamRequest(), std::nullopt);
  WaitForAllPendingWork();
}

//
// Ordering tests
//

FILTER_TEST(ExtProcFilterTest,
            ClientToServerOrderingHeadersResponseWhenDisabled) {
  Init(ConfigBuilder()
           .SetRequestHeaderMode(false)
           .SetResponseHeaderMode(false)
           .SetRequestBodyMode(true));
  StartRpc();
  ASSERT_TRUE(WaitForHandler());
  ASSERT_NE(WaitForSideStream(), nullptr);
  PushClientMessage(NewMessage(kMessage1));
  EXPECT_THAT(NextSideStreamRequest(),
              Optional(MatchesRequestBody(kMessage1, !kEndOfStream)));
  SendSideStreamResponse(MakeRequestHeadersMutationResponse({}));
  EXPECT_EQ(
      PullServerTrailingStatus(),
      absl::InternalError("Received unexpected request headers response from "
                          "external processor"));
  WaitForAllPendingWork();
}

FILTER_TEST(ExtProcFilterTest, ClientToServerOrderingRequestBodyBeforeHeaders) {
  // failure_mode_allow applies only to side stream errors: an invalid
  // response from the ext_proc server still fails the call.
  Init(ConfigBuilder()
           .SetFailureModeAllow(true)
           .SetRequestHeaderMode(true)
           .SetResponseHeaderMode(false)
           .SetRequestBodyMode(true));
  StartRpc();
  ASSERT_NE(WaitForSideStream(), nullptr);
  EXPECT_THAT(NextSideStreamRequest(),
              Optional(MatchesRequestHeaders(Contains(Pair(":path", kPath)))));
  SendSideStreamResponse(MakeRequestBodyMutationResponse(""));
  EXPECT_EQ(
      PullServerTrailingStatus(),
      absl::InternalError("Received unexpected request body response from "
                          "external processor"));
  WaitForAllPendingWork();
}

FILTER_TEST(ExtProcFilterTest,
            ServerToClientOrderingHeadersResponseWhenDisabled) {
  Init(ConfigBuilder()
           .SetRequestHeaderMode(false)
           .SetResponseHeaderMode(false)
           .SetResponseTrailerMode(true)
           .SetResponseBodyMode(true));
  StartRpc();
  ASSERT_TRUE(WaitForHandler());
  ASSERT_NE(WaitForSideStream(), nullptr);
  PushServerInitialMetadata(NewServerMetadata());
  PushServerMessage(NewMessage(kMessage1));
  EXPECT_THAT(NextSideStreamRequest(),
              Optional(MatchesResponseBody(kMessage1, !kEndOfStream)));
  SendSideStreamResponse(MakeResponseHeadersMutationResponse({}));
  EXPECT_EQ(
      PullServerTrailingStatus(),
      absl::InternalError("Received unexpected response headers response from "
                          "external processor"));
  WaitForAllPendingWork();
}

FILTER_TEST(ExtProcFilterTest,
            ServerToClientOrderingResponseBodyBeforeHeaders) {
  Init(ConfigBuilder()
           .SetRequestHeaderMode(false)
           .SetResponseHeaderMode(true)
           .SetResponseTrailerMode(true)
           .SetResponseBodyMode(true));
  StartRpc();
  ASSERT_TRUE(WaitForHandler());
  ASSERT_NE(WaitForSideStream(), nullptr);
  PushServerInitialMetadata(NewServerMetadata());
  EXPECT_THAT(NextSideStreamRequest(), Optional(MatchesResponseHeaders(_)));
  SendSideStreamResponse(MakeResponseBodyMutationResponse(""));
  EXPECT_EQ(
      PullServerTrailingStatus(),
      absl::InternalError("Received unexpected response body response from "
                          "external processor"));
  WaitForAllPendingWork();
}

FILTER_TEST(ExtProcFilterTest, ServerToClientOrderingTrailersBeforeHeaders) {
  Init(ConfigBuilder()
           .SetRequestHeaderMode(false)
           .SetResponseHeaderMode(true)
           .SetResponseTrailerMode(true));
  StartRpc();
  ASSERT_TRUE(WaitForHandler());
  ASSERT_NE(WaitForSideStream(), nullptr);
  PushServerInitialMetadata(NewServerMetadata());
  EXPECT_THAT(NextSideStreamRequest(), Optional(MatchesResponseHeaders(_)));
  SendSideStreamResponse(MakeResponseTrailersMutationResponse({}));
  EXPECT_EQ(
      PullServerTrailingStatus(),
      absl::InternalError("Received unexpected response trailers response from "
                          "external processor"));
  WaitForAllPendingWork();
}

FILTER_TEST(ExtProcFilterTest,
            ServerToClientOrderingTrailersBeforeResponseBody) {
  Init(ConfigBuilder()
           .SetRequestHeaderMode(false)
           .SetResponseHeaderMode(true)
           .SetResponseTrailerMode(true)
           .SetResponseBodyMode(true));
  StartRpc();
  ASSERT_TRUE(WaitForHandler());
  ASSERT_NE(WaitForSideStream(), nullptr);
  PushServerInitialMetadata(NewServerMetadata());
  EXPECT_THAT(NextSideStreamRequest(), Optional(MatchesResponseHeaders(_)));
  SendSideStreamResponse(MakeResponseHeadersMutationResponse({}));
  PullRequiredServerInitialMetadata();
  PushServerMessage(NewMessage(kMessage1));
  EXPECT_THAT(NextSideStreamRequest(),
              Optional(MatchesResponseBody(kMessage1, !kEndOfStream)));
  SendSideStreamResponse(MakeResponseTrailersMutationResponse({}));
  EXPECT_EQ(
      PullServerTrailingStatus(),
      absl::InternalError("Received response trailers response before all "
                          "outstanding response body responses were received"));
  WaitForAllPendingWork();
}

FILTER_TEST(ExtProcFilterTest,
            ServerToClientOrderingTrailersResponseWhenDisabled) {
  Init(ConfigBuilder().SetRequestHeaderMode(false).SetResponseHeaderMode(true));
  StartRpc();
  ASSERT_TRUE(WaitForHandler());
  ASSERT_NE(WaitForSideStream(), nullptr);
  PushServerInitialMetadata(NewServerMetadata());
  EXPECT_THAT(NextSideStreamRequest(), Optional(MatchesResponseHeaders(_)));
  // The ext_proc server's responses arrive in order on the side stream, so
  // the trailers response is always processed after the headers response.
  SendSideStreamResponse(MakeResponseHeadersMutationResponse({}));
  SendSideStreamResponse(MakeResponseTrailersMutationResponse({}));
  EXPECT_EQ(
      PullServerTrailingStatus(),
      absl::InternalError("Received unexpected response trailers response from "
                          "external processor"));
  WaitForAllPendingWork();
}

FILTER_TEST(ExtProcFilterTest, ServerToClientResponseBodyHalfClose) {
  // failure_mode_allow applies only to side stream errors: an invalid
  // response from the ext_proc server still fails the call.
  Init(ConfigBuilder()
           .SetFailureModeAllow(true)
           .SetRequestHeaderMode(false)
           .SetResponseHeaderMode(true)
           .SetResponseTrailerMode(true)
           .SetResponseBodyMode(true));
  StartRpc();
  ASSERT_TRUE(WaitForHandler());
  ASSERT_NE(WaitForSideStream(), nullptr);
  PushServerInitialMetadata(NewServerMetadata());
  EXPECT_THAT(NextSideStreamRequest(), Optional(MatchesResponseHeaders(_)));
  SendSideStreamResponse(MakeResponseHeadersMutationResponse({}));
  PullRequiredServerInitialMetadata();
  PushServerMessage(NewMessage(kMessage1));
  EXPECT_THAT(NextSideStreamRequest(),
              Optional(MatchesResponseBody(kMessage1, !kEndOfStream)));
  SendSideStreamResponse(MakeResponseBodyMutationResponse("", kEndOfStream));
  EXPECT_EQ(PullServerTrailingStatus(),
            absl::InternalError(
                "end_of_stream / end_of_stream_without_message is not "
                "supported for response_body"));
  WaitForAllPendingWork();
}

//
// Stream clean close tests
//

FILTER_TEST(ExtProcFilterTest, StreamCleanCloseBodiesNotConfiguredSuccess) {
  Init(ConfigBuilder().SetRequestHeaderMode(true).SetResponseHeaderMode(true));
  StartRpc();
  ASSERT_NE(WaitForSideStream(), nullptr);
  EXPECT_THAT(NextSideStreamRequest(),
              Optional(MatchesRequestHeaders(Contains(Pair(":path", kPath)))));
  // Nothing needs draining, so the call carries on unmodified.
  CloseSideStream(absl::OkStatus());
  ASSERT_TRUE(WaitForHandler());
  PullRequiredClientInitialMetadata();
  PushClientMessage(NewMessage(kMessage1));
  EXPECT_THAT(PullClientMessage(), IsMessage(kMessage1));
  PushClientHalfClose();
  EXPECT_THAT(PullClientMessage(), IsEndOfStream());
  PushServerInitialMetadata(NewServerMetadata());
  PushServerMessage(NewMessage(kMessage1));
  PullRequiredServerInitialMetadata();
  EXPECT_THAT(PullServerMessage(), IsMessage(kMessage1));
  PushServerTrailingMetadata(ServerMetadataFromStatus(GRPC_STATUS_OK));
  EXPECT_EQ(PullServerTrailingStatus(), absl::OkStatus());
  EXPECT_EQ(NextSideStreamRequest(), std::nullopt);
  WaitForAllPendingWork();
}

FILTER_TEST(ExtProcFilterTest, StreamCleanCloseBodiesDrainedSuccess) {
  Init(ConfigBuilder()
           .SetRequestHeaderMode(false)
           .SetResponseHeaderMode(false)
           .SetResponseTrailerMode(true)
           .SetRequestBodyMode(true)
           .SetResponseBodyMode(true));
  StartRpc();
  ASSERT_TRUE(WaitForHandler());
  ASSERT_NE(WaitForSideStream(), nullptr);
  PullRequiredClientInitialMetadata();
  PushClientMessage(NewMessage(kMessage1));
  EXPECT_THAT(NextSideStreamRequest(),
              Optional(MatchesRequestBody(kMessage1, !kEndOfStream)));
  SendSideStreamResponse(MakeRequestBodyMutationResponse(kMessage1Mutated));
  EXPECT_THAT(PullClientMessage(), IsMessage(kMessage1Mutated));
  PushServerInitialMetadata(NewServerMetadata());
  PushServerMessage(NewMessage(kMessage1Mutated));
  EXPECT_THAT(NextSideStreamRequest(),
              Optional(MatchesResponseBody(kMessage1Mutated, !kEndOfStream)));
  SendSideStreamResponse(MakeResponseBodyMutationResponse(
      kMessage1DoubleMutated, !kEndOfStream, /*request_drain=*/true));
  PullRequiredServerInitialMetadata();
  EXPECT_THAT(PullServerMessage(), IsMessage(kMessage1DoubleMutated));
  // The filter half-closes the side stream once draining is done, and the
  // ext_proc server finishes it with OK.
  ASSERT_TRUE(WaitForSideStreamHalfClose());
  CloseSideStream(absl::OkStatus());
  PushClientHalfClose();
  EXPECT_THAT(PullClientMessage(), IsEndOfStream());
  PushServerTrailingMetadata(ServerMetadataFromStatus(GRPC_STATUS_OK));
  EXPECT_EQ(PullServerTrailingStatus(), absl::OkStatus());
  EXPECT_EQ(NextSideStreamRequest(), std::nullopt);
  WaitForAllPendingWork();
}

FILTER_TEST(ExtProcFilterTest,
            StreamCleanCloseRequestBodyInFlightObservabilitySuccess) {
  Init(ConfigBuilder()
           .SetObservabilityMode(true)
           .SetRequestHeaderMode(false)
           .SetResponseHeaderMode(false)
           .SetRequestBodyMode(true));
  StartRpc();
  ASSERT_TRUE(WaitForHandler());
  ASSERT_NE(WaitForSideStream(), nullptr);
  PullRequiredClientInitialMetadata();
  PushClientMessage(NewMessage(kMessage1));
  EXPECT_THAT(NextSideStreamRequest(),
              Optional(MatchesRequestBody(kMessage1, !kEndOfStream)));
  CloseSideStream(absl::OkStatus());
  EXPECT_THAT(PullClientMessage(), IsMessage(kMessage1));
  PushServerInitialMetadata(NewServerMetadata());
  PushServerMessage(NewMessage(kMessage1));
  PullRequiredServerInitialMetadata();
  EXPECT_THAT(PullServerMessage(), IsMessage(kMessage1));
  PushClientMessage(NewMessage(kMessage2));
  EXPECT_THAT(PullClientMessage(), IsMessage(kMessage2));
  PushServerMessage(NewMessage(kMessage2));
  EXPECT_THAT(PullServerMessage(), IsMessage(kMessage2));
  PushClientHalfClose();
  EXPECT_THAT(PullClientMessage(), IsEndOfStream());
  PushServerTrailingMetadata(ServerMetadataFromStatus(GRPC_STATUS_OK));
  EXPECT_EQ(PullServerTrailingStatus(), absl::OkStatus());
  EXPECT_EQ(NextSideStreamRequest(), std::nullopt);
  WaitForAllPendingWork();
}

FILTER_TEST(ExtProcFilterTest,
            StreamCleanCloseResponseBodyInFlightObservabilitySuccess) {
  Init(ConfigBuilder()
           .SetObservabilityMode(true)
           .SetRequestHeaderMode(false)
           .SetResponseHeaderMode(false)
           .SetResponseTrailerMode(true)
           .SetResponseBodyMode(true));
  StartRpc();
  ASSERT_TRUE(WaitForHandler());
  ASSERT_NE(WaitForSideStream(), nullptr);
  PullRequiredClientInitialMetadata();
  PushClientMessage(NewMessage(kMessage1));
  EXPECT_THAT(PullClientMessage(), IsMessage(kMessage1));
  PushServerInitialMetadata(NewServerMetadata());
  PushServerMessage(NewMessage(kMessage1));
  EXPECT_THAT(NextSideStreamRequest(),
              Optional(MatchesResponseBody(kMessage1, !kEndOfStream)));
  CloseSideStream(absl::OkStatus());
  PullRequiredServerInitialMetadata();
  EXPECT_THAT(PullServerMessage(), IsMessage(kMessage1));
  PushClientHalfClose();
  EXPECT_THAT(PullClientMessage(), IsEndOfStream());
  PushServerTrailingMetadata(ServerMetadataFromStatus(GRPC_STATUS_OK));
  EXPECT_EQ(PullServerTrailingStatus(), absl::OkStatus());
  EXPECT_EQ(NextSideStreamRequest(), std::nullopt);
  WaitForAllPendingWork();
}

FILTER_TEST(ExtProcFilterTest, StreamCleanCloseRequestBodyNotDrainedFails) {
  Init(ConfigBuilder()
           .SetRequestHeaderMode(false)
           .SetResponseHeaderMode(false)
           .SetRequestBodyMode(true));
  StartRpc();
  ASSERT_TRUE(WaitForHandler());
  ASSERT_NE(WaitForSideStream(), nullptr);
  PushClientMessage(NewMessage(kMessage1));
  EXPECT_THAT(NextSideStreamRequest(),
              Optional(MatchesRequestBody(kMessage1, !kEndOfStream)));
  CloseSideStream(absl::OkStatus());
  PushClientHalfClose();
  EXPECT_EQ(PullServerTrailingStatus(),
            absl::InternalError("Stream closed cleanly without drain"));
  WaitForAllPendingWork();
}

FILTER_TEST(ExtProcFilterTest, StreamCleanCloseResponseBodyNotDrainedFails) {
  Init(ConfigBuilder()
           .SetRequestHeaderMode(false)
           .SetResponseHeaderMode(false)
           .SetResponseTrailerMode(true)
           .SetResponseBodyMode(true));
  StartRpc();
  ASSERT_TRUE(WaitForHandler());
  ASSERT_NE(WaitForSideStream(), nullptr);
  PullRequiredClientInitialMetadata();
  PushClientMessage(NewMessage(kMessage1));
  EXPECT_THAT(PullClientMessage(), IsMessage(kMessage1));
  PushServerInitialMetadata(NewServerMetadata());
  PushServerMessage(NewMessage(kMessage1));
  EXPECT_THAT(NextSideStreamRequest(),
              Optional(MatchesResponseBody(kMessage1, !kEndOfStream)));
  SendSideStreamResponse(MakeResponseBodyMutationResponse(kMessage1));
  PullRequiredServerInitialMetadata();
  EXPECT_THAT(PullServerMessage(), IsMessage(kMessage1));
  PushClientMessage(NewMessage(kMessage2));
  EXPECT_THAT(PullClientMessage(), IsMessage(kMessage2));
  PushServerMessage(NewMessage(kMessage2));
  EXPECT_THAT(NextSideStreamRequest(),
              Optional(MatchesResponseBody(kMessage2, !kEndOfStream)));
  CloseSideStream(absl::OkStatus());
  PushClientHalfClose();
  EXPECT_EQ(PullServerTrailingStatus(),
            absl::InternalError("Stream closed cleanly without drain"));
  WaitForAllPendingWork();
}

FILTER_TEST(ExtProcFilterTest, StreamCleanCloseBeforeBodySentDrainSuccess) {
  Init(ConfigBuilder()
           .SetRequestHeaderMode(true)
           .SetResponseHeaderMode(false)
           .SetResponseTrailerMode(true)
           .SetRequestBodyMode(true)
           .SetResponseBodyMode(true));
  StartRpc();
  ASSERT_NE(WaitForSideStream(), nullptr);
  EXPECT_THAT(NextSideStreamRequest(),
              Optional(MatchesRequestHeaders(Contains(Pair(":path", kPath)))));
  SendSideStreamResponse(
      MakeRequestHeadersMutationResponse({}, /*request_drain=*/true));
  ASSERT_TRUE(WaitForHandler());
  CloseSideStream(absl::OkStatus());
  // Let the filter see the side stream close before any body is sent.
  TickUntilIdle();
  PullRequiredClientInitialMetadata();
  PushClientMessage(NewMessage(kMessage1));
  EXPECT_THAT(PullClientMessage(), IsMessage(kMessage1));
  PushClientHalfClose();
  EXPECT_THAT(PullClientMessage(), IsEndOfStream());
  PushServerInitialMetadata(NewServerMetadata());
  PushServerMessage(NewMessage(kMessage1));
  PullRequiredServerInitialMetadata();
  EXPECT_THAT(PullServerMessage(), IsMessage(kMessage1));
  PushServerTrailingMetadata(ServerMetadataFromStatus(GRPC_STATUS_OK));
  EXPECT_EQ(PullServerTrailingStatus(), absl::OkStatus());
  EXPECT_EQ(NextSideStreamRequest(), std::nullopt);
  WaitForAllPendingWork();
}

//
// Stream error tests
//

FILTER_TEST(ExtProcFilterTest,
            StreamErrorFailureModeAllowBodiesNotConfiguredSuccess) {
  Init(ConfigBuilder()
           .SetFailureModeAllow(true)
           .SetRequestHeaderMode(true)
           .SetResponseHeaderMode(true));
  StartRpc();
  ASSERT_NE(WaitForSideStream(), nullptr);
  EXPECT_THAT(NextSideStreamRequest(),
              Optional(MatchesRequestHeaders(Contains(Pair(":path", kPath)))));
  CloseSideStream(
      absl::UnavailableError("Call closed by ext_proc server on headers"));
  ASSERT_TRUE(WaitForHandler());
  PullRequiredClientInitialMetadata();
  PushClientMessage(NewMessage(kMessage1));
  EXPECT_THAT(PullClientMessage(), IsMessage(kMessage1));
  PushClientHalfClose();
  EXPECT_THAT(PullClientMessage(), IsEndOfStream());
  PushServerInitialMetadata(NewServerMetadata());
  PushServerMessage(NewMessage(kMessage1));
  PullRequiredServerInitialMetadata();
  EXPECT_THAT(PullServerMessage(), IsMessage(kMessage1));
  PushServerTrailingMetadata(ServerMetadataFromStatus(GRPC_STATUS_OK));
  EXPECT_EQ(PullServerTrailingStatus(), absl::OkStatus());
  WaitForAllPendingWork();
}

FILTER_TEST(ExtProcFilterTest,
            StreamErrorFailureModeAllowObservabilitySuccess) {
  Init(ConfigBuilder()
           .SetObservabilityMode(true)
           .SetFailureModeAllow(true)
           .EnableAllProcessingModes());
  StartRpc();
  ASSERT_NE(WaitForSideStream(), nullptr);
  EXPECT_THAT(NextSideStreamRequest(),
              Optional(MatchesRequestHeaders(Contains(Pair(":path", kPath)))));
  ASSERT_TRUE(WaitForHandler());
  PullRequiredClientInitialMetadata();
  PushClientMessage(NewMessage(kMessage1));
  EXPECT_THAT(NextSideStreamRequest(),
              Optional(MatchesRequestBody(kMessage1, !kEndOfStream)));
  CloseSideStream(absl::ResourceExhaustedError(
      "Call closed by ext_proc server on request body"));
  EXPECT_THAT(PullClientMessage(), IsMessage(kMessage1));
  PushClientHalfClose();
  EXPECT_THAT(PullClientMessage(), IsEndOfStream());
  PushServerInitialMetadata(NewServerMetadata());
  PushServerMessage(NewMessage(kMessage1));
  PullRequiredServerInitialMetadata();
  EXPECT_THAT(PullServerMessage(), IsMessage(kMessage1));
  PushServerTrailingMetadata(ServerMetadataFromStatus(GRPC_STATUS_OK));
  EXPECT_EQ(PullServerTrailingStatus(), absl::OkStatus());
  WaitForAllPendingWork();
}

FILTER_TEST(ExtProcFilterTest,
            StreamErrorFailureModeAllowRequestBodyNotDrainedFails) {
  Init(ConfigBuilder()
           .SetFailureModeAllow(true)
           .SetRequestHeaderMode(false)
           .SetResponseHeaderMode(false)
           .SetRequestBodyMode(true));
  StartRpc();
  ASSERT_TRUE(WaitForHandler());
  ASSERT_NE(WaitForSideStream(), nullptr);
  PushClientMessage(NewMessage(kMessage1));
  EXPECT_THAT(NextSideStreamRequest(),
              Optional(MatchesRequestBody(kMessage1, !kEndOfStream)));
  // Fail-open doesn't apply once a body has been sent.
  CloseSideStream(absl::ResourceExhaustedError(
      "Call closed by ext_proc server on request body"));
  EXPECT_EQ(PullServerTrailingStatus(),
            absl::InternalError(
                "External processor stream failed: RESOURCE_EXHAUSTED: "
                "Call closed by ext_proc server on request body"));
  WaitForAllPendingWork();
}

FILTER_TEST(ExtProcFilterTest,
            StreamErrorFailureModeAllowResponseBodyNotDrainedFails) {
  Init(ConfigBuilder()
           .SetFailureModeAllow(true)
           .SetRequestHeaderMode(false)
           .SetResponseHeaderMode(false)
           .SetResponseTrailerMode(true)
           .SetResponseBodyMode(true));
  StartRpc();
  ASSERT_TRUE(WaitForHandler());
  ASSERT_NE(WaitForSideStream(), nullptr);
  PushServerInitialMetadata(NewServerMetadata());
  PushServerMessage(NewMessage(kMessage1));
  EXPECT_THAT(NextSideStreamRequest(),
              Optional(MatchesResponseBody(kMessage1, !kEndOfStream)));
  CloseSideStream(absl::ResourceExhaustedError(
      "Call closed by ext_proc server on response body"));
  EXPECT_EQ(PullServerTrailingStatus(),
            absl::InternalError(
                "External processor stream failed: RESOURCE_EXHAUSTED: "
                "Call closed by ext_proc server on response body"));
  WaitForAllPendingWork();
}

FILTER_TEST(ExtProcFilterTest,
            StreamErrorFailureModeAllowBodiesDrainedSuccess) {
  Init(ConfigBuilder()
           .SetFailureModeAllow(true)
           .SetRequestHeaderMode(true)
           .SetResponseHeaderMode(false)
           .SetResponseTrailerMode(true)
           .SetRequestBodyMode(true)
           .SetResponseBodyMode(true));
  StartRpc();
  ASSERT_NE(WaitForSideStream(), nullptr);
  EXPECT_THAT(NextSideStreamRequest(),
              Optional(MatchesRequestHeaders(Contains(Pair(":path", kPath)))));
  SendSideStreamResponse(
      MakeRequestHeadersMutationResponse({}, /*request_drain=*/true));
  ASSERT_TRUE(WaitForHandler());
  CloseSideStream(absl::ResourceExhaustedError(
      "Call closed by ext_proc server after drain"));
  PullRequiredClientInitialMetadata();
  PushClientMessage(NewMessage(kMessage1));
  EXPECT_THAT(PullClientMessage(), IsMessage(kMessage1));
  PushClientHalfClose();
  EXPECT_THAT(PullClientMessage(), IsEndOfStream());
  PushServerInitialMetadata(NewServerMetadata());
  PushServerMessage(NewMessage(kMessage1));
  PullRequiredServerInitialMetadata();
  EXPECT_THAT(PullServerMessage(), IsMessage(kMessage1));
  PushServerTrailingMetadata(ServerMetadataFromStatus(GRPC_STATUS_OK));
  EXPECT_EQ(PullServerTrailingStatus(), absl::OkStatus());
  EXPECT_EQ(NextSideStreamRequest(), std::nullopt);
  WaitForAllPendingWork();
}

FILTER_TEST(ExtProcFilterTest, StreamErrorFailureModeFalseFails) {
  Init(ConfigBuilder().SetRequestHeaderMode(true).SetResponseHeaderMode(false));
  StartRpc();
  ASSERT_NE(WaitForSideStream(), nullptr);
  EXPECT_THAT(NextSideStreamRequest(),
              Optional(MatchesRequestHeaders(Contains(Pair(":path", kPath)))));
  CloseSideStream(absl::UnavailableError(
      "Call closed by ext_proc server on request headers"));
  EXPECT_EQ(
      PullServerTrailingStatus(),
      absl::InternalError("External processor stream failed: UNAVAILABLE: Call "
                          "closed by ext_proc server on request headers"));
  WaitForAllPendingWork();
}

//
// Metrics tests
//

FILTER_TEST(ExtProcFilterTest, ExtProcClientHeadersDurationMetric) {
  auto stats_plugin = InitWithStatsPlugin(
      ConfigBuilder().SetRequestHeaderMode(true).SetResponseHeaderMode(false));
  StartRpc();
  ASSERT_NE(WaitForSideStream(), nullptr);
  EXPECT_THAT(NextSideStreamRequest(),
              Optional(MatchesRequestHeaders(Contains(Pair(":path", kPath)))));
  // The filter waits for the ext_proc server's response.
  AdvanceTime(std::chrono::seconds(2));
  SendSideStreamResponse(MakeRequestHeadersMutationResponse({}));
  ASSERT_TRUE(WaitForHandler());
  PullRequiredClientInitialMetadata();
  PushServerInitialMetadata(NewServerMetadata());
  PullRequiredServerInitialMetadata();
  PushServerTrailingMetadata(ServerMetadataFromStatus(GRPC_STATUS_OK));
  EXPECT_EQ(PullServerTrailingStatus(), absl::OkStatus());
  EXPECT_THAT(stats_plugin->GetHistogramValueByName(
                  "grpc.client_ext_proc.client_headers_duration", {kServerUri}),
              HasOneSampleOverOneSecond());
  WaitForAllPendingWork();
}

FILTER_TEST(ExtProcFilterTest, ExtProcClientHalfCloseDurationMetric) {
  auto stats_plugin = InitWithStatsPlugin(ConfigBuilder()
                                              .SetRequestHeaderMode(false)
                                              .SetResponseHeaderMode(false)
                                              .SetRequestBodyMode(true));
  StartRpc();
  ASSERT_TRUE(WaitForHandler());
  ASSERT_NE(WaitForSideStream(), nullptr);
  PullRequiredClientInitialMetadata();
  PushClientMessage(NewMessage(kMessage1));
  EXPECT_THAT(NextSideStreamRequest(),
              Optional(MatchesRequestBody(kMessage1, !kEndOfStream)));
  SendSideStreamResponse(MakeRequestBodyMutationResponse(kMessage1));
  EXPECT_THAT(PullClientMessage(), IsMessage(kMessage1));
  PushClientHalfClose();
  EXPECT_THAT(NextSideStreamRequest(),
              Optional(MatchesRequestBody(kEmptyBody, kEndOfStream)));
  // The filter waits for the ext_proc server's response.
  AdvanceTime(std::chrono::seconds(2));
  SendSideStreamResponse(MakeRequestHalfCloseResponse());
  EXPECT_THAT(PullClientMessage(), IsEndOfStream());
  PushServerInitialMetadata(NewServerMetadata());
  PullRequiredServerInitialMetadata();
  PushServerTrailingMetadata(ServerMetadataFromStatus(GRPC_STATUS_OK));
  EXPECT_EQ(PullServerTrailingStatus(), absl::OkStatus());
  EXPECT_THAT(
      stats_plugin->GetHistogramValueByName(
          "grpc.client_ext_proc.client_half_close_duration", {kServerUri}),
      HasOneSampleOverOneSecond());
  WaitForAllPendingWork();
}

FILTER_TEST(ExtProcFilterTest, ExtProcServerHeadersDurationMetric) {
  auto stats_plugin = InitWithStatsPlugin(
      ConfigBuilder().SetRequestHeaderMode(false).SetResponseHeaderMode(true));
  StartRpc();
  ASSERT_TRUE(WaitForHandler());
  ASSERT_NE(WaitForSideStream(), nullptr);
  PushServerInitialMetadata(NewServerMetadata());
  EXPECT_THAT(NextSideStreamRequest(), Optional(MatchesResponseHeaders(_)));
  // The filter waits for the ext_proc server's response.
  AdvanceTime(std::chrono::seconds(2));
  SendSideStreamResponse(MakeResponseHeadersMutationResponse({}));
  PullRequiredServerInitialMetadata();
  PushServerTrailingMetadata(ServerMetadataFromStatus(GRPC_STATUS_OK));
  EXPECT_EQ(PullServerTrailingStatus(), absl::OkStatus());
  EXPECT_THAT(stats_plugin->GetHistogramValueByName(
                  "grpc.client_ext_proc.server_headers_duration", {kServerUri}),
              HasOneSampleOverOneSecond());
  WaitForAllPendingWork();
}

FILTER_TEST(ExtProcFilterTest, ExtProcServerTrailersDurationMetric) {
  auto stats_plugin = InitWithStatsPlugin(ConfigBuilder()
                                              .SetRequestHeaderMode(false)
                                              .SetResponseHeaderMode(false)
                                              .SetResponseTrailerMode(true));
  StartRpc();
  ASSERT_TRUE(WaitForHandler());
  ASSERT_NE(WaitForSideStream(), nullptr);
  PushServerInitialMetadata(NewServerMetadata());
  PullRequiredServerInitialMetadata();
  PushServerTrailingMetadata(ServerMetadataFromStatus(GRPC_STATUS_OK));
  EXPECT_THAT(NextSideStreamRequest(), Optional(MatchesResponseTrailers(_)));
  // The filter waits for the ext_proc server's response.
  AdvanceTime(std::chrono::seconds(2));
  SendSideStreamResponse(MakeResponseTrailersMutationResponse({}));
  EXPECT_EQ(PullServerTrailingStatus(), absl::OkStatus());
  EXPECT_THAT(
      stats_plugin->GetHistogramValueByName(
          "grpc.client_ext_proc.server_trailers_duration", {kServerUri}),
      HasOneSampleOverOneSecond());
  WaitForAllPendingWork();
}

}  // namespace
}  // namespace grpc_core
