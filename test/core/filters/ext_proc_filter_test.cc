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

#include "src/core/filter/ext_proc/ext_proc_filter.h"

#include <grpc/status.h>
#include <grpc/support/metrics.h>

#include <memory>
#include <optional>
#include <string>
#include <utility>

#include "envoy/service/ext_proc/v3/external_processor.pb.h"
#include "src/core/call/call_spine.h"
#include "src/core/call/metadata.h"
#include "src/core/client_channel/client_channel_args.h"
#include "src/core/filter/filter_args.h"
#include "src/core/handshaker/endpoint_info/endpoint_info_handshaker.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/event_engine/channel_args_endpoint_config.h"
#include "src/core/telemetry/metrics.h"
#include "src/core/util/ref_counted_ptr.h"
#include "src/core/util/time.h"
#include "src/core/xds/grpc/xds_server_grpc.h"
#include "test/core/filters/ext_proc_test_util.h"
#include "test/core/filters/filter_matchers.h"
#include "test/core/filters/filter_test.h"
#include "test/core/test_util/fake_stats_plugin.h"
#include "test/core/xds/xds_transport_fake.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
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
using ::testing::ElementsAre;
using ::testing::Not;
using ::testing::Optional;
using ::testing::Pair;
using ::testing::Pointee;

//
// Test fixture
//

class ExtProcFilterTest : public FilterTest {
 protected:
  using FilterTest::FilterTest;

  // Builds an ExtProcFilter::Config directly, bypassing xDS parsing.
  // Setters mirror the fields of the envoy ExternalProcessor proto. Unset
  // fields take the values the xDS parser would produce for an unset proto
  // field: in particular, the processing mode defaults to sending request and
  // response headers only.
  class ConfigBuilder {
   public:
    ConfigBuilder() : config_(MakeRefCounted<ExtProcFilter::Config>()) {
      config_->failure_mode_allow = false;
      config_->processing_mode.emplace();
      config_->disable_immediate_response = false;
      config_->observability_mode = false;
    }

    ConfigBuilder& SetFailureModeAllow(bool allow) {
      config_->failure_mode_allow = allow;
      return *this;
    }
    ConfigBuilder& SetRequestHeaderMode(bool send) {
      config_->processing_mode->send_request_headers = send;
      return *this;
    }
    ConfigBuilder& SetResponseHeaderMode(bool send) {
      config_->processing_mode->send_response_headers = send;
      return *this;
    }
    ConfigBuilder& SetResponseTrailerMode(bool send) {
      config_->processing_mode->send_response_trailers = send;
      return *this;
    }
    ConfigBuilder& SetRequestBodyMode(bool send) {
      config_->processing_mode->send_request_body = send;
      return *this;
    }
    ConfigBuilder& SetResponseBodyMode(bool send) {
      config_->processing_mode->send_response_body = send;
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
      config_->request_attributes.push_back(std::move(attribute));
      return *this;
    }
    ConfigBuilder& SetDisableImmediateResponse(bool disable) {
      config_->disable_immediate_response = disable;
      return *this;
    }
    ConfigBuilder& SetObservabilityMode(bool observability_mode) {
      config_->observability_mode = observability_mode;
      return *this;
    }
    ConfigBuilder& SetDeferredCloseTimeout(Duration timeout) {
      config_->deferred_close_timeout = timeout;
      return *this;
    }

    RefCountedPtr<ExtProcFilter::Config> Build(
        RefCountedPtr<ExtProcFilter::ExtProcChannel> channel) {
      config_->channel_info = std::move(channel);
      return std::move(config_);
    }

   private:
    RefCountedPtr<ExtProcFilter::Config> config_;
  };

  // Returns the value of `attribute_name` from the ext_proc attributes of
  // `request`, or "" if it is not set.
  std::string GetExtProcAttribute(const ProcessingRequest& request,
                                  absl::string_view attribute_name) {
    auto it = request.attributes().find("envoy.filters.http.ext_proc");
    if (it == request.attributes().end()) return "";
    const auto& fields = it->second.fields();
    auto field_it = fields.find(std::string(attribute_name));
    if (field_it == fields.end()) return "";
    return field_it->second.string_value();
  }

  // Builds the chain [ExtProcFilter] -> backend, with the filter's side
  // stream going to a fake transport.
  void Init(ConfigBuilder builder, ChannelArgs args = ChannelArgs()) {
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
  // (or server, if `args` sets GRPC_ARG_IS_SERVER_FILTER_STACK) and returns it.
  std::shared_ptr<FakeStatsPlugin> InitWithStatsPlugin(
      ConfigBuilder builder, ChannelArgs args = ChannelArgs()) {
    auto stats_plugin = FakeStatsPluginBuilder()
                            .UseDisabledByDefaultMetrics(true)
                            .BuildAndRegister();
    if (args.GetBool(GRPC_ARG_IS_SERVER_FILTER_STACK).value_or(false)) {
      stats_plugin_group_ =
          GlobalStatsPluginRegistry::GetStatsPluginsForServer(args);
    } else {
      stats_plugin_group_ =
          GlobalStatsPluginRegistry::GetStatsPluginsForChannel(
              experimental::StatsPluginChannelScope(
                  kServerUri, /*default_authority=*/"",
                  grpc_event_engine::experimental::ChannelArgsEndpointConfig(
                      args)));
    }
    Init(std::move(builder), args.SetObject(stats_plugin_group_));
    return stats_plugin;
  }

  //
  // Side stream (fake ext_proc server)
  //

  // Returns the next ProcessingRequest the filter sent on `side_stream`, or
  // nullopt if it doesn't send one.
  std::optional<ProcessingRequest> GetNextSideStreamRequest(
      FakeXdsTransportFactory::FakeStreamingCall& side_stream) {
    std::optional<std::string> payload = side_stream.WaitForMessageFromClient();
    if (!payload.has_value()) return std::nullopt;
    ProcessingRequest request;
    if (!request.ParseFromString(*payload)) {
      ADD_FAILURE() << "failed to parse ProcessingRequest from side stream";
      return std::nullopt;
    }
    return request;
  }

  // Drops everything holding a ref to event_engine() before YodelTest waits
  // for it to have a single owner. Runs after FilterTest has torn down the
  // stack under test.
  void Cleanup() override {
    if (transport_factory_ != nullptr) {
      // A real ext_proc server would see the side stream cancelled and finish
      // it; the fake never does so on its own. Finish it here so the filter
      // drops the side call, which holds references to the fake transport
      // and, through it, to event_engine().
      auto side_stream =
          transport_factory_->WaitForStream(target_, kExtProcMethod);
      if (side_stream != nullptr) {
        side_stream->MaybeSendStatusToClient(absl::CancelledError());
      }
    }
    transport_factory_.reset();
    if (stats_plugin_group_ != nullptr) {
      stats_plugin_group_.reset();
      GlobalStatsPluginRegistryTestPeer::ResetGlobalStatsPluginRegistry();
    }
  }

  // The side stream (fake ext_proc server) is opened on this transport
  // factory: tests wait for it with
  // transport_factory_->WaitForStream(target_, kExtProcMethod), which
  // returns null if the filter doesn't open one.
  const GrpcXdsServerTarget target_{"dns:///ext_proc.example.com",
                                    /*channel_creds_config=*/nullptr,
                                    /*call_creds_configs=*/{}};
  RefCountedPtr<FakeXdsTransportFactory> transport_factory_;

 private:
  // Set by InitWithStatsPlugin(). Held for the whole test, as a channel holds
  // it via its args: the filter's metrics are only visible to the stats plugin
  // while the group's collection scope is alive.
  std::shared_ptr<GlobalStatsPluginRegistry::StatsPluginGroup>
      stats_plugin_group_;
};

//
// Processing mode tests
//

FILTER_TEST(ExtProcFilterTest, ProcessingModeAllDisabledSuccess) {
  Init(
      ConfigBuilder().SetRequestHeaderMode(false).SetResponseHeaderMode(false));
  CallInitiator client = StartCall(NewClientMetadata({}, kPath));
  std::optional<CallHandler> backend = GetNextHandler();
  ASSERT_TRUE(backend.has_value());
  EXPECT_EQ(transport_factory_->WaitForStream(target_, kExtProcMethod),
            nullptr);
  EXPECT_TRUE(PullClientInitialMetadata(*backend).ok());
  PushClientMessage(client, NewMessage(kMessage1));
  EXPECT_THAT(PullClientMessage(*backend), IsMessage(kMessage1));
  PushClientHalfClose(client);
  EXPECT_THAT(PullClientMessage(*backend), IsEndOfStream());
  PushServerInitialMetadata(*backend, NewServerMetadata());
  PushServerMessage(*backend, NewMessage(kMessage1));
  EXPECT_TRUE(PullServerInitialMetadata(client).ok());
  EXPECT_THAT(PullServerMessage(client), IsMessage(kMessage1));
  PushServerTrailingMetadata(*backend,
                             ServerMetadataFromStatus(GRPC_STATUS_OK));
  EXPECT_EQ(PullServerTrailingStatus(client), absl::OkStatus());
  WaitForAllPendingWork();
}

// processing_mode unset in the xDS config yields the default ProcessingMode:
// request and response headers are sent.
FILTER_TEST(ExtProcFilterTest, ProcessingModeNotSetSuccess) {
  Init(ConfigBuilder());
  CallInitiator client = StartCall(NewClientMetadata({}, kPath));
  auto side_stream = transport_factory_->WaitForStream(target_, kExtProcMethod);
  ASSERT_NE(side_stream, nullptr);
  // Request headers.
  EXPECT_THAT(GetNextSideStreamRequest(*side_stream),
              Optional(MatchesRequestHeaders(Contains(Pair(":path", kPath)))));
  side_stream->SendMessageToClient(
      MakeRequestHeadersMutationResponse(
          {{kRequestHeadersMutatedHeaderKey, kHeaderMutatedValue}})
          .SerializeAsString());
  std::optional<CallHandler> backend = GetNextHandler();
  ASSERT_TRUE(backend.has_value());
  EXPECT_THAT(PullClientInitialMetadata(*backend),
              IsOkAndHolds(Pointee(HasMetadataKeyValue(
                  kRequestHeadersMutatedHeaderKey, kHeaderMutatedValue))));
  // Request body is not sent.
  PushClientMessage(client, NewMessage(kMessage1));
  EXPECT_THAT(PullClientMessage(*backend), IsMessage(kMessage1));
  PushClientHalfClose(client);
  EXPECT_THAT(PullClientMessage(*backend), IsEndOfStream());
  // Response headers.
  PushServerInitialMetadata(*backend, NewServerMetadata());
  EXPECT_THAT(GetNextSideStreamRequest(*side_stream),
              Optional(MatchesResponseHeaders(_)));
  side_stream->SendMessageToClient(
      MakeResponseHeadersMutationResponse(
          {{kResponseHeadersMutatedHeaderKey, kHeaderMutatedValue}})
          .SerializeAsString());
  EXPECT_THAT(PullServerInitialMetadata(client),
              IsOkAndHolds(Optional(Pointee(HasMetadataKeyValue(
                  kResponseHeadersMutatedHeaderKey, kHeaderMutatedValue)))));
  // Response body and trailers are not sent.
  PushServerMessage(*backend, NewMessage(kMessage1));
  EXPECT_THAT(PullServerMessage(client), IsMessage(kMessage1));
  PushServerTrailingMetadata(*backend,
                             ServerMetadataFromStatus(GRPC_STATUS_OK));
  EXPECT_EQ(PullServerTrailingStatus(client), absl::OkStatus());
  EXPECT_EQ(GetNextSideStreamRequest(*side_stream), std::nullopt);
  WaitForAllPendingWork();
}

FILTER_TEST(ExtProcFilterTest, ProcessingModeAllEnabledSuccess) {
  Init(ConfigBuilder().EnableAllProcessingModes());
  CallInitiator client = StartCall(NewClientMetadata({}, kPath));
  auto side_stream = transport_factory_->WaitForStream(target_, kExtProcMethod);
  ASSERT_NE(side_stream, nullptr);
  // Request headers.
  EXPECT_THAT(GetNextSideStreamRequest(*side_stream),
              Optional(MatchesRequestHeaders(Contains(Pair(":path", kPath)))));
  side_stream->SendMessageToClient(
      MakeRequestHeadersMutationResponse(
          {{kRequestHeadersMutatedHeaderKey, kHeaderMutatedValue}})
          .SerializeAsString());
  std::optional<CallHandler> backend = GetNextHandler();
  ASSERT_TRUE(backend.has_value());
  EXPECT_THAT(PullClientInitialMetadata(*backend),
              IsOkAndHolds(Pointee(HasMetadataKeyValue(
                  kRequestHeadersMutatedHeaderKey, kHeaderMutatedValue))));
  // Request body.
  PushClientMessage(client, NewMessage(kMessage1));
  EXPECT_THAT(GetNextSideStreamRequest(*side_stream),
              Optional(MatchesRequestBody(kMessage1, !kEndOfStream)));
  side_stream->SendMessageToClient(
      MakeRequestBodyMutationResponse(kMessage1Mutated).SerializeAsString());
  EXPECT_THAT(PullClientMessage(*backend), IsMessage(kMessage1Mutated));
  // Client half-close.
  PushClientHalfClose(client);
  EXPECT_THAT(GetNextSideStreamRequest(*side_stream),
              Optional(MatchesRequestBody(kEmptyBody, kEndOfStream)));
  side_stream->SendMessageToClient(
      MakeRequestBodyMutationResponse(kEmptyBody, kEndOfStream)
          .SerializeAsString());
  EXPECT_THAT(PullClientMessage(*backend), IsMessage(kEmptyBody));
  EXPECT_THAT(PullClientMessage(*backend), IsEndOfStream());
  // Response headers.
  PushServerInitialMetadata(*backend, NewServerMetadata());
  EXPECT_THAT(GetNextSideStreamRequest(*side_stream),
              Optional(MatchesResponseHeaders(_)));
  side_stream->SendMessageToClient(
      MakeResponseHeadersMutationResponse(
          {{kResponseHeadersMutatedHeaderKey, kHeaderMutatedValue}})
          .SerializeAsString());
  // Response body.
  PushServerMessage(*backend, NewMessage(kMessage1Mutated));
  EXPECT_THAT(GetNextSideStreamRequest(*side_stream),
              Optional(MatchesResponseBody(kMessage1Mutated, !kEndOfStream)));
  side_stream->SendMessageToClient(
      MakeResponseBodyMutationResponse(kMessage1DoubleMutated)
          .SerializeAsString());
  // Response trailers.
  PushServerTrailingMetadata(*backend,
                             ServerMetadataFromStatus(GRPC_STATUS_OK));
  EXPECT_THAT(GetNextSideStreamRequest(*side_stream),
              Optional(MatchesResponseTrailers(_)));
  side_stream->SendMessageToClient(
      MakeResponseTrailersMutationResponse(
          {{kResponseTrailersMutatedHeaderKey, kHeaderMutatedValue}})
          .SerializeAsString());
  // The client sees all of the response mutations.
  EXPECT_THAT(PullServerInitialMetadata(client),
              IsOkAndHolds(Optional(Pointee(HasMetadataKeyValue(
                  kResponseHeadersMutatedHeaderKey, kHeaderMutatedValue)))));
  EXPECT_THAT(PullServerMessage(client), IsMessage(kMessage1DoubleMutated));
  EXPECT_THAT(PullServerTrailingMetadata(client),
              IsOkAndHolds(Pointee(
                  AllOf(HasMetadataResult(absl::OkStatus()),
                        HasMetadataKeyValue(kResponseTrailersMutatedHeaderKey,
                                            kHeaderMutatedValue)))));
  WaitForAllPendingWork();
}

FILTER_TEST(ExtProcFilterTest,
            ProcessingModeAllEnabledWithObservabilityModeSuccess) {
  Init(ConfigBuilder()
           .SetObservabilityMode(true)
           .SetDeferredCloseTimeout(Duration::Seconds(1))
           .EnableAllProcessingModes());
  CallInitiator client = StartCall(
      NewClientMetadata({{kCustomHeaderKey, kCustomHeaderValue}}, kPath));
  auto side_stream = transport_factory_->WaitForStream(target_, kExtProcMethod);
  ASSERT_NE(side_stream, nullptr);
  // Every event is sent to the ext_proc server, but its responses are
  // ignored and nothing waits for them.
  // Request headers.
  EXPECT_THAT(GetNextSideStreamRequest(*side_stream),
              Optional(MatchesRequestHeaders(AllOf(
                  Contains(Pair(":path", kPath)),
                  Contains(Pair(kCustomHeaderKey, kCustomHeaderValue))))));
  side_stream->SendMessageToClient(
      MakeRequestHeadersMutationResponse(
          {{kRequestHeadersMutatedHeaderKey, kHeaderMutatedValue}})
          .SerializeAsString());
  std::optional<CallHandler> backend = GetNextHandler();
  ASSERT_TRUE(backend.has_value());
  EXPECT_THAT(PullClientInitialMetadata(*backend),
              IsOkAndHolds(Pointee(AllOf(
                  HasMetadataKeyValue(kCustomHeaderKey, kCustomHeaderValue),
                  LacksMetadataKey(kRequestHeadersMutatedHeaderKey)))));
  // Request body.
  PushClientMessage(client, NewMessage(kMessage1));
  EXPECT_THAT(GetNextSideStreamRequest(*side_stream),
              Optional(MatchesRequestBody(kMessage1, !kEndOfStream)));
  side_stream->SendMessageToClient(
      MakeRequestBodyMutationResponse(kMessage1Mutated).SerializeAsString());
  EXPECT_THAT(PullClientMessage(*backend), IsMessage(kMessage1));
  // Client half-close.
  PushClientHalfClose(client);
  EXPECT_THAT(GetNextSideStreamRequest(*side_stream),
              Optional(MatchesRequestBody(kEmptyBody, kEndOfStream)));
  side_stream->SendMessageToClient(
      MakeRequestBodyMutationResponse(kEmptyBody, kEndOfStream)
          .SerializeAsString());
  EXPECT_THAT(PullClientMessage(*backend), IsEndOfStream());
  // Response headers.
  PushServerInitialMetadata(
      *backend, NewServerMetadata({{kCustomHeaderKey, kCustomHeaderValue}}));
  EXPECT_THAT(GetNextSideStreamRequest(*side_stream),
              Optional(MatchesResponseHeaders(AllOf(
                  Contains(Pair(kCustomHeaderKey, kCustomHeaderValue)),
                  Not(Contains(Pair(kRequestHeadersMutatedHeaderKey, _)))))));
  side_stream->SendMessageToClient(
      MakeResponseHeadersMutationResponse(
          {{kResponseHeadersMutatedHeaderKey, kHeaderMutatedValue}})
          .SerializeAsString());
  EXPECT_THAT(PullServerInitialMetadata(client),
              IsOkAndHolds(Optional(Pointee(AllOf(
                  HasMetadataKeyValue(kCustomHeaderKey, kCustomHeaderValue),
                  LacksMetadataKey(kResponseHeadersMutatedHeaderKey))))));
  // Response body.
  PushServerMessage(*backend, NewMessage(kMessage1));
  EXPECT_THAT(GetNextSideStreamRequest(*side_stream),
              Optional(MatchesResponseBody(kMessage1, !kEndOfStream)));
  side_stream->SendMessageToClient(
      MakeResponseBodyMutationResponse(kMessage1Mutated).SerializeAsString());
  EXPECT_THAT(PullServerMessage(client), IsMessage(kMessage1));
  // Response trailers.
  PushServerTrailingMetadata(*backend,
                             ServerMetadataFromStatus(GRPC_STATUS_OK));
  EXPECT_THAT(GetNextSideStreamRequest(*side_stream),
              Optional(MatchesResponseTrailers(_)));
  side_stream->SendMessageToClient(
      MakeResponseTrailersMutationResponse(
          {{kResponseTrailersMutatedHeaderKey, kHeaderMutatedValue}})
          .SerializeAsString());
  EXPECT_THAT(PullServerTrailingMetadata(client),
              IsOkAndHolds(Pointee(
                  AllOf(HasMetadataResult(absl::OkStatus()),
                        LacksMetadataKey(kResponseTrailersMutatedHeaderKey)))));
  WaitForAllPendingWork();
}

//
// Trailers-only tests
//

FILTER_TEST(ExtProcFilterTest, TrailersOnlyProcessingModeAllEnabled) {
  Init(ConfigBuilder().EnableAllProcessingModes());
  CallInitiator client = StartCall(NewClientMetadata({}, kPath));
  auto side_stream = transport_factory_->WaitForStream(target_, kExtProcMethod);
  ASSERT_NE(side_stream, nullptr);
  EXPECT_THAT(GetNextSideStreamRequest(*side_stream),
              Optional(MatchesRequestHeaders(Contains(Pair(":path", kPath)))));
  side_stream->SendMessageToClient(
      MakeRequestHeadersMutationResponse(
          {{kRequestHeadersMutatedHeaderKey, kHeaderMutatedValue}})
          .SerializeAsString());
  std::optional<CallHandler> backend = GetNextHandler();
  ASSERT_TRUE(backend.has_value());
  EXPECT_TRUE(PullClientInitialMetadata(*backend).ok());
  PushClientMessage(client, NewMessage(kMessage1));
  EXPECT_THAT(GetNextSideStreamRequest(*side_stream),
              Optional(MatchesRequestBody(kMessage1, !kEndOfStream)));
  side_stream->SendMessageToClient(
      MakeRequestBodyMutationResponse(kMessage1).SerializeAsString());
  EXPECT_THAT(PullClientMessage(*backend), IsMessage(kMessage1));
  PushClientHalfClose(client);
  EXPECT_THAT(GetNextSideStreamRequest(*side_stream),
              Optional(MatchesRequestBody(kEmptyBody, kEndOfStream)));
  side_stream->SendMessageToClient(
      MakeRequestBodyMutationResponse(kEmptyBody, kEndOfStream)
          .SerializeAsString());
  EXPECT_THAT(PullClientMessage(*backend), IsMessage(kEmptyBody));
  EXPECT_THAT(PullClientMessage(*backend), IsEndOfStream());
  // The backend fails the call with a trailers-only response, which the
  // filter sends to the ext_proc server as response headers.
  PushServerTrailingMetadata(
      *backend, ServerMetadataFromStatus(GRPC_STATUS_FAILED_PRECONDITION));
  EXPECT_THAT(GetNextSideStreamRequest(*side_stream),
              Optional(MatchesResponseHeaders(_, kEndOfStream)));
  side_stream->SendMessageToClient(
      MakeResponseHeadersMutationResponse(
          {{kResponseHeadersMutatedHeaderKey, kHeaderMutatedValue}})
          .SerializeAsString());
  // No further events are sent to the ext_proc server.
  EXPECT_EQ(GetNextSideStreamRequest(*side_stream), std::nullopt);
  EXPECT_THAT(PullServerTrailingMetadata(client),
              IsOkAndHolds(Pointee(
                  AllOf(HasMetadataResult(absl::FailedPreconditionError("")),
                        HasMetadataKeyValue(kResponseHeadersMutatedHeaderKey,
                                            kHeaderMutatedValue)))));
  WaitForAllPendingWork();
}

FILTER_TEST(ExtProcFilterTest,
            TrailersOnlyProcessingModeAllEnabledWithObservabilityMode) {
  Init(ConfigBuilder().SetObservabilityMode(true).EnableAllProcessingModes());
  CallInitiator client = StartCall(NewClientMetadata({}, kPath));
  auto side_stream = transport_factory_->WaitForStream(target_, kExtProcMethod);
  ASSERT_NE(side_stream, nullptr);
  EXPECT_THAT(GetNextSideStreamRequest(*side_stream),
              Optional(MatchesRequestHeaders(Contains(Pair(":path", kPath)))));
  std::optional<CallHandler> backend = GetNextHandler();
  ASSERT_TRUE(backend.has_value());
  EXPECT_TRUE(PullClientInitialMetadata(*backend).ok());
  PushClientMessage(client, NewMessage(kMessage1));
  EXPECT_THAT(GetNextSideStreamRequest(*side_stream),
              Optional(MatchesRequestBody(kMessage1, !kEndOfStream)));
  EXPECT_THAT(PullClientMessage(*backend), IsMessage(kMessage1));
  PushClientHalfClose(client);
  EXPECT_THAT(GetNextSideStreamRequest(*side_stream),
              Optional(MatchesRequestBody(kEmptyBody, kEndOfStream)));
  EXPECT_THAT(PullClientMessage(*backend), IsEndOfStream());
  // Response headers are sent in observability mode even for a
  // trailers-only response.
  PushServerTrailingMetadata(
      *backend, ServerMetadataFromStatus(GRPC_STATUS_FAILED_PRECONDITION));
  EXPECT_THAT(GetNextSideStreamRequest(*side_stream),
              Optional(MatchesResponseHeaders(_, kEndOfStream)));
  EXPECT_EQ(GetNextSideStreamRequest(*side_stream), std::nullopt);
  EXPECT_EQ(PullServerTrailingStatus(client),
            absl::FailedPreconditionError(""));
  WaitForAllPendingWork();
}

//
// Request headers / body tests
//

FILTER_TEST(ExtProcFilterTest, ContinueAndReplaceFails) {
  Init(ConfigBuilder().EnableAllProcessingModes());
  CallInitiator client = StartCall(NewClientMetadata({}, kPath));
  auto side_stream = transport_factory_->WaitForStream(target_, kExtProcMethod);
  ASSERT_NE(side_stream, nullptr);
  EXPECT_THAT(GetNextSideStreamRequest(*side_stream),
              Optional(MatchesRequestHeaders(Contains(Pair(":path", kPath)))));
  ProcessingResponse response;
  response.mutable_request_headers()->mutable_response()->set_status(
      ::envoy::service::ext_proc::v3::CommonResponse::CONTINUE_AND_REPLACE);
  side_stream->SendMessageToClient(response.SerializeAsString());
  EXPECT_EQ(PullServerTrailingStatus(client),
            absl::InternalError("CONTINUE_AND_REPLACE is not supported"));
  WaitForAllPendingWork();
}

FILTER_TEST(ExtProcFilterTest, RequestHeadersInvalidHeaderMutationFails) {
  Init(ConfigBuilder().EnableAllProcessingModes());
  CallInitiator client = StartCall(NewClientMetadata({}, kPath));
  auto side_stream = transport_factory_->WaitForStream(target_, kExtProcMethod);
  ASSERT_NE(side_stream, nullptr);
  EXPECT_THAT(GetNextSideStreamRequest(*side_stream),
              Optional(MatchesRequestHeaders(Contains(Pair(":path", kPath)))));
  side_stream->SendMessageToClient(
      MakeRequestHeadersMutationResponse({{"host", "invalid-host"}})
          .SerializeAsString());
  EXPECT_EQ(PullServerTrailingStatus(client),
            absl::InternalError(
                "Failed to parse XdsHeaderValueOption: [field:header.key "
                "error:header \"host\" not allowed]"));
  WaitForAllPendingWork();
}

FILTER_TEST(ExtProcFilterTest, RequestHeadersMutationRemovesHeader) {
  Init(ConfigBuilder().SetRequestHeaderMode(true).SetResponseHeaderMode(false));
  CallInitiator client = StartCall(
      NewClientMetadata({{kCustomHeaderKey, kCustomHeaderValue}}, kPath));
  auto side_stream = transport_factory_->WaitForStream(target_, kExtProcMethod);
  ASSERT_NE(side_stream, nullptr);
  EXPECT_THAT(GetNextSideStreamRequest(*side_stream),
              Optional(MatchesRequestHeaders(
                  Contains(Pair(kCustomHeaderKey, kCustomHeaderValue)))));
  side_stream->SendMessageToClient(
      MakeRequestHeadersMutationResponse(
          {{kRequestHeadersMutatedHeaderKey, kHeaderMutatedValue}},
          /*remove_headers=*/{kCustomHeaderKey})
          .SerializeAsString());
  std::optional<CallHandler> backend = GetNextHandler();
  ASSERT_TRUE(backend.has_value());
  EXPECT_THAT(PullClientInitialMetadata(*backend),
              IsOkAndHolds(Pointee(
                  AllOf(LacksMetadataKey(kCustomHeaderKey),
                        HasMetadataKeyValue(kRequestHeadersMutatedHeaderKey,
                                            kHeaderMutatedValue)))));
  PushClientHalfClose(client);
  EXPECT_THAT(PullClientMessage(*backend), IsEndOfStream());
  PushServerInitialMetadata(*backend, NewServerMetadata());
  EXPECT_TRUE(PullServerInitialMetadata(client).ok());
  PushServerTrailingMetadata(*backend,
                             ServerMetadataFromStatus(GRPC_STATUS_OK));
  EXPECT_EQ(PullServerTrailingStatus(client), absl::OkStatus());
  EXPECT_EQ(GetNextSideStreamRequest(*side_stream), std::nullopt);
  WaitForAllPendingWork();
}

FILTER_TEST(ExtProcFilterTest, RequestHeadersRequestAttributesSent) {
  Init(ConfigBuilder()
           .SetRequestHeaderMode(true)
           .SetResponseHeaderMode(false)
           .AddRequestAttribute("request.path")
           .AddRequestAttribute("request.method"));
  CallInitiator client = StartCall(NewClientMetadata({}, kPath));
  auto side_stream = transport_factory_->WaitForStream(target_, kExtProcMethod);
  ASSERT_NE(side_stream, nullptr);
  auto request = GetNextSideStreamRequest(*side_stream);
  ASSERT_THAT(request,
              Optional(MatchesRequestHeaders(Contains(Pair(":path", kPath)))));
  EXPECT_EQ(GetExtProcAttribute(*request, "request.path"), kPath);
  // The method is not in the metadata, so the filter falls back to POST.
  EXPECT_EQ(GetExtProcAttribute(*request, "request.method"), "POST");
  side_stream->SendMessageToClient(
      MakeRequestHeadersMutationResponse({}).SerializeAsString());
  std::optional<CallHandler> backend = GetNextHandler();
  ASSERT_TRUE(backend.has_value());
  EXPECT_TRUE(PullClientInitialMetadata(*backend).ok());
  PushServerInitialMetadata(*backend, NewServerMetadata());
  EXPECT_TRUE(PullServerInitialMetadata(client).ok());
  PushServerTrailingMetadata(*backend,
                             ServerMetadataFromStatus(GRPC_STATUS_OK));
  EXPECT_EQ(PullServerTrailingStatus(client), absl::OkStatus());
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
  CallInitiator client = StartCall(NewClientMetadata({}, kPath));
  std::optional<CallHandler> backend = GetNextHandler();
  ASSERT_TRUE(backend.has_value());
  auto side_stream = transport_factory_->WaitForStream(target_, kExtProcMethod);
  ASSERT_NE(side_stream, nullptr);
  EXPECT_TRUE(PullClientInitialMetadata(*backend).ok());
  // The request attributes are attached to the first request body event.
  PushClientMessage(client, NewMessage(kMessage1));
  auto request = GetNextSideStreamRequest(*side_stream);
  ASSERT_THAT(request, Optional(MatchesRequestBody(kMessage1, !kEndOfStream)));
  EXPECT_EQ(GetExtProcAttribute(*request, "request.path"), kPath);
  EXPECT_EQ(GetExtProcAttribute(*request, "request.method"), "POST");
  side_stream->SendMessageToClient(
      MakeRequestBodyMutationResponse(kMessage1).SerializeAsString());
  EXPECT_THAT(PullClientMessage(*backend), IsMessage(kMessage1));
  PushClientHalfClose(client);
  EXPECT_THAT(GetNextSideStreamRequest(*side_stream),
              Optional(MatchesRequestBody(kEmptyBody, kEndOfStream)));
  side_stream->SendMessageToClient(
      MakeRequestBodyMutationResponse(kEmptyBody, kEndOfStream)
          .SerializeAsString());
  EXPECT_THAT(PullClientMessage(*backend), IsMessage(kEmptyBody));
  EXPECT_THAT(PullClientMessage(*backend), IsEndOfStream());
  PushServerInitialMetadata(*backend, NewServerMetadata());
  EXPECT_TRUE(PullServerInitialMetadata(client).ok());
  PushServerTrailingMetadata(*backend,
                             ServerMetadataFromStatus(GRPC_STATUS_OK));
  EXPECT_EQ(PullServerTrailingStatus(client), absl::OkStatus());
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
  CallInitiator client = StartCall(NewClientMetadata({}, kPath));
  auto side_stream = transport_factory_->WaitForStream(target_, kExtProcMethod);
  ASSERT_NE(side_stream, nullptr);
  EXPECT_THAT(GetNextSideStreamRequest(*side_stream),
              Optional(MatchesRequestHeaders(Contains(Pair(":path", kPath)))));
  side_stream->SendMessageToClient(
      MakeRequestHeadersMutationResponse({}).SerializeAsString());
  std::optional<CallHandler> backend = GetNextHandler();
  ASSERT_TRUE(backend.has_value());
  PushClientMessage(client, NewMessage(kMessage1));
  EXPECT_THAT(GetNextSideStreamRequest(*side_stream),
              Optional(MatchesRequestBody(kMessage1, !kEndOfStream)));
  ProcessingResponse response;
  response.mutable_request_body()
      ->mutable_response()
      ->mutable_body_mutation()
      ->mutable_streamed_response()
      ->set_grpc_message_compressed(true);
  side_stream->SendMessageToClient(response.SerializeAsString());
  EXPECT_EQ(PullServerTrailingStatus(client),
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
  CallInitiator client = StartCall(NewClientMetadata({}, kPath));
  auto side_stream = transport_factory_->WaitForStream(target_, kExtProcMethod);
  ASSERT_NE(side_stream, nullptr);
  EXPECT_THAT(GetNextSideStreamRequest(*side_stream),
              Optional(MatchesRequestHeaders(Contains(Pair(":path", kPath)))));
  side_stream->SendMessageToClient(
      MakeRequestHeadersMutationResponse({}).SerializeAsString());
  std::optional<CallHandler> backend = GetNextHandler();
  ASSERT_TRUE(backend.has_value());
  EXPECT_TRUE(PullClientInitialMetadata(*backend).ok());
  PushClientMessage(client, NewMessage(kMessage1));
  EXPECT_THAT(GetNextSideStreamRequest(*side_stream),
              Optional(MatchesRequestBody(kMessage1, !kEndOfStream)));
  // The ext_proc server half-closes the request stream along with the
  // mutated message.
  side_stream->SendMessageToClient(
      MakeRequestBodyMutationResponse(kMessage1Mutated, kEndOfStream)
          .SerializeAsString());
  EXPECT_THAT(PullClientMessage(*backend), IsMessage(kMessage1Mutated));
  PushServerInitialMetadata(*backend, NewServerMetadata());
  PushServerMessage(*backend, NewMessage(kMessage1Mutated));
  EXPECT_TRUE(PullServerInitialMetadata(client).ok());
  EXPECT_THAT(PullServerMessage(client), IsMessage(kMessage1Mutated));
  // A further message from the client can't be delivered.
  PushClientMessage(client, NewMessage(kMessage2));
  EXPECT_EQ(PullServerTrailingStatus(client),
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
  CallInitiator client = StartCall(NewClientMetadata({}, kPath));
  auto side_stream = transport_factory_->WaitForStream(target_, kExtProcMethod);
  ASSERT_NE(side_stream, nullptr);
  EXPECT_THAT(GetNextSideStreamRequest(*side_stream),
              Optional(MatchesRequestHeaders(Contains(Pair(":path", kPath)))));
  side_stream->SendMessageToClient(
      MakeRequestHeadersMutationResponse({}).SerializeAsString());
  std::optional<CallHandler> backend = GetNextHandler();
  ASSERT_TRUE(backend.has_value());
  EXPECT_TRUE(PullClientInitialMetadata(*backend).ok());
  PushClientMessage(client, NewMessage(kMessage1));
  EXPECT_THAT(GetNextSideStreamRequest(*side_stream),
              Optional(MatchesRequestBody(kMessage1, !kEndOfStream)));
  side_stream->SendMessageToClient(
      MakeRequestBodyMutationResponse(kMessage1Mutated, kEndOfStream)
          .SerializeAsString());
  EXPECT_THAT(PullClientMessage(*backend), IsMessage(kMessage1Mutated));
  PushServerInitialMetadata(*backend, NewServerMetadata());
  PushServerMessage(*backend, NewMessage(kMessage1Mutated));
  EXPECT_TRUE(PullServerInitialMetadata(client).ok());
  EXPECT_THAT(PullServerMessage(client), IsMessage(kMessage1Mutated));
  // The client's own half-close goes straight to the backend.
  PushClientHalfClose(client);
  EXPECT_THAT(PullClientMessage(*backend), IsEndOfStream());
  PushServerTrailingMetadata(*backend,
                             ServerMetadataFromStatus(GRPC_STATUS_OK));
  EXPECT_THAT(PullServerMessage(client), IsEndOfStream());
  EXPECT_EQ(PullServerTrailingStatus(client), absl::OkStatus());
  EXPECT_EQ(GetNextSideStreamRequest(*side_stream), std::nullopt);
  WaitForAllPendingWork();
}

FILTER_TEST(ExtProcFilterTest, BidiStreamNormalHalfCloseSuccess) {
  Init(ConfigBuilder()
           .SetRequestHeaderMode(true)
           .SetResponseHeaderMode(false)
           .SetRequestBodyMode(true));
  CallInitiator client = StartCall(NewClientMetadata({}, kPath));
  auto side_stream = transport_factory_->WaitForStream(target_, kExtProcMethod);
  ASSERT_NE(side_stream, nullptr);
  EXPECT_THAT(GetNextSideStreamRequest(*side_stream),
              Optional(MatchesRequestHeaders(Contains(Pair(":path", kPath)))));
  side_stream->SendMessageToClient(
      MakeRequestHeadersMutationResponse({}).SerializeAsString());
  std::optional<CallHandler> backend = GetNextHandler();
  ASSERT_TRUE(backend.has_value());
  EXPECT_TRUE(PullClientInitialMetadata(*backend).ok());
  PushServerInitialMetadata(*backend, NewServerMetadata());
  EXPECT_TRUE(PullServerInitialMetadata(client).ok());
  for (int i = 1; i <= 3; ++i) {
    const std::string message = absl::StrCat("message", i);
    PushClientMessage(client, NewMessage(message));
    EXPECT_THAT(GetNextSideStreamRequest(*side_stream),
                Optional(MatchesRequestBody(message, !kEndOfStream)));
    side_stream->SendMessageToClient(
        MakeRequestBodyMutationResponse(message).SerializeAsString());
    EXPECT_THAT(PullClientMessage(*backend), IsMessage(message));
    PushServerMessage(*backend, NewMessage(message));
    EXPECT_THAT(PullServerMessage(client), IsMessage(message));
  }
  PushClientHalfClose(client);
  auto request = GetNextSideStreamRequest(*side_stream);
  ASSERT_THAT(request, Optional(MatchesRequestBody(kEmptyBody, kEndOfStream)));
  EXPECT_TRUE(request->request_body().end_of_stream_without_message());
  side_stream->SendMessageToClient(
      MakeRequestHalfCloseResponse().SerializeAsString());
  EXPECT_THAT(PullClientMessage(*backend), IsEndOfStream());
  PushServerTrailingMetadata(*backend,
                             ServerMetadataFromStatus(GRPC_STATUS_OK));
  EXPECT_THAT(PullServerMessage(client), IsEndOfStream());
  EXPECT_EQ(PullServerTrailingStatus(client), absl::OkStatus());
  WaitForAllPendingWork();
}

//
// Response headers / body / trailers tests
//

FILTER_TEST(ExtProcFilterTest, ResponseHeadersInvalidHeaderMutationFails) {
  Init(ConfigBuilder().SetRequestHeaderMode(false).SetResponseHeaderMode(true));
  CallInitiator client = StartCall(NewClientMetadata({}, kPath));
  std::optional<CallHandler> backend = GetNextHandler();
  ASSERT_TRUE(backend.has_value());
  auto side_stream = transport_factory_->WaitForStream(target_, kExtProcMethod);
  ASSERT_NE(side_stream, nullptr);
  PushServerInitialMetadata(*backend, NewServerMetadata());
  EXPECT_THAT(GetNextSideStreamRequest(*side_stream),
              Optional(MatchesResponseHeaders(_)));
  side_stream->SendMessageToClient(
      MakeResponseHeadersMutationResponse({{"host", "invalid-host"}})
          .SerializeAsString());
  EXPECT_EQ(PullServerTrailingStatus(client),
            absl::InternalError(
                "Failed to parse XdsHeaderValueOption: [field:header.key "
                "error:header \"host\" not allowed]"));
  WaitForAllPendingWork();
}

FILTER_TEST(ExtProcFilterTest, ResponseHeadersMutationRemovesHeader) {
  Init(ConfigBuilder().SetRequestHeaderMode(false).SetResponseHeaderMode(true));
  CallInitiator client = StartCall(NewClientMetadata({}, kPath));
  std::optional<CallHandler> backend = GetNextHandler();
  ASSERT_TRUE(backend.has_value());
  auto side_stream = transport_factory_->WaitForStream(target_, kExtProcMethod);
  ASSERT_NE(side_stream, nullptr);
  EXPECT_TRUE(PullClientInitialMetadata(*backend).ok());
  PushServerInitialMetadata(
      *backend, NewServerMetadata({{kCustomHeaderKey, kCustomHeaderValue}}));
  EXPECT_THAT(GetNextSideStreamRequest(*side_stream),
              Optional(MatchesResponseHeaders(
                  Contains(Pair(kCustomHeaderKey, kCustomHeaderValue)))));
  side_stream->SendMessageToClient(
      MakeResponseHeadersMutationResponse(
          {{kResponseHeadersMutatedHeaderKey, kHeaderMutatedValue}},
          /*remove_headers=*/{kCustomHeaderKey})
          .SerializeAsString());
  EXPECT_THAT(PullServerInitialMetadata(client),
              IsOkAndHolds(Optional(Pointee(
                  AllOf(LacksMetadataKey(kCustomHeaderKey),
                        HasMetadataKeyValue(kResponseHeadersMutatedHeaderKey,
                                            kHeaderMutatedValue))))));
  PushClientHalfClose(client);
  EXPECT_THAT(PullClientMessage(*backend), IsEndOfStream());
  PushServerTrailingMetadata(*backend,
                             ServerMetadataFromStatus(GRPC_STATUS_OK));
  EXPECT_EQ(PullServerTrailingStatus(client), absl::OkStatus());
  EXPECT_EQ(GetNextSideStreamRequest(*side_stream), std::nullopt);
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
  CallInitiator client = StartCall(NewClientMetadata({}, kPath));
  std::optional<CallHandler> backend = GetNextHandler();
  ASSERT_TRUE(backend.has_value());
  auto side_stream = transport_factory_->WaitForStream(target_, kExtProcMethod);
  ASSERT_NE(side_stream, nullptr);
  PushServerInitialMetadata(*backend, NewServerMetadata());
  EXPECT_THAT(GetNextSideStreamRequest(*side_stream),
              Optional(MatchesResponseHeaders(_)));
  side_stream->SendMessageToClient(
      MakeResponseHeadersMutationResponse({}).SerializeAsString());
  PushServerMessage(*backend, NewMessage(kMessage1));
  EXPECT_THAT(GetNextSideStreamRequest(*side_stream),
              Optional(MatchesResponseBody(kMessage1, !kEndOfStream)));
  ProcessingResponse response;
  response.mutable_response_body()
      ->mutable_response()
      ->mutable_body_mutation()
      ->mutable_streamed_response()
      ->set_grpc_message_compressed(true);
  side_stream->SendMessageToClient(response.SerializeAsString());
  EXPECT_EQ(PullServerTrailingStatus(client),
            absl::InternalError("grpc_message_compressed is not supported"));
  WaitForAllPendingWork();
}

FILTER_TEST(ExtProcFilterTest, ResponseTrailersInvalidHeaderMutationFails) {
  Init(ConfigBuilder()
           .SetRequestHeaderMode(false)
           .SetResponseHeaderMode(false)
           .SetResponseTrailerMode(true));
  CallInitiator client = StartCall(NewClientMetadata({}, kPath));
  std::optional<CallHandler> backend = GetNextHandler();
  ASSERT_TRUE(backend.has_value());
  auto side_stream = transport_factory_->WaitForStream(target_, kExtProcMethod);
  ASSERT_NE(side_stream, nullptr);
  PushServerInitialMetadata(*backend, NewServerMetadata());
  EXPECT_TRUE(PullServerInitialMetadata(client).ok());
  PushServerTrailingMetadata(*backend,
                             ServerMetadataFromStatus(GRPC_STATUS_OK));
  EXPECT_THAT(GetNextSideStreamRequest(*side_stream),
              Optional(MatchesResponseTrailers(_)));
  side_stream->SendMessageToClient(
      MakeResponseTrailersMutationResponse({{"host", "invalid-host"}})
          .SerializeAsString());
  EXPECT_EQ(PullServerTrailingStatus(client),
            absl::InternalError(
                "Failed to parse XdsHeaderValueOption: [field:header.key "
                "error:header \"host\" not allowed]"));
  WaitForAllPendingWork();
}

FILTER_TEST(ExtProcFilterTest, ResponseTrailersMutationRemovesHeader) {
  Init(ConfigBuilder()
           .SetRequestHeaderMode(false)
           .SetResponseHeaderMode(false)
           .SetResponseTrailerMode(true));
  CallInitiator client = StartCall(NewClientMetadata({}, kPath));
  std::optional<CallHandler> backend = GetNextHandler();
  ASSERT_TRUE(backend.has_value());
  auto side_stream = transport_factory_->WaitForStream(target_, kExtProcMethod);
  ASSERT_NE(side_stream, nullptr);
  EXPECT_TRUE(PullClientInitialMetadata(*backend).ok());
  PushServerInitialMetadata(*backend, NewServerMetadata());
  EXPECT_TRUE(PullServerInitialMetadata(client).ok());
  PushClientHalfClose(client);
  EXPECT_THAT(PullClientMessage(*backend), IsEndOfStream());
  PushServerTrailingMetadata(
      *backend, NewServerMetadata({{"grpc-status", "0"},
                                   {kCustomHeaderKey, kCustomHeaderValue}}));
  EXPECT_THAT(GetNextSideStreamRequest(*side_stream),
              Optional(MatchesResponseTrailers(
                  Contains(Pair(kCustomHeaderKey, kCustomHeaderValue)))));
  side_stream->SendMessageToClient(
      MakeResponseTrailersMutationResponse(
          {{kResponseTrailersMutatedHeaderKey, kHeaderMutatedValue}},
          /*remove_headers=*/{kCustomHeaderKey})
          .SerializeAsString());
  EXPECT_THAT(PullServerTrailingMetadata(client),
              IsOkAndHolds(Pointee(
                  AllOf(HasMetadataResult(absl::OkStatus()),
                        LacksMetadataKey(kCustomHeaderKey),
                        HasMetadataKeyValue(kResponseTrailersMutatedHeaderKey,
                                            kHeaderMutatedValue)))));
  EXPECT_EQ(GetNextSideStreamRequest(*side_stream), std::nullopt);
  WaitForAllPendingWork();
}

//
// Immediate response tests
//

FILTER_TEST(ExtProcFilterTest, DisableImmediateResponse) {
  Init(ConfigBuilder()
           .SetDisableImmediateResponse(true)
           .EnableAllProcessingModes());
  CallInitiator client = StartCall(NewClientMetadata({}, kPath));
  auto side_stream = transport_factory_->WaitForStream(target_, kExtProcMethod);
  ASSERT_NE(side_stream, nullptr);
  EXPECT_THAT(GetNextSideStreamRequest(*side_stream),
              Optional(MatchesRequestHeaders(Contains(Pair(":path", kPath)))));
  side_stream->SendMessageToClient(
      MakeImmediateResponse(
          GRPC_STATUS_PERMISSION_DENIED,
          "Access Denied by ExtProc (Request Headers)",
          {{kImmediateResponseHeaderKey, kHeaderMutatedValue}})
          .SerializeAsString());
  EXPECT_EQ(
      PullServerTrailingStatus(client),
      absl::InternalError("unhandled immediate response due to config disabled "
                          "it"));
  WaitForAllPendingWork();
}

FILTER_TEST(ExtProcFilterTest, ImmediateResponse) {
  Init(ConfigBuilder().EnableAllProcessingModes());
  CallInitiator client = StartCall(NewClientMetadata({}, kPath));
  auto side_stream = transport_factory_->WaitForStream(target_, kExtProcMethod);
  ASSERT_NE(side_stream, nullptr);
  EXPECT_THAT(GetNextSideStreamRequest(*side_stream),
              Optional(MatchesRequestHeaders(Contains(Pair(":path", kPath)))));
  side_stream->SendMessageToClient(
      MakeImmediateResponse(
          GRPC_STATUS_PERMISSION_DENIED,
          "Access Denied by ExtProc (Request Headers)",
          {{kImmediateResponseHeaderKey, kHeaderMutatedValue}})
          .SerializeAsString());
  EXPECT_THAT(PullServerTrailingMetadata(client),
              IsOkAndHolds(Pointee(
                  AllOf(HasMetadataResult(absl::PermissionDeniedError(
                            "Access Denied by ExtProc (Request Headers)")),
                        HasMetadataKeyValue(kImmediateResponseHeaderKey,
                                            kHeaderMutatedValue)))));
  WaitForAllPendingWork();
}

//
// Stream drain tests
//
// A ProcessingResponse may ask the filter to drain request and/or response
// messages. The filter sends a body request with drain_complete set for each
// direction drained, and once the ext_proc server acknowledges it, messages in
// that direction pass through without being sent to the ext_proc server.
//

FILTER_TEST(ExtProcFilterTest, DrainRequestsStartedInRequestHeadersResponse) {
  Init(ConfigBuilder()
           .SetRequestHeaderMode(true)
           .SetRequestBodyMode(true)
           .SetResponseHeaderMode(false)
           .SetResponseTrailerMode(false));
  CallInitiator client = StartCall(NewClientMetadata({}, kPath));
  auto side_stream = transport_factory_->WaitForStream(target_, kExtProcMethod);
  ASSERT_NE(side_stream, nullptr);
  EXPECT_THAT(GetNextSideStreamRequest(*side_stream),
              Optional(MatchesRequestHeaders(_)));
  side_stream->SendMessageToClient(
      SetRequestDrains(MakeRequestHeadersMutationResponse({}),
                       /*drain_requests=*/true, /*drain_responses=*/false)
          .SerializeAsString());
  EXPECT_THAT(GetNextSideStreamRequest(*side_stream),
              Optional(MatchesRequestBodyDrainComplete()));
  side_stream->SendMessageToClient(
      MakeRequestBodyDrainCompleteResponse().SerializeAsString());
  std::optional<CallHandler> backend = GetNextHandler();
  ASSERT_TRUE(backend.has_value());
  EXPECT_TRUE(PullClientInitialMetadata(*backend).ok());
  // Request messages and half-close are not sent to the ext_proc server.
  PushClientMessage(client, NewMessage(kMessage1));
  EXPECT_THAT(PullClientMessage(*backend), IsMessage(kMessage1));
  PushServerInitialMetadata(*backend, NewServerMetadata());
  PushServerMessage(*backend, NewMessage(kMessage1));
  EXPECT_TRUE(PullServerInitialMetadata(client).ok());
  EXPECT_THAT(PullServerMessage(client), IsMessage(kMessage1));
  PushClientMessage(client, NewMessage(kMessage2));
  EXPECT_THAT(PullClientMessage(*backend), IsMessage(kMessage2));
  PushServerMessage(*backend, NewMessage(kMessage2));
  EXPECT_THAT(PullServerMessage(client), IsMessage(kMessage2));
  PushClientHalfClose(client);
  EXPECT_THAT(PullClientMessage(*backend), IsEndOfStream());
  PushServerTrailingMetadata(*backend,
                             ServerMetadataFromStatus(GRPC_STATUS_OK));
  EXPECT_EQ(PullServerTrailingStatus(client), absl::OkStatus());
  EXPECT_EQ(GetNextSideStreamRequest(*side_stream), std::nullopt);
  WaitForAllPendingWork();
}

FILTER_TEST(ExtProcFilterTest, DrainRequestsStartedInRequestBodyResponse) {
  Init(ConfigBuilder()
           .SetRequestHeaderMode(true)
           .SetRequestBodyMode(true)
           .SetResponseHeaderMode(false)
           .SetResponseTrailerMode(false));
  CallInitiator client = StartCall(NewClientMetadata({}, kPath));
  auto side_stream = transport_factory_->WaitForStream(target_, kExtProcMethod);
  ASSERT_NE(side_stream, nullptr);
  EXPECT_THAT(GetNextSideStreamRequest(*side_stream),
              Optional(MatchesRequestHeaders(_)));
  side_stream->SendMessageToClient(
      MakeRequestHeadersMutationResponse({}).SerializeAsString());
  std::optional<CallHandler> backend = GetNextHandler();
  ASSERT_TRUE(backend.has_value());
  EXPECT_TRUE(PullClientInitialMetadata(*backend).ok());
  PushClientMessage(client, NewMessage(kMessage1));
  EXPECT_THAT(GetNextSideStreamRequest(*side_stream),
              Optional(MatchesRequestBody(kMessage1, !kEndOfStream)));
  side_stream->SendMessageToClient(
      SetRequestDrains(MakeRequestBodyMutationResponse(kMessage1Mutated),
                       /*drain_requests=*/true, /*drain_responses=*/false)
          .SerializeAsString());
  EXPECT_THAT(GetNextSideStreamRequest(*side_stream),
              Optional(MatchesRequestBodyDrainComplete()));
  side_stream->SendMessageToClient(
      MakeRequestBodyDrainCompleteResponse().SerializeAsString());
  EXPECT_THAT(PullClientMessage(*backend), IsMessage(kMessage1Mutated));
  PushServerInitialMetadata(*backend, NewServerMetadata());
  PushServerMessage(*backend, NewMessage(kMessage1Mutated));
  EXPECT_TRUE(PullServerInitialMetadata(client).ok());
  EXPECT_THAT(PullServerMessage(client), IsMessage(kMessage1Mutated));
  // Subsequent request messages and half-close are not sent to the ext_proc
  // server.
  PushClientMessage(client, NewMessage(kMessage2));
  EXPECT_THAT(PullClientMessage(*backend), IsMessage(kMessage2));
  PushServerMessage(*backend, NewMessage(kMessage2));
  EXPECT_THAT(PullServerMessage(client), IsMessage(kMessage2));
  PushClientHalfClose(client);
  EXPECT_THAT(PullClientMessage(*backend), IsEndOfStream());
  PushServerTrailingMetadata(*backend,
                             ServerMetadataFromStatus(GRPC_STATUS_OK));
  EXPECT_EQ(PullServerTrailingStatus(client), absl::OkStatus());
  EXPECT_EQ(GetNextSideStreamRequest(*side_stream), std::nullopt);
  WaitForAllPendingWork();
}

FILTER_TEST(ExtProcFilterTest, DrainRequestsStartedInResponseHeadersResponse) {
  Init(ConfigBuilder()
           .SetRequestHeaderMode(true)
           .SetRequestBodyMode(true)
           .SetResponseHeaderMode(true)
           .SetResponseTrailerMode(false));
  CallInitiator client = StartCall(NewClientMetadata({}, kPath));
  auto side_stream = transport_factory_->WaitForStream(target_, kExtProcMethod);
  ASSERT_NE(side_stream, nullptr);
  EXPECT_THAT(GetNextSideStreamRequest(*side_stream),
              Optional(MatchesRequestHeaders(_)));
  side_stream->SendMessageToClient(
      MakeRequestHeadersMutationResponse({}).SerializeAsString());
  std::optional<CallHandler> backend = GetNextHandler();
  ASSERT_TRUE(backend.has_value());
  EXPECT_TRUE(PullClientInitialMetadata(*backend).ok());
  PushClientMessage(client, NewMessage(kMessage1));
  EXPECT_THAT(GetNextSideStreamRequest(*side_stream),
              Optional(MatchesRequestBody(kMessage1, !kEndOfStream)));
  side_stream->SendMessageToClient(
      MakeRequestBodyMutationResponse(kMessage1Mutated).SerializeAsString());
  EXPECT_THAT(PullClientMessage(*backend), IsMessage(kMessage1Mutated));
  PushServerInitialMetadata(*backend, NewServerMetadata());
  EXPECT_THAT(GetNextSideStreamRequest(*side_stream),
              Optional(MatchesResponseHeaders(_)));
  side_stream->SendMessageToClient(
      SetRequestDrains(MakeResponseHeadersMutationResponse({}),
                       /*drain_requests=*/true, /*drain_responses=*/false)
          .SerializeAsString());
  EXPECT_THAT(GetNextSideStreamRequest(*side_stream),
              Optional(MatchesRequestBodyDrainComplete()));
  side_stream->SendMessageToClient(
      MakeRequestBodyDrainCompleteResponse().SerializeAsString());
  EXPECT_TRUE(PullServerInitialMetadata(client).ok());
  PushServerMessage(*backend, NewMessage(kMessage1Mutated));
  EXPECT_THAT(PullServerMessage(client), IsMessage(kMessage1Mutated));
  // Subsequent request messages and half-close are not sent to the ext_proc
  // server.
  PushClientMessage(client, NewMessage(kMessage2));
  EXPECT_THAT(PullClientMessage(*backend), IsMessage(kMessage2));
  PushServerMessage(*backend, NewMessage(kMessage2));
  EXPECT_THAT(PullServerMessage(client), IsMessage(kMessage2));
  PushClientHalfClose(client);
  EXPECT_THAT(PullClientMessage(*backend), IsEndOfStream());
  PushServerTrailingMetadata(*backend,
                             ServerMetadataFromStatus(GRPC_STATUS_OK));
  EXPECT_EQ(PullServerTrailingStatus(client), absl::OkStatus());
  EXPECT_EQ(GetNextSideStreamRequest(*side_stream), std::nullopt);
  WaitForAllPendingWork();
}

FILTER_TEST(ExtProcFilterTest, DrainRequestsStartedInResponseBodyResponse) {
  Init(ConfigBuilder()
           .SetRequestHeaderMode(true)
           .SetRequestBodyMode(true)
           .SetResponseHeaderMode(false)
           .SetResponseBodyMode(true)
           .SetResponseTrailerMode(true));
  CallInitiator client = StartCall(NewClientMetadata({}, kPath));
  auto side_stream = transport_factory_->WaitForStream(target_, kExtProcMethod);
  ASSERT_NE(side_stream, nullptr);
  EXPECT_THAT(GetNextSideStreamRequest(*side_stream),
              Optional(MatchesRequestHeaders(_)));
  side_stream->SendMessageToClient(
      MakeRequestHeadersMutationResponse({}).SerializeAsString());
  std::optional<CallHandler> backend = GetNextHandler();
  ASSERT_TRUE(backend.has_value());
  EXPECT_TRUE(PullClientInitialMetadata(*backend).ok());
  PushClientMessage(client, NewMessage(kMessage1));
  EXPECT_THAT(GetNextSideStreamRequest(*side_stream),
              Optional(MatchesRequestBody(kMessage1, !kEndOfStream)));
  side_stream->SendMessageToClient(
      MakeRequestBodyMutationResponse(kMessage1Mutated).SerializeAsString());
  EXPECT_THAT(PullClientMessage(*backend), IsMessage(kMessage1Mutated));
  PushServerInitialMetadata(*backend, NewServerMetadata());
  PushServerMessage(*backend, NewMessage(kMessage1Mutated));
  EXPECT_THAT(GetNextSideStreamRequest(*side_stream),
              Optional(MatchesResponseBody(kMessage1Mutated, !kEndOfStream)));
  side_stream->SendMessageToClient(
      SetRequestDrains(MakeResponseBodyMutationResponse(kMessage1DoubleMutated),
                       /*drain_requests=*/true, /*drain_responses=*/false)
          .SerializeAsString());
  EXPECT_THAT(GetNextSideStreamRequest(*side_stream),
              Optional(MatchesRequestBodyDrainComplete()));
  side_stream->SendMessageToClient(
      MakeRequestBodyDrainCompleteResponse().SerializeAsString());
  EXPECT_TRUE(PullServerInitialMetadata(client).ok());
  EXPECT_THAT(PullServerMessage(client), IsMessage(kMessage1DoubleMutated));
  // Subsequent request messages and half-close are not sent to the ext_proc
  // server. The response message and trailers are still sent, since the
  // response direction has not been drained.
  PushClientMessage(client, NewMessage(kMessage2));
  EXPECT_THAT(PullClientMessage(*backend), IsMessage(kMessage2));
  PushServerMessage(*backend, NewMessage(kMessage2));
  EXPECT_THAT(GetNextSideStreamRequest(*side_stream),
              Optional(MatchesResponseBody(kMessage2, !kEndOfStream)));
  side_stream->SendMessageToClient(
      MakeResponseBodyMutationResponse(kMessage2).SerializeAsString());
  EXPECT_THAT(PullServerMessage(client), IsMessage(kMessage2));
  PushClientHalfClose(client);
  EXPECT_THAT(PullClientMessage(*backend), IsEndOfStream());
  PushServerTrailingMetadata(*backend,
                             ServerMetadataFromStatus(GRPC_STATUS_OK));
  EXPECT_THAT(GetNextSideStreamRequest(*side_stream),
              Optional(MatchesResponseTrailers(_)));
  side_stream->SendMessageToClient(
      MakeResponseTrailersMutationResponse({}).SerializeAsString());
  EXPECT_EQ(PullServerTrailingStatus(client), absl::OkStatus());
  EXPECT_EQ(GetNextSideStreamRequest(*side_stream), std::nullopt);
  WaitForAllPendingWork();
}

FILTER_TEST(ExtProcFilterTest, DrainRequestsStartedInStandaloneResponse) {
  Init(ConfigBuilder()
           .SetRequestHeaderMode(true)
           .SetRequestBodyMode(true)
           .SetResponseHeaderMode(false)
           .SetResponseTrailerMode(false));
  CallInitiator client = StartCall(NewClientMetadata({}, kPath));
  auto side_stream = transport_factory_->WaitForStream(target_, kExtProcMethod);
  ASSERT_NE(side_stream, nullptr);
  EXPECT_THAT(GetNextSideStreamRequest(*side_stream),
              Optional(MatchesRequestHeaders(_)));
  side_stream->SendMessageToClient(
      MakeRequestHeadersMutationResponse({}).SerializeAsString());
  std::optional<CallHandler> backend = GetNextHandler();
  ASSERT_TRUE(backend.has_value());
  EXPECT_TRUE(PullClientInitialMetadata(*backend).ok());
  PushClientMessage(client, NewMessage(kMessage1));
  EXPECT_THAT(GetNextSideStreamRequest(*side_stream),
              Optional(MatchesRequestBody(kMessage1, !kEndOfStream)));
  side_stream->SendMessageToClient(
      MakeRequestBodyMutationResponse(kMessage1Mutated).SerializeAsString());
  EXPECT_THAT(PullClientMessage(*backend), IsMessage(kMessage1Mutated));
  PushServerInitialMetadata(*backend, NewServerMetadata());
  PushServerMessage(*backend, NewMessage(kMessage1Mutated));
  EXPECT_TRUE(PullServerInitialMetadata(client).ok());
  EXPECT_THAT(PullServerMessage(client), IsMessage(kMessage1Mutated));
  // Start the drain in a response that does not contain any other event.
  side_stream->SendMessageToClient(SetRequestDrains(ProcessingResponse(),
                                                    /*drain_requests=*/true,
                                                    /*drain_responses=*/false)
                                       .SerializeAsString());
  EXPECT_THAT(GetNextSideStreamRequest(*side_stream),
              Optional(MatchesRequestBodyDrainComplete()));
  side_stream->SendMessageToClient(
      MakeRequestBodyDrainCompleteResponse().SerializeAsString());
  // Subsequent request messages and half-close are not sent to the ext_proc
  // server.
  PushClientMessage(client, NewMessage(kMessage2));
  EXPECT_THAT(PullClientMessage(*backend), IsMessage(kMessage2));
  PushServerMessage(*backend, NewMessage(kMessage2));
  EXPECT_THAT(PullServerMessage(client), IsMessage(kMessage2));
  PushClientHalfClose(client);
  EXPECT_THAT(PullClientMessage(*backend), IsEndOfStream());
  PushServerTrailingMetadata(*backend,
                             ServerMetadataFromStatus(GRPC_STATUS_OK));
  EXPECT_EQ(PullServerTrailingStatus(client), absl::OkStatus());
  EXPECT_EQ(GetNextSideStreamRequest(*side_stream), std::nullopt);
  WaitForAllPendingWork();
}

FILTER_TEST(ExtProcFilterTest, DrainResponsesStartedInRequestHeadersResponse) {
  Init(ConfigBuilder()
           .SetRequestHeaderMode(true)
           .SetResponseHeaderMode(true)
           .SetResponseBodyMode(true)
           .SetResponseTrailerMode(true));
  CallInitiator client = StartCall(NewClientMetadata({}, kPath));
  auto side_stream = transport_factory_->WaitForStream(target_, kExtProcMethod);
  ASSERT_NE(side_stream, nullptr);
  EXPECT_THAT(GetNextSideStreamRequest(*side_stream),
              Optional(MatchesRequestHeaders(_)));
  side_stream->SendMessageToClient(
      SetRequestDrains(MakeRequestHeadersMutationResponse({}),
                       /*drain_requests=*/false, /*drain_responses=*/true)
          .SerializeAsString());
  EXPECT_THAT(GetNextSideStreamRequest(*side_stream),
              Optional(MatchesResponseBodyDrainComplete()));
  side_stream->SendMessageToClient(
      MakeResponseBodyDrainCompleteResponse().SerializeAsString());
  std::optional<CallHandler> backend = GetNextHandler();
  ASSERT_TRUE(backend.has_value());
  EXPECT_TRUE(PullClientInitialMetadata(*backend).ok());
  // Response headers, messages, and trailers are not sent to the ext_proc
  // server.
  PushClientMessage(client, NewMessage(kMessage1));
  EXPECT_THAT(PullClientMessage(*backend), IsMessage(kMessage1));
  PushServerInitialMetadata(*backend, NewServerMetadata());
  PushServerMessage(*backend, NewMessage(kMessage1));
  EXPECT_TRUE(PullServerInitialMetadata(client).ok());
  EXPECT_THAT(PullServerMessage(client), IsMessage(kMessage1));
  PushClientMessage(client, NewMessage(kMessage2));
  EXPECT_THAT(PullClientMessage(*backend), IsMessage(kMessage2));
  PushServerMessage(*backend, NewMessage(kMessage2));
  EXPECT_THAT(PullServerMessage(client), IsMessage(kMessage2));
  PushClientHalfClose(client);
  EXPECT_THAT(PullClientMessage(*backend), IsEndOfStream());
  PushServerTrailingMetadata(*backend,
                             ServerMetadataFromStatus(GRPC_STATUS_OK));
  EXPECT_EQ(PullServerTrailingStatus(client), absl::OkStatus());
  EXPECT_EQ(GetNextSideStreamRequest(*side_stream), std::nullopt);
  WaitForAllPendingWork();
}

FILTER_TEST(ExtProcFilterTest, DrainResponsesStartedInRequestBodyResponse) {
  Init(ConfigBuilder()
           .SetRequestHeaderMode(false)
           .SetRequestBodyMode(true)
           .SetResponseHeaderMode(true)
           .SetResponseBodyMode(true)
           .SetResponseTrailerMode(true));
  CallInitiator client = StartCall(NewClientMetadata({}, kPath));
  std::optional<CallHandler> backend = GetNextHandler();
  ASSERT_TRUE(backend.has_value());
  auto side_stream = transport_factory_->WaitForStream(target_, kExtProcMethod);
  ASSERT_NE(side_stream, nullptr);
  EXPECT_TRUE(PullClientInitialMetadata(*backend).ok());
  PushClientMessage(client, NewMessage(kMessage1));
  EXPECT_THAT(GetNextSideStreamRequest(*side_stream),
              Optional(MatchesRequestBody(kMessage1, !kEndOfStream)));
  side_stream->SendMessageToClient(
      SetRequestDrains(MakeRequestBodyMutationResponse(kMessage1Mutated),
                       /*drain_requests=*/false, /*drain_responses=*/true)
          .SerializeAsString());
  EXPECT_THAT(GetNextSideStreamRequest(*side_stream),
              Optional(MatchesResponseBodyDrainComplete()));
  side_stream->SendMessageToClient(
      MakeResponseBodyDrainCompleteResponse().SerializeAsString());
  // Response headers, messages, and trailers are not sent to the ext_proc
  // server. Request messages and half-close are still sent, since the request
  // direction has not been drained.
  EXPECT_THAT(PullClientMessage(*backend), IsMessage(kMessage1Mutated));
  PushServerInitialMetadata(*backend, NewServerMetadata());
  PushServerMessage(*backend, NewMessage(kMessage1Mutated));
  EXPECT_TRUE(PullServerInitialMetadata(client).ok());
  EXPECT_THAT(PullServerMessage(client), IsMessage(kMessage1Mutated));
  PushClientMessage(client, NewMessage(kMessage2));
  EXPECT_THAT(GetNextSideStreamRequest(*side_stream),
              Optional(MatchesRequestBody(kMessage2, !kEndOfStream)));
  side_stream->SendMessageToClient(
      MakeRequestBodyMutationResponse(kMessage2).SerializeAsString());
  EXPECT_THAT(PullClientMessage(*backend), IsMessage(kMessage2));
  PushServerMessage(*backend, NewMessage(kMessage2));
  EXPECT_THAT(PullServerMessage(client), IsMessage(kMessage2));
  PushClientHalfClose(client);
  EXPECT_THAT(GetNextSideStreamRequest(*side_stream),
              Optional(MatchesRequestBody(kEmptyBody, kEndOfStream)));
  side_stream->SendMessageToClient(
      MakeRequestBodyMutationResponse(kEmptyBody, kEndOfStream)
          .SerializeAsString());
  EXPECT_THAT(PullClientMessage(*backend), IsMessage(kEmptyBody));
  EXPECT_THAT(PullClientMessage(*backend), IsEndOfStream());
  PushServerTrailingMetadata(*backend,
                             ServerMetadataFromStatus(GRPC_STATUS_OK));
  EXPECT_EQ(PullServerTrailingStatus(client), absl::OkStatus());
  EXPECT_EQ(GetNextSideStreamRequest(*side_stream), std::nullopt);
  WaitForAllPendingWork();
}

FILTER_TEST(ExtProcFilterTest, DrainResponsesStartedInResponseHeadersResponse) {
  Init(ConfigBuilder()
           .SetRequestHeaderMode(false)
           .SetResponseHeaderMode(true)
           .SetResponseBodyMode(true)
           .SetResponseTrailerMode(true));
  CallInitiator client = StartCall(NewClientMetadata({}, kPath));
  std::optional<CallHandler> backend = GetNextHandler();
  ASSERT_TRUE(backend.has_value());
  auto side_stream = transport_factory_->WaitForStream(target_, kExtProcMethod);
  ASSERT_NE(side_stream, nullptr);
  EXPECT_TRUE(PullClientInitialMetadata(*backend).ok());
  // The backend sends its initial metadata before any message, so that the
  // drain is started before any response message is sent to the ext_proc
  // server.
  PushServerInitialMetadata(*backend, NewServerMetadata());
  EXPECT_THAT(GetNextSideStreamRequest(*side_stream),
              Optional(MatchesResponseHeaders(_)));
  side_stream->SendMessageToClient(
      SetRequestDrains(MakeResponseHeadersMutationResponse({}),
                       /*drain_requests=*/false, /*drain_responses=*/true)
          .SerializeAsString());
  EXPECT_THAT(GetNextSideStreamRequest(*side_stream),
              Optional(MatchesResponseBodyDrainComplete()));
  side_stream->SendMessageToClient(
      MakeResponseBodyDrainCompleteResponse().SerializeAsString());
  EXPECT_TRUE(PullServerInitialMetadata(client).ok());
  // Response messages and trailers are not sent to the ext_proc server.
  PushClientMessage(client, NewMessage(kMessage1));
  EXPECT_THAT(PullClientMessage(*backend), IsMessage(kMessage1));
  PushServerMessage(*backend, NewMessage(kMessage1));
  EXPECT_THAT(PullServerMessage(client), IsMessage(kMessage1));
  PushClientMessage(client, NewMessage(kMessage2));
  EXPECT_THAT(PullClientMessage(*backend), IsMessage(kMessage2));
  PushServerMessage(*backend, NewMessage(kMessage2));
  EXPECT_THAT(PullServerMessage(client), IsMessage(kMessage2));
  PushClientHalfClose(client);
  EXPECT_THAT(PullClientMessage(*backend), IsEndOfStream());
  PushServerTrailingMetadata(*backend,
                             ServerMetadataFromStatus(GRPC_STATUS_OK));
  EXPECT_EQ(PullServerTrailingStatus(client), absl::OkStatus());
  EXPECT_EQ(GetNextSideStreamRequest(*side_stream), std::nullopt);
  WaitForAllPendingWork();
}

FILTER_TEST(ExtProcFilterTest, DrainResponsesStartedInResponseBodyResponse) {
  Init(ConfigBuilder()
           .SetRequestHeaderMode(false)
           .SetResponseHeaderMode(true)
           .SetResponseBodyMode(true)
           .SetResponseTrailerMode(true));
  CallInitiator client = StartCall(NewClientMetadata({}, kPath));
  std::optional<CallHandler> backend = GetNextHandler();
  ASSERT_TRUE(backend.has_value());
  auto side_stream = transport_factory_->WaitForStream(target_, kExtProcMethod);
  ASSERT_NE(side_stream, nullptr);
  EXPECT_TRUE(PullClientInitialMetadata(*backend).ok());
  PushClientMessage(client, NewMessage(kMessage1));
  EXPECT_THAT(PullClientMessage(*backend), IsMessage(kMessage1));
  PushServerInitialMetadata(*backend, NewServerMetadata());
  EXPECT_THAT(GetNextSideStreamRequest(*side_stream),
              Optional(MatchesResponseHeaders(_)));
  side_stream->SendMessageToClient(
      MakeResponseHeadersMutationResponse({}).SerializeAsString());
  PushServerMessage(*backend, NewMessage(kMessage1));
  EXPECT_THAT(GetNextSideStreamRequest(*side_stream),
              Optional(MatchesResponseBody(kMessage1, !kEndOfStream)));
  side_stream->SendMessageToClient(
      SetRequestDrains(MakeResponseBodyMutationResponse(kMessage1Mutated),
                       /*drain_requests=*/false, /*drain_responses=*/true)
          .SerializeAsString());
  EXPECT_THAT(GetNextSideStreamRequest(*side_stream),
              Optional(MatchesResponseBodyDrainComplete()));
  side_stream->SendMessageToClient(
      MakeResponseBodyDrainCompleteResponse().SerializeAsString());
  EXPECT_TRUE(PullServerInitialMetadata(client).ok());
  EXPECT_THAT(PullServerMessage(client), IsMessage(kMessage1Mutated));
  // Subsequent response messages and trailers are not sent to the ext_proc
  // server.
  PushClientMessage(client, NewMessage(kMessage2));
  EXPECT_THAT(PullClientMessage(*backend), IsMessage(kMessage2));
  PushServerMessage(*backend, NewMessage(kMessage2));
  EXPECT_THAT(PullServerMessage(client), IsMessage(kMessage2));
  PushClientHalfClose(client);
  EXPECT_THAT(PullClientMessage(*backend), IsEndOfStream());
  PushServerTrailingMetadata(*backend,
                             ServerMetadataFromStatus(GRPC_STATUS_OK));
  EXPECT_EQ(PullServerTrailingStatus(client), absl::OkStatus());
  EXPECT_EQ(GetNextSideStreamRequest(*side_stream), std::nullopt);
  WaitForAllPendingWork();
}

FILTER_TEST(ExtProcFilterTest, DrainResponsesStartedInStandaloneResponse) {
  Init(ConfigBuilder()
           .SetRequestHeaderMode(false)
           .SetResponseHeaderMode(true)
           .SetResponseBodyMode(true)
           .SetResponseTrailerMode(true));
  CallInitiator client = StartCall(NewClientMetadata({}, kPath));
  std::optional<CallHandler> backend = GetNextHandler();
  ASSERT_TRUE(backend.has_value());
  auto side_stream = transport_factory_->WaitForStream(target_, kExtProcMethod);
  ASSERT_NE(side_stream, nullptr);
  EXPECT_TRUE(PullClientInitialMetadata(*backend).ok());
  PushClientMessage(client, NewMessage(kMessage1));
  EXPECT_THAT(PullClientMessage(*backend), IsMessage(kMessage1));
  PushServerInitialMetadata(*backend, NewServerMetadata());
  EXPECT_THAT(GetNextSideStreamRequest(*side_stream),
              Optional(MatchesResponseHeaders(_)));
  side_stream->SendMessageToClient(
      MakeResponseHeadersMutationResponse({}).SerializeAsString());
  PushServerMessage(*backend, NewMessage(kMessage1));
  EXPECT_THAT(GetNextSideStreamRequest(*side_stream),
              Optional(MatchesResponseBody(kMessage1, !kEndOfStream)));
  side_stream->SendMessageToClient(
      MakeResponseBodyMutationResponse(kMessage1Mutated).SerializeAsString());
  EXPECT_TRUE(PullServerInitialMetadata(client).ok());
  EXPECT_THAT(PullServerMessage(client), IsMessage(kMessage1Mutated));
  // Start the drain in a response that does not contain any other event.
  side_stream->SendMessageToClient(SetRequestDrains(ProcessingResponse(),
                                                    /*drain_requests=*/false,
                                                    /*drain_responses=*/true)
                                       .SerializeAsString());
  EXPECT_THAT(GetNextSideStreamRequest(*side_stream),
              Optional(MatchesResponseBodyDrainComplete()));
  side_stream->SendMessageToClient(
      MakeResponseBodyDrainCompleteResponse().SerializeAsString());
  // Subsequent response messages and trailers are not sent to the ext_proc
  // server.
  PushClientMessage(client, NewMessage(kMessage2));
  EXPECT_THAT(PullClientMessage(*backend), IsMessage(kMessage2));
  PushServerMessage(*backend, NewMessage(kMessage2));
  EXPECT_THAT(PullServerMessage(client), IsMessage(kMessage2));
  PushClientHalfClose(client);
  EXPECT_THAT(PullClientMessage(*backend), IsEndOfStream());
  PushServerTrailingMetadata(*backend,
                             ServerMetadataFromStatus(GRPC_STATUS_OK));
  EXPECT_EQ(PullServerTrailingStatus(client), absl::OkStatus());
  EXPECT_EQ(GetNextSideStreamRequest(*side_stream), std::nullopt);
  WaitForAllPendingWork();
}

// A response drain requested after the server's trailers have been seen is
// ignored, since there are no more response messages to drain.
FILTER_TEST(ExtProcFilterTest,
            DrainResponsesIgnoredInResponseTrailersResponse) {
  Init(ConfigBuilder()
           .SetRequestHeaderMode(false)
           .SetResponseHeaderMode(false)
           .SetResponseBodyMode(true)
           .SetResponseTrailerMode(true));
  CallInitiator client = StartCall(NewClientMetadata({}, kPath));
  std::optional<CallHandler> backend = GetNextHandler();
  ASSERT_TRUE(backend.has_value());
  auto side_stream = transport_factory_->WaitForStream(target_, kExtProcMethod);
  ASSERT_NE(side_stream, nullptr);
  EXPECT_TRUE(PullClientInitialMetadata(*backend).ok());
  PushClientMessage(client, NewMessage(kMessage1));
  EXPECT_THAT(PullClientMessage(*backend), IsMessage(kMessage1));
  PushServerInitialMetadata(*backend, NewServerMetadata());
  PushServerMessage(*backend, NewMessage(kMessage1));
  EXPECT_THAT(GetNextSideStreamRequest(*side_stream),
              Optional(MatchesResponseBody(kMessage1, !kEndOfStream)));
  side_stream->SendMessageToClient(
      MakeResponseBodyMutationResponse(kMessage1).SerializeAsString());
  EXPECT_TRUE(PullServerInitialMetadata(client).ok());
  EXPECT_THAT(PullServerMessage(client), IsMessage(kMessage1));
  PushClientHalfClose(client);
  EXPECT_THAT(PullClientMessage(*backend), IsEndOfStream());
  PushServerTrailingMetadata(*backend,
                             ServerMetadataFromStatus(GRPC_STATUS_OK));
  EXPECT_THAT(GetNextSideStreamRequest(*side_stream),
              Optional(MatchesResponseTrailers(_)));
  side_stream->SendMessageToClient(
      SetRequestDrains(MakeResponseTrailersMutationResponse({}),
                       /*drain_requests=*/false, /*drain_responses=*/true)
          .SerializeAsString());
  EXPECT_EQ(PullServerTrailingStatus(client), absl::OkStatus());
  // No drain_complete is sent to the ext_proc server.
  EXPECT_EQ(GetNextSideStreamRequest(*side_stream), std::nullopt);
  WaitForAllPendingWork();
}

FILTER_TEST(ExtProcFilterTest, UnexpectedDrainCompleteFailsCall) {
  Init(ConfigBuilder()
           .SetRequestHeaderMode(false)
           .SetResponseHeaderMode(false)
           .SetRequestBodyMode(true));
  CallInitiator client = StartCall(NewClientMetadata({}, kPath));
  std::optional<CallHandler> backend = GetNextHandler();
  ASSERT_TRUE(backend.has_value());
  auto side_stream = transport_factory_->WaitForStream(target_, kExtProcMethod);
  ASSERT_NE(side_stream, nullptr);
  EXPECT_TRUE(PullClientInitialMetadata(*backend).ok());
  PushClientMessage(client, NewMessage(kMessage1));
  EXPECT_THAT(GetNextSideStreamRequest(*side_stream),
              Optional(MatchesRequestBody(kMessage1, !kEndOfStream)));
  side_stream->SendMessageToClient(
      MakeRequestBodyDrainCompleteResponse().SerializeAsString());
  EXPECT_EQ(PullServerTrailingStatus(client),
            absl::InternalError(
                "Received unexpected drain_complete from external processor"));
  WaitForAllPendingWork();
}

FILTER_TEST(ExtProcFilterTest, DrainIgnoredAfterHalfClose) {
  Init(ConfigBuilder()
           .SetRequestHeaderMode(false)
           .SetResponseHeaderMode(false)
           .SetRequestBodyMode(true));
  CallInitiator client = StartCall(NewClientMetadata({}, kPath));
  std::optional<CallHandler> backend = GetNextHandler();
  ASSERT_TRUE(backend.has_value());
  auto side_stream = transport_factory_->WaitForStream(target_, kExtProcMethod);
  ASSERT_NE(side_stream, nullptr);
  EXPECT_TRUE(PullClientInitialMetadata(*backend).ok());
  PushClientMessage(client, NewMessage(kMessage1));
  EXPECT_THAT(GetNextSideStreamRequest(*side_stream),
              Optional(MatchesRequestBody(kMessage1, !kEndOfStream)));
  side_stream->SendMessageToClient(
      MakeRequestBodyMutationResponse(kMessage1Mutated, !kEndOfStream)
          .SerializeAsString());
  EXPECT_THAT(PullClientMessage(*backend), IsMessage(kMessage1Mutated));
  PushServerInitialMetadata(*backend, NewServerMetadata());
  PushServerMessage(*backend, NewMessage(kMessage1Mutated));
  EXPECT_TRUE(PullServerInitialMetadata(client).ok());
  EXPECT_THAT(PullServerMessage(client), IsMessage(kMessage1Mutated));
  PushClientHalfClose(client);
  EXPECT_THAT(GetNextSideStreamRequest(*side_stream),
              Optional(MatchesRequestBody(kEmptyBody, kEndOfStream)));
  side_stream->SendMessageToClient(
      SetRequestDrains(
          MakeRequestBodyMutationResponse(kEmptyBody, kEndOfStream),
          /*drain_requests=*/true, /*drain_responses=*/false)
          .SerializeAsString());
  EXPECT_THAT(PullClientMessage(*backend), IsMessage(kEmptyBody));
  EXPECT_THAT(PullClientMessage(*backend), IsEndOfStream());
  PushServerTrailingMetadata(*backend,
                             ServerMetadataFromStatus(GRPC_STATUS_OK));
  EXPECT_THAT(PullServerMessage(client), IsEndOfStream());
  EXPECT_EQ(PullServerTrailingStatus(client), absl::OkStatus());
  // No drain_complete is sent to the ext_proc server.
  EXPECT_EQ(GetNextSideStreamRequest(*side_stream), std::nullopt);
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
  CallInitiator client = StartCall(NewClientMetadata({}, kPath));
  std::optional<CallHandler> backend = GetNextHandler();
  ASSERT_TRUE(backend.has_value());
  auto side_stream = transport_factory_->WaitForStream(target_, kExtProcMethod);
  ASSERT_NE(side_stream, nullptr);
  PushClientMessage(client, NewMessage(kMessage1));
  EXPECT_THAT(GetNextSideStreamRequest(*side_stream),
              Optional(MatchesRequestBody(kMessage1, !kEndOfStream)));
  side_stream->SendMessageToClient(
      MakeRequestHeadersMutationResponse({}).SerializeAsString());
  EXPECT_EQ(
      PullServerTrailingStatus(client),
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
  CallInitiator client = StartCall(NewClientMetadata({}, kPath));
  auto side_stream = transport_factory_->WaitForStream(target_, kExtProcMethod);
  ASSERT_NE(side_stream, nullptr);
  EXPECT_THAT(GetNextSideStreamRequest(*side_stream),
              Optional(MatchesRequestHeaders(Contains(Pair(":path", kPath)))));
  side_stream->SendMessageToClient(
      MakeRequestBodyMutationResponse("").SerializeAsString());
  EXPECT_EQ(
      PullServerTrailingStatus(client),
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
  CallInitiator client = StartCall(NewClientMetadata({}, kPath));
  std::optional<CallHandler> backend = GetNextHandler();
  ASSERT_TRUE(backend.has_value());
  auto side_stream = transport_factory_->WaitForStream(target_, kExtProcMethod);
  ASSERT_NE(side_stream, nullptr);
  PushServerInitialMetadata(*backend, NewServerMetadata());
  PushServerMessage(*backend, NewMessage(kMessage1));
  EXPECT_THAT(GetNextSideStreamRequest(*side_stream),
              Optional(MatchesResponseBody(kMessage1, !kEndOfStream)));
  side_stream->SendMessageToClient(
      MakeResponseHeadersMutationResponse({}).SerializeAsString());
  EXPECT_EQ(
      PullServerTrailingStatus(client),
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
  CallInitiator client = StartCall(NewClientMetadata({}, kPath));
  std::optional<CallHandler> backend = GetNextHandler();
  ASSERT_TRUE(backend.has_value());
  auto side_stream = transport_factory_->WaitForStream(target_, kExtProcMethod);
  ASSERT_NE(side_stream, nullptr);
  PushServerInitialMetadata(*backend, NewServerMetadata());
  EXPECT_THAT(GetNextSideStreamRequest(*side_stream),
              Optional(MatchesResponseHeaders(_)));
  side_stream->SendMessageToClient(
      MakeResponseBodyMutationResponse("").SerializeAsString());
  EXPECT_EQ(
      PullServerTrailingStatus(client),
      absl::InternalError("Received unexpected response body response from "
                          "external processor"));
  WaitForAllPendingWork();
}

FILTER_TEST(ExtProcFilterTest, ServerToClientOrderingTrailersBeforeHeaders) {
  Init(ConfigBuilder()
           .SetRequestHeaderMode(false)
           .SetResponseHeaderMode(true)
           .SetResponseTrailerMode(true));
  CallInitiator client = StartCall(NewClientMetadata({}, kPath));
  std::optional<CallHandler> backend = GetNextHandler();
  ASSERT_TRUE(backend.has_value());
  auto side_stream = transport_factory_->WaitForStream(target_, kExtProcMethod);
  ASSERT_NE(side_stream, nullptr);
  PushServerInitialMetadata(*backend, NewServerMetadata());
  EXPECT_THAT(GetNextSideStreamRequest(*side_stream),
              Optional(MatchesResponseHeaders(_)));
  side_stream->SendMessageToClient(
      MakeResponseTrailersMutationResponse({}).SerializeAsString());
  EXPECT_EQ(
      PullServerTrailingStatus(client),
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
  CallInitiator client = StartCall(NewClientMetadata({}, kPath));
  std::optional<CallHandler> backend = GetNextHandler();
  ASSERT_TRUE(backend.has_value());
  auto side_stream = transport_factory_->WaitForStream(target_, kExtProcMethod);
  ASSERT_NE(side_stream, nullptr);
  PushServerInitialMetadata(*backend, NewServerMetadata());
  EXPECT_THAT(GetNextSideStreamRequest(*side_stream),
              Optional(MatchesResponseHeaders(_)));
  side_stream->SendMessageToClient(
      MakeResponseHeadersMutationResponse({}).SerializeAsString());
  EXPECT_TRUE(PullServerInitialMetadata(client).ok());
  PushServerMessage(*backend, NewMessage(kMessage1));
  EXPECT_THAT(GetNextSideStreamRequest(*side_stream),
              Optional(MatchesResponseBody(kMessage1, !kEndOfStream)));
  side_stream->SendMessageToClient(
      MakeResponseTrailersMutationResponse({}).SerializeAsString());
  EXPECT_THAT(PullServerMessage(client), Not(IsMessage(_)));
  EXPECT_EQ(
      PullServerTrailingStatus(client),
      absl::InternalError("Received response trailers response before all "
                          "outstanding response body responses were received"));
  WaitForAllPendingWork();
}

FILTER_TEST(ExtProcFilterTest,
            ServerToClientOrderingTrailersResponseWhenDisabled) {
  Init(ConfigBuilder().SetRequestHeaderMode(false).SetResponseHeaderMode(true));
  CallInitiator client = StartCall(NewClientMetadata({}, kPath));
  std::optional<CallHandler> backend = GetNextHandler();
  ASSERT_TRUE(backend.has_value());
  auto side_stream = transport_factory_->WaitForStream(target_, kExtProcMethod);
  ASSERT_NE(side_stream, nullptr);
  PushServerInitialMetadata(*backend, NewServerMetadata());
  EXPECT_THAT(GetNextSideStreamRequest(*side_stream),
              Optional(MatchesResponseHeaders(_)));
  // The ext_proc server's responses arrive in order on the side stream, so
  // the trailers response is always processed after the headers response.
  side_stream->SendMessageToClient(
      MakeResponseHeadersMutationResponse({}).SerializeAsString());
  side_stream->SendMessageToClient(
      MakeResponseTrailersMutationResponse({}).SerializeAsString());
  EXPECT_EQ(
      PullServerTrailingStatus(client),
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
  CallInitiator client = StartCall(NewClientMetadata({}, kPath));
  std::optional<CallHandler> backend = GetNextHandler();
  ASSERT_TRUE(backend.has_value());
  auto side_stream = transport_factory_->WaitForStream(target_, kExtProcMethod);
  ASSERT_NE(side_stream, nullptr);
  PushServerInitialMetadata(*backend, NewServerMetadata());
  EXPECT_THAT(GetNextSideStreamRequest(*side_stream),
              Optional(MatchesResponseHeaders(_)));
  side_stream->SendMessageToClient(
      MakeResponseHeadersMutationResponse({}).SerializeAsString());
  EXPECT_TRUE(PullServerInitialMetadata(client).ok());
  PushServerMessage(*backend, NewMessage(kMessage1));
  EXPECT_THAT(GetNextSideStreamRequest(*side_stream),
              Optional(MatchesResponseBody(kMessage1, !kEndOfStream)));
  side_stream->SendMessageToClient(
      MakeResponseBodyMutationResponse("", kEndOfStream).SerializeAsString());
  EXPECT_THAT(PullServerMessage(client), Not(IsMessage(_)));
  EXPECT_EQ(PullServerTrailingStatus(client),
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
  CallInitiator client = StartCall(NewClientMetadata({}, kPath));
  auto side_stream = transport_factory_->WaitForStream(target_, kExtProcMethod);
  ASSERT_NE(side_stream, nullptr);
  EXPECT_THAT(GetNextSideStreamRequest(*side_stream),
              Optional(MatchesRequestHeaders(Contains(Pair(":path", kPath)))));
  // Nothing needs draining, so the call carries on unmodified.
  side_stream->MaybeSendStatusToClient(absl::OkStatus());
  std::optional<CallHandler> backend = GetNextHandler();
  ASSERT_TRUE(backend.has_value());
  EXPECT_TRUE(PullClientInitialMetadata(*backend).ok());
  PushClientMessage(client, NewMessage(kMessage1));
  EXPECT_THAT(PullClientMessage(*backend), IsMessage(kMessage1));
  PushClientHalfClose(client);
  EXPECT_THAT(PullClientMessage(*backend), IsEndOfStream());
  PushServerInitialMetadata(*backend, NewServerMetadata());
  PushServerMessage(*backend, NewMessage(kMessage1));
  EXPECT_TRUE(PullServerInitialMetadata(client).ok());
  EXPECT_THAT(PullServerMessage(client), IsMessage(kMessage1));
  PushServerTrailingMetadata(*backend,
                             ServerMetadataFromStatus(GRPC_STATUS_OK));
  EXPECT_EQ(PullServerTrailingStatus(client), absl::OkStatus());
  EXPECT_EQ(GetNextSideStreamRequest(*side_stream), std::nullopt);
  WaitForAllPendingWork();
}

FILTER_TEST(ExtProcFilterTest, StreamCleanCloseBodiesDrainedSuccess) {
  Init(ConfigBuilder()
           .SetRequestHeaderMode(false)
           .SetResponseHeaderMode(false)
           .SetResponseTrailerMode(true)
           .SetRequestBodyMode(true)
           .SetResponseBodyMode(true));
  CallInitiator client = StartCall(NewClientMetadata({}, kPath));
  std::optional<CallHandler> backend = GetNextHandler();
  ASSERT_TRUE(backend.has_value());
  auto side_stream = transport_factory_->WaitForStream(target_, kExtProcMethod);
  ASSERT_NE(side_stream, nullptr);
  EXPECT_TRUE(PullClientInitialMetadata(*backend).ok());
  PushClientMessage(client, NewMessage(kMessage1));
  EXPECT_THAT(GetNextSideStreamRequest(*side_stream),
              Optional(MatchesRequestBody(kMessage1, !kEndOfStream)));
  side_stream->SendMessageToClient(
      SetRequestDrains(
          MakeRequestBodyMutationResponse(kMessage1Mutated, !kEndOfStream),
          /*drain_requests=*/true, /*drain_responses=*/false)
          .SerializeAsString());
  EXPECT_THAT(GetNextSideStreamRequest(*side_stream),
              Optional(MatchesRequestBodyDrainComplete()));
  side_stream->SendMessageToClient(
      MakeRequestBodyDrainCompleteResponse().SerializeAsString());
  EXPECT_THAT(PullClientMessage(*backend), IsMessage(kMessage1Mutated));
  PushServerInitialMetadata(*backend, NewServerMetadata());
  PushServerMessage(*backend, NewMessage(kMessage1Mutated));
  EXPECT_THAT(GetNextSideStreamRequest(*side_stream),
              Optional(MatchesResponseBody(kMessage1Mutated, !kEndOfStream)));
  side_stream->SendMessageToClient(
      SetRequestDrains(MakeResponseBodyMutationResponse(kMessage1DoubleMutated,
                                                        !kEndOfStream),
                       /*drain_requests=*/false, /*drain_responses=*/true)
          .SerializeAsString());
  EXPECT_THAT(GetNextSideStreamRequest(*side_stream),
              Optional(MatchesResponseBodyDrainComplete()));
  side_stream->SendMessageToClient(
      MakeResponseBodyDrainCompleteResponse().SerializeAsString());
  EXPECT_TRUE(PullServerInitialMetadata(client).ok());
  EXPECT_THAT(PullServerMessage(client), IsMessage(kMessage1DoubleMutated));
  // The fake transport delivers the status immediately but queues messages
  // until the filter reads them, so let the filter read the drain_complete
  // response before the stream is closed.
  event_engine()->TickUntilIdle();
  // Both directions are drained, so the ext_proc server may close the stream
  // cleanly.
  side_stream->MaybeSendStatusToClient(absl::OkStatus());
  PushClientHalfClose(client);
  EXPECT_THAT(PullClientMessage(*backend), IsEndOfStream());
  PushServerTrailingMetadata(*backend,
                             ServerMetadataFromStatus(GRPC_STATUS_OK));
  EXPECT_EQ(PullServerTrailingStatus(client), absl::OkStatus());
  EXPECT_EQ(GetNextSideStreamRequest(*side_stream), std::nullopt);
  WaitForAllPendingWork();
}

FILTER_TEST(ExtProcFilterTest,
            StreamCleanCloseRequestBodyInFlightObservabilitySuccess) {
  Init(ConfigBuilder()
           .SetObservabilityMode(true)
           .SetRequestHeaderMode(false)
           .SetResponseHeaderMode(false)
           .SetRequestBodyMode(true));
  CallInitiator client = StartCall(NewClientMetadata({}, kPath));
  std::optional<CallHandler> backend = GetNextHandler();
  ASSERT_TRUE(backend.has_value());
  auto side_stream = transport_factory_->WaitForStream(target_, kExtProcMethod);
  ASSERT_NE(side_stream, nullptr);
  EXPECT_TRUE(PullClientInitialMetadata(*backend).ok());
  PushClientMessage(client, NewMessage(kMessage1));
  EXPECT_THAT(GetNextSideStreamRequest(*side_stream),
              Optional(MatchesRequestBody(kMessage1, !kEndOfStream)));
  side_stream->MaybeSendStatusToClient(absl::OkStatus());
  EXPECT_THAT(PullClientMessage(*backend), IsMessage(kMessage1));
  PushServerInitialMetadata(*backend, NewServerMetadata());
  PushServerMessage(*backend, NewMessage(kMessage1));
  EXPECT_TRUE(PullServerInitialMetadata(client).ok());
  EXPECT_THAT(PullServerMessage(client), IsMessage(kMessage1));
  PushClientMessage(client, NewMessage(kMessage2));
  EXPECT_THAT(PullClientMessage(*backend), IsMessage(kMessage2));
  PushServerMessage(*backend, NewMessage(kMessage2));
  EXPECT_THAT(PullServerMessage(client), IsMessage(kMessage2));
  PushClientHalfClose(client);
  EXPECT_THAT(PullClientMessage(*backend), IsEndOfStream());
  PushServerTrailingMetadata(*backend,
                             ServerMetadataFromStatus(GRPC_STATUS_OK));
  EXPECT_EQ(PullServerTrailingStatus(client), absl::OkStatus());
  EXPECT_EQ(GetNextSideStreamRequest(*side_stream), std::nullopt);
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
  CallInitiator client = StartCall(NewClientMetadata({}, kPath));
  std::optional<CallHandler> backend = GetNextHandler();
  ASSERT_TRUE(backend.has_value());
  auto side_stream = transport_factory_->WaitForStream(target_, kExtProcMethod);
  ASSERT_NE(side_stream, nullptr);
  EXPECT_TRUE(PullClientInitialMetadata(*backend).ok());
  PushClientMessage(client, NewMessage(kMessage1));
  EXPECT_THAT(PullClientMessage(*backend), IsMessage(kMessage1));
  PushServerInitialMetadata(*backend, NewServerMetadata());
  PushServerMessage(*backend, NewMessage(kMessage1));
  EXPECT_THAT(GetNextSideStreamRequest(*side_stream),
              Optional(MatchesResponseBody(kMessage1, !kEndOfStream)));
  side_stream->MaybeSendStatusToClient(absl::OkStatus());
  EXPECT_TRUE(PullServerInitialMetadata(client).ok());
  EXPECT_THAT(PullServerMessage(client), IsMessage(kMessage1));
  PushClientHalfClose(client);
  EXPECT_THAT(PullClientMessage(*backend), IsEndOfStream());
  PushServerTrailingMetadata(*backend,
                             ServerMetadataFromStatus(GRPC_STATUS_OK));
  EXPECT_EQ(PullServerTrailingStatus(client), absl::OkStatus());
  EXPECT_EQ(GetNextSideStreamRequest(*side_stream), std::nullopt);
  WaitForAllPendingWork();
}

FILTER_TEST(ExtProcFilterTest, StreamCleanCloseRequestBodyNotDrainedFails) {
  Init(ConfigBuilder()
           .SetRequestHeaderMode(false)
           .SetResponseHeaderMode(false)
           .SetRequestBodyMode(true));
  CallInitiator client = StartCall(NewClientMetadata({}, kPath));
  std::optional<CallHandler> backend = GetNextHandler();
  ASSERT_TRUE(backend.has_value());
  auto side_stream = transport_factory_->WaitForStream(target_, kExtProcMethod);
  ASSERT_NE(side_stream, nullptr);
  PushClientMessage(client, NewMessage(kMessage1));
  EXPECT_THAT(GetNextSideStreamRequest(*side_stream),
              Optional(MatchesRequestBody(kMessage1, !kEndOfStream)));
  side_stream->MaybeSendStatusToClient(absl::OkStatus());
  EXPECT_EQ(PullServerTrailingStatus(client),
            absl::InternalError("Stream closed cleanly without drain"));
  WaitForAllPendingWork();
}

FILTER_TEST(ExtProcFilterTest, StreamCleanCloseResponseBodyNotDrainedFails) {
  Init(ConfigBuilder()
           .SetRequestHeaderMode(false)
           .SetResponseHeaderMode(false)
           .SetResponseTrailerMode(true)
           .SetResponseBodyMode(true));
  CallInitiator client = StartCall(NewClientMetadata({}, kPath));
  std::optional<CallHandler> backend = GetNextHandler();
  ASSERT_TRUE(backend.has_value());
  auto side_stream = transport_factory_->WaitForStream(target_, kExtProcMethod);
  ASSERT_NE(side_stream, nullptr);
  EXPECT_TRUE(PullClientInitialMetadata(*backend).ok());
  PushClientMessage(client, NewMessage(kMessage1));
  EXPECT_THAT(PullClientMessage(*backend), IsMessage(kMessage1));
  PushServerInitialMetadata(*backend, NewServerMetadata());
  PushServerMessage(*backend, NewMessage(kMessage1));
  EXPECT_THAT(GetNextSideStreamRequest(*side_stream),
              Optional(MatchesResponseBody(kMessage1, !kEndOfStream)));
  side_stream->SendMessageToClient(
      MakeResponseBodyMutationResponse(kMessage1).SerializeAsString());
  EXPECT_TRUE(PullServerInitialMetadata(client).ok());
  EXPECT_THAT(PullServerMessage(client), IsMessage(kMessage1));
  PushClientMessage(client, NewMessage(kMessage2));
  EXPECT_THAT(PullClientMessage(*backend), IsMessage(kMessage2));
  PushServerMessage(*backend, NewMessage(kMessage2));
  EXPECT_THAT(GetNextSideStreamRequest(*side_stream),
              Optional(MatchesResponseBody(kMessage2, !kEndOfStream)));
  side_stream->MaybeSendStatusToClient(absl::OkStatus());
  PushClientHalfClose(client);
  EXPECT_EQ(PullServerTrailingStatus(client),
            absl::InternalError("Stream closed cleanly without drain"));
  WaitForAllPendingWork();
}

FILTER_TEST(ExtProcFilterTest,
            StreamCleanCloseDuringRequestBodyDrainInFlightFails) {
  Init(ConfigBuilder()
           .SetRequestHeaderMode(false)
           .SetResponseHeaderMode(false)
           .SetRequestBodyMode(true));
  CallInitiator client = StartCall(NewClientMetadata({}, kPath));
  std::optional<CallHandler> backend = GetNextHandler();
  ASSERT_TRUE(backend.has_value());
  auto side_stream = transport_factory_->WaitForStream(target_, kExtProcMethod);
  ASSERT_NE(side_stream, nullptr);
  EXPECT_TRUE(PullClientInitialMetadata(*backend).ok());
  PushClientMessage(client, NewMessage(kMessage1));
  EXPECT_THAT(GetNextSideStreamRequest(*side_stream),
              Optional(MatchesRequestBody(kMessage1, !kEndOfStream)));
  side_stream->SendMessageToClient(
      SetRequestDrains(
          MakeRequestBodyMutationResponse(kMessage1, !kEndOfStream),
          /*drain_requests=*/true, /*drain_responses=*/false)
          .SerializeAsString());
  EXPECT_THAT(GetNextSideStreamRequest(*side_stream),
              Optional(MatchesRequestBodyDrainComplete()));
  // The ext_proc server closes the stream without acknowledging the drain.
  side_stream->MaybeSendStatusToClient(absl::OkStatus());
  EXPECT_EQ(PullServerTrailingStatus(client),
            absl::InternalError("Stream closed cleanly without drain"));
  WaitForAllPendingWork();
}

FILTER_TEST(ExtProcFilterTest,
            StreamCleanCloseDuringResponseBodyDrainInFlightFails) {
  Init(ConfigBuilder()
           .SetRequestHeaderMode(false)
           .SetResponseHeaderMode(false)
           .SetResponseTrailerMode(true)
           .SetResponseBodyMode(true));
  CallInitiator client = StartCall(NewClientMetadata({}, kPath));
  std::optional<CallHandler> backend = GetNextHandler();
  ASSERT_TRUE(backend.has_value());
  auto side_stream = transport_factory_->WaitForStream(target_, kExtProcMethod);
  ASSERT_NE(side_stream, nullptr);
  EXPECT_TRUE(PullClientInitialMetadata(*backend).ok());
  PushClientMessage(client, NewMessage(kMessage1));
  EXPECT_THAT(PullClientMessage(*backend), IsMessage(kMessage1));
  PushServerInitialMetadata(*backend, NewServerMetadata());
  PushServerMessage(*backend, NewMessage(kMessage1));
  EXPECT_THAT(GetNextSideStreamRequest(*side_stream),
              Optional(MatchesResponseBody(kMessage1, !kEndOfStream)));
  side_stream->SendMessageToClient(
      SetRequestDrains(
          MakeResponseBodyMutationResponse(kMessage1, !kEndOfStream),
          /*drain_requests=*/false, /*drain_responses=*/true)
          .SerializeAsString());
  EXPECT_THAT(GetNextSideStreamRequest(*side_stream),
              Optional(MatchesResponseBodyDrainComplete()));
  // The ext_proc server closes the stream without acknowledging the drain.
  side_stream->MaybeSendStatusToClient(absl::OkStatus());
  PushClientHalfClose(client);
  EXPECT_EQ(PullServerTrailingStatus(client),
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
  CallInitiator client = StartCall(NewClientMetadata({}, kPath));
  auto side_stream = transport_factory_->WaitForStream(target_, kExtProcMethod);
  ASSERT_NE(side_stream, nullptr);
  EXPECT_THAT(GetNextSideStreamRequest(*side_stream),
              Optional(MatchesRequestHeaders(Contains(Pair(":path", kPath)))));
  side_stream->SendMessageToClient(
      SetRequestDrains(MakeRequestHeadersMutationResponse({}),
                       /*drain_requests=*/true, /*drain_responses=*/true)
          .SerializeAsString());
  EXPECT_THAT(GetNextSideStreamRequest(*side_stream),
              Optional(MatchesRequestBodyDrainComplete()));
  side_stream->SendMessageToClient(
      MakeRequestBodyDrainCompleteResponse().SerializeAsString());
  EXPECT_THAT(GetNextSideStreamRequest(*side_stream),
              Optional(MatchesResponseBodyDrainComplete()));
  side_stream->SendMessageToClient(
      MakeResponseBodyDrainCompleteResponse().SerializeAsString());
  std::optional<CallHandler> backend = GetNextHandler();
  ASSERT_TRUE(backend.has_value());
  // The fake transport delivers the status immediately but queues messages
  // until the filter reads them, so let the filter read the drain_complete
  // response before the stream is closed.
  event_engine()->TickUntilIdle();
  side_stream->MaybeSendStatusToClient(absl::OkStatus());
  // Let the filter see the side stream close before any body is sent.
  event_engine()->TickUntilIdle();
  EXPECT_TRUE(PullClientInitialMetadata(*backend).ok());
  PushClientMessage(client, NewMessage(kMessage1));
  EXPECT_THAT(PullClientMessage(*backend), IsMessage(kMessage1));
  PushClientHalfClose(client);
  EXPECT_THAT(PullClientMessage(*backend), IsEndOfStream());
  PushServerInitialMetadata(*backend, NewServerMetadata());
  PushServerMessage(*backend, NewMessage(kMessage1));
  EXPECT_TRUE(PullServerInitialMetadata(client).ok());
  EXPECT_THAT(PullServerMessage(client), IsMessage(kMessage1));
  PushServerTrailingMetadata(*backend,
                             ServerMetadataFromStatus(GRPC_STATUS_OK));
  EXPECT_EQ(PullServerTrailingStatus(client), absl::OkStatus());
  EXPECT_EQ(GetNextSideStreamRequest(*side_stream), std::nullopt);
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
  CallInitiator client = StartCall(NewClientMetadata({}, kPath));
  auto side_stream = transport_factory_->WaitForStream(target_, kExtProcMethod);
  ASSERT_NE(side_stream, nullptr);
  EXPECT_THAT(GetNextSideStreamRequest(*side_stream),
              Optional(MatchesRequestHeaders(Contains(Pair(":path", kPath)))));
  side_stream->MaybeSendStatusToClient(
      absl::UnavailableError("Call closed by ext_proc server on headers"));
  std::optional<CallHandler> backend = GetNextHandler();
  ASSERT_TRUE(backend.has_value());
  EXPECT_TRUE(PullClientInitialMetadata(*backend).ok());
  PushClientMessage(client, NewMessage(kMessage1));
  EXPECT_THAT(PullClientMessage(*backend), IsMessage(kMessage1));
  PushClientHalfClose(client);
  EXPECT_THAT(PullClientMessage(*backend), IsEndOfStream());
  PushServerInitialMetadata(*backend, NewServerMetadata());
  PushServerMessage(*backend, NewMessage(kMessage1));
  EXPECT_TRUE(PullServerInitialMetadata(client).ok());
  EXPECT_THAT(PullServerMessage(client), IsMessage(kMessage1));
  PushServerTrailingMetadata(*backend,
                             ServerMetadataFromStatus(GRPC_STATUS_OK));
  EXPECT_EQ(PullServerTrailingStatus(client), absl::OkStatus());
  WaitForAllPendingWork();
}

FILTER_TEST(ExtProcFilterTest,
            StreamErrorFailureModeAllowObservabilitySuccess) {
  Init(ConfigBuilder()
           .SetObservabilityMode(true)
           .SetFailureModeAllow(true)
           .EnableAllProcessingModes());
  CallInitiator client = StartCall(NewClientMetadata({}, kPath));
  auto side_stream = transport_factory_->WaitForStream(target_, kExtProcMethod);
  ASSERT_NE(side_stream, nullptr);
  EXPECT_THAT(GetNextSideStreamRequest(*side_stream),
              Optional(MatchesRequestHeaders(Contains(Pair(":path", kPath)))));
  std::optional<CallHandler> backend = GetNextHandler();
  ASSERT_TRUE(backend.has_value());
  EXPECT_TRUE(PullClientInitialMetadata(*backend).ok());
  PushClientMessage(client, NewMessage(kMessage1));
  EXPECT_THAT(GetNextSideStreamRequest(*side_stream),
              Optional(MatchesRequestBody(kMessage1, !kEndOfStream)));
  side_stream->MaybeSendStatusToClient(absl::ResourceExhaustedError(
      "Call closed by ext_proc server on request body"));
  EXPECT_THAT(PullClientMessage(*backend), IsMessage(kMessage1));
  PushClientHalfClose(client);
  EXPECT_THAT(PullClientMessage(*backend), IsEndOfStream());
  PushServerInitialMetadata(*backend, NewServerMetadata());
  PushServerMessage(*backend, NewMessage(kMessage1));
  EXPECT_TRUE(PullServerInitialMetadata(client).ok());
  EXPECT_THAT(PullServerMessage(client), IsMessage(kMessage1));
  PushServerTrailingMetadata(*backend,
                             ServerMetadataFromStatus(GRPC_STATUS_OK));
  EXPECT_EQ(PullServerTrailingStatus(client), absl::OkStatus());
  WaitForAllPendingWork();
}

FILTER_TEST(ExtProcFilterTest,
            StreamErrorFailureModeAllowRequestBodyNotDrainedFails) {
  Init(ConfigBuilder()
           .SetFailureModeAllow(true)
           .SetRequestHeaderMode(false)
           .SetResponseHeaderMode(false)
           .SetRequestBodyMode(true));
  CallInitiator client = StartCall(NewClientMetadata({}, kPath));
  std::optional<CallHandler> backend = GetNextHandler();
  ASSERT_TRUE(backend.has_value());
  auto side_stream = transport_factory_->WaitForStream(target_, kExtProcMethod);
  ASSERT_NE(side_stream, nullptr);
  PushClientMessage(client, NewMessage(kMessage1));
  EXPECT_THAT(GetNextSideStreamRequest(*side_stream),
              Optional(MatchesRequestBody(kMessage1, !kEndOfStream)));
  // Fail-open doesn't apply once a body has been sent.
  side_stream->MaybeSendStatusToClient(absl::ResourceExhaustedError(
      "Call closed by ext_proc server on request body"));
  EXPECT_EQ(PullServerTrailingStatus(client),
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
  CallInitiator client = StartCall(NewClientMetadata({}, kPath));
  std::optional<CallHandler> backend = GetNextHandler();
  ASSERT_TRUE(backend.has_value());
  auto side_stream = transport_factory_->WaitForStream(target_, kExtProcMethod);
  ASSERT_NE(side_stream, nullptr);
  PushServerInitialMetadata(*backend, NewServerMetadata());
  PushServerMessage(*backend, NewMessage(kMessage1));
  EXPECT_THAT(GetNextSideStreamRequest(*side_stream),
              Optional(MatchesResponseBody(kMessage1, !kEndOfStream)));
  side_stream->MaybeSendStatusToClient(absl::ResourceExhaustedError(
      "Call closed by ext_proc server on response body"));
  EXPECT_EQ(PullServerTrailingStatus(client),
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
  CallInitiator client = StartCall(NewClientMetadata({}, kPath));
  auto side_stream = transport_factory_->WaitForStream(target_, kExtProcMethod);
  ASSERT_NE(side_stream, nullptr);
  EXPECT_THAT(GetNextSideStreamRequest(*side_stream),
              Optional(MatchesRequestHeaders(Contains(Pair(":path", kPath)))));
  side_stream->SendMessageToClient(
      SetRequestDrains(MakeRequestHeadersMutationResponse({}),
                       /*drain_requests=*/true, /*drain_responses=*/true)
          .SerializeAsString());
  EXPECT_THAT(GetNextSideStreamRequest(*side_stream),
              Optional(MatchesRequestBodyDrainComplete()));
  EXPECT_THAT(GetNextSideStreamRequest(*side_stream),
              Optional(MatchesResponseBodyDrainComplete()));
  std::optional<CallHandler> backend = GetNextHandler();
  ASSERT_TRUE(backend.has_value());
  // The ext_proc server fails the stream before acknowledging the drains.
  side_stream->MaybeSendStatusToClient(absl::ResourceExhaustedError(
      "Call closed by ext_proc server after drain"));
  EXPECT_TRUE(PullClientInitialMetadata(*backend).ok());
  PushClientMessage(client, NewMessage(kMessage1));
  EXPECT_THAT(PullClientMessage(*backend), IsMessage(kMessage1));
  PushClientHalfClose(client);
  EXPECT_THAT(PullClientMessage(*backend), IsEndOfStream());
  PushServerInitialMetadata(*backend, NewServerMetadata());
  PushServerMessage(*backend, NewMessage(kMessage1));
  EXPECT_TRUE(PullServerInitialMetadata(client).ok());
  EXPECT_THAT(PullServerMessage(client), IsMessage(kMessage1));
  PushServerTrailingMetadata(*backend,
                             ServerMetadataFromStatus(GRPC_STATUS_OK));
  EXPECT_EQ(PullServerTrailingStatus(client), absl::OkStatus());
  EXPECT_EQ(GetNextSideStreamRequest(*side_stream), std::nullopt);
  WaitForAllPendingWork();
}

FILTER_TEST(ExtProcFilterTest, StreamErrorFailureModeFalseFails) {
  Init(ConfigBuilder().SetRequestHeaderMode(true).SetResponseHeaderMode(false));
  CallInitiator client = StartCall(NewClientMetadata({}, kPath));
  auto side_stream = transport_factory_->WaitForStream(target_, kExtProcMethod);
  ASSERT_NE(side_stream, nullptr);
  EXPECT_THAT(GetNextSideStreamRequest(*side_stream),
              Optional(MatchesRequestHeaders(Contains(Pair(":path", kPath)))));
  side_stream->MaybeSendStatusToClient(absl::UnavailableError(
      "Call closed by ext_proc server on request headers"));
  EXPECT_EQ(
      PullServerTrailingStatus(client),
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
  CallInitiator client = StartCall(NewClientMetadata({}, kPath));
  auto side_stream = transport_factory_->WaitForStream(target_, kExtProcMethod);
  ASSERT_NE(side_stream, nullptr);
  EXPECT_THAT(GetNextSideStreamRequest(*side_stream),
              Optional(MatchesRequestHeaders(Contains(Pair(":path", kPath)))));
  // The filter waits for the ext_proc server's response.
  event_engine()->TickForDuration(std::chrono::seconds(2));
  side_stream->SendMessageToClient(
      MakeRequestHeadersMutationResponse({}).SerializeAsString());
  std::optional<CallHandler> backend = GetNextHandler();
  ASSERT_TRUE(backend.has_value());
  EXPECT_TRUE(PullClientInitialMetadata(*backend).ok());
  PushServerInitialMetadata(*backend, NewServerMetadata());
  EXPECT_TRUE(PullServerInitialMetadata(client).ok());
  PushServerTrailingMetadata(*backend,
                             ServerMetadataFromStatus(GRPC_STATUS_OK));
  EXPECT_EQ(PullServerTrailingStatus(client), absl::OkStatus());
  EXPECT_THAT(stats_plugin->GetHistogramValueByName(
                  "grpc.client_ext_proc.client_headers_duration", {kServerUri}),
              Optional(ElementsAre(0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
                                   0, 0, 0, 0, 0)));
  WaitForAllPendingWork();
}

FILTER_TEST(ExtProcFilterTest, ExtProcClientHalfCloseDurationMetric) {
  auto stats_plugin = InitWithStatsPlugin(ConfigBuilder()
                                              .SetRequestHeaderMode(false)
                                              .SetResponseHeaderMode(false)
                                              .SetRequestBodyMode(true));
  CallInitiator client = StartCall(NewClientMetadata({}, kPath));
  std::optional<CallHandler> backend = GetNextHandler();
  ASSERT_TRUE(backend.has_value());
  auto side_stream = transport_factory_->WaitForStream(target_, kExtProcMethod);
  ASSERT_NE(side_stream, nullptr);
  EXPECT_TRUE(PullClientInitialMetadata(*backend).ok());
  PushClientMessage(client, NewMessage(kMessage1));
  EXPECT_THAT(GetNextSideStreamRequest(*side_stream),
              Optional(MatchesRequestBody(kMessage1, !kEndOfStream)));
  side_stream->SendMessageToClient(
      MakeRequestBodyMutationResponse(kMessage1).SerializeAsString());
  EXPECT_THAT(PullClientMessage(*backend), IsMessage(kMessage1));
  PushClientHalfClose(client);
  EXPECT_THAT(GetNextSideStreamRequest(*side_stream),
              Optional(MatchesRequestBody(kEmptyBody, kEndOfStream)));
  // The filter waits for the ext_proc server's response.
  event_engine()->TickForDuration(std::chrono::seconds(2));
  side_stream->SendMessageToClient(
      MakeRequestBodyMutationResponse(kEmptyBody, kEndOfStream)
          .SerializeAsString());
  EXPECT_THAT(PullClientMessage(*backend), IsMessage(kEmptyBody));
  EXPECT_THAT(PullClientMessage(*backend), IsEndOfStream());
  PushServerInitialMetadata(*backend, NewServerMetadata());
  EXPECT_TRUE(PullServerInitialMetadata(client).ok());
  PushServerTrailingMetadata(*backend,
                             ServerMetadataFromStatus(GRPC_STATUS_OK));
  EXPECT_EQ(PullServerTrailingStatus(client), absl::OkStatus());
  EXPECT_THAT(
      stats_plugin->GetHistogramValueByName(
          "grpc.client_ext_proc.client_half_close_duration", {kServerUri}),
      Optional(ElementsAre(0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
                           0, 0)));
  WaitForAllPendingWork();
}

FILTER_TEST(ExtProcFilterTest,
            ExtProcClientHalfCloseDurationMetricAfterRequestBodyDrain) {
  auto stats_plugin = InitWithStatsPlugin(ConfigBuilder()
                                              .SetRequestHeaderMode(false)
                                              .SetResponseHeaderMode(false)
                                              .SetRequestBodyMode(true));
  CallInitiator client = StartCall(NewClientMetadata({}, kPath));
  std::optional<CallHandler> backend = GetNextHandler();
  ASSERT_TRUE(backend.has_value());
  auto side_stream = transport_factory_->WaitForStream(target_, kExtProcMethod);
  ASSERT_NE(side_stream, nullptr);
  EXPECT_TRUE(PullClientInitialMetadata(*backend).ok());
  PushClientMessage(client, NewMessage(kMessage1));
  EXPECT_THAT(GetNextSideStreamRequest(*side_stream),
              Optional(MatchesRequestBody(kMessage1, !kEndOfStream)));
  side_stream->SendMessageToClient(
      SetRequestDrains(
          MakeRequestBodyMutationResponse(kMessage1, !kEndOfStream),
          /*drain_requests=*/true, /*drain_responses=*/false)
          .SerializeAsString());
  EXPECT_THAT(GetNextSideStreamRequest(*side_stream),
              Optional(MatchesRequestBodyDrainComplete()));
  side_stream->SendMessageToClient(
      MakeRequestBodyDrainCompleteResponse().SerializeAsString());
  EXPECT_THAT(PullClientMessage(*backend), IsMessage(kMessage1));
  PushClientMessage(client, NewMessage(kMessage2));
  EXPECT_THAT(PullClientMessage(*backend), IsMessage(kMessage2));
  PushClientHalfClose(client);
  EXPECT_THAT(PullClientMessage(*backend), IsEndOfStream());
  PushServerInitialMetadata(*backend, NewServerMetadata());
  EXPECT_TRUE(PullServerInitialMetadata(client).ok());
  PushServerTrailingMetadata(*backend,
                             ServerMetadataFromStatus(GRPC_STATUS_OK));
  EXPECT_EQ(PullServerTrailingStatus(client), absl::OkStatus());
  // The duration is recorded only when the filter holds the half-close while
  // waiting on the ext_proc server. Once requests are drained, the half-close
  // is forwarded immediately, so no sample is recorded.
  // TODO(rishesh): Confirm that this is intended: the e2e version of this test
  // only checks that the histogram exists.
  EXPECT_THAT(
      stats_plugin->GetHistogramValueByName(
          "grpc.client_ext_proc.client_half_close_duration", {kServerUri}),
      Optional(ElementsAre(0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
                           0, 0)));
  WaitForAllPendingWork();
}

FILTER_TEST(ExtProcFilterTest, ExtProcServerHeadersDurationMetric) {
  auto stats_plugin = InitWithStatsPlugin(
      ConfigBuilder().SetRequestHeaderMode(false).SetResponseHeaderMode(true));
  CallInitiator client = StartCall(NewClientMetadata({}, kPath));
  std::optional<CallHandler> backend = GetNextHandler();
  ASSERT_TRUE(backend.has_value());
  auto side_stream = transport_factory_->WaitForStream(target_, kExtProcMethod);
  ASSERT_NE(side_stream, nullptr);
  PushServerInitialMetadata(*backend, NewServerMetadata());
  EXPECT_THAT(GetNextSideStreamRequest(*side_stream),
              Optional(MatchesResponseHeaders(_)));
  // The filter waits for the ext_proc server's response.
  event_engine()->TickForDuration(std::chrono::seconds(2));
  side_stream->SendMessageToClient(
      MakeResponseHeadersMutationResponse({}).SerializeAsString());
  EXPECT_TRUE(PullServerInitialMetadata(client).ok());
  PushServerTrailingMetadata(*backend,
                             ServerMetadataFromStatus(GRPC_STATUS_OK));
  EXPECT_EQ(PullServerTrailingStatus(client), absl::OkStatus());
  EXPECT_THAT(stats_plugin->GetHistogramValueByName(
                  "grpc.client_ext_proc.server_headers_duration", {kServerUri}),
              Optional(ElementsAre(0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
                                   0, 0, 0, 0, 0)));
  WaitForAllPendingWork();
}

FILTER_TEST(ExtProcFilterTest, ExtProcServerTrailersDurationMetric) {
  auto stats_plugin = InitWithStatsPlugin(ConfigBuilder()
                                              .SetRequestHeaderMode(false)
                                              .SetResponseHeaderMode(false)
                                              .SetResponseTrailerMode(true));
  CallInitiator client = StartCall(NewClientMetadata({}, kPath));
  std::optional<CallHandler> backend = GetNextHandler();
  ASSERT_TRUE(backend.has_value());
  auto side_stream = transport_factory_->WaitForStream(target_, kExtProcMethod);
  ASSERT_NE(side_stream, nullptr);
  PushServerInitialMetadata(*backend, NewServerMetadata());
  EXPECT_TRUE(PullServerInitialMetadata(client).ok());
  PushServerTrailingMetadata(*backend,
                             ServerMetadataFromStatus(GRPC_STATUS_OK));
  EXPECT_THAT(GetNextSideStreamRequest(*side_stream),
              Optional(MatchesResponseTrailers(_)));
  // The filter waits for the ext_proc server's response.
  event_engine()->TickForDuration(std::chrono::seconds(2));
  side_stream->SendMessageToClient(
      MakeResponseTrailersMutationResponse({}).SerializeAsString());
  EXPECT_EQ(PullServerTrailingStatus(client), absl::OkStatus());
  EXPECT_THAT(
      stats_plugin->GetHistogramValueByName(
          "grpc.client_ext_proc.server_trailers_duration", {kServerUri}),
      Optional(ElementsAre(0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
                           0, 0)));
  WaitForAllPendingWork();
}

//
// Server-side tests
//
// The filter behaves the same on the client and server, except that on the
// server it records metrics in the server telemetry domain, computes the
// connection request attributes, and does not default request.host to the
// channel's authority.
//

FILTER_TEST(ExtProcFilterTest, ServerSideDurationMetrics) {
  auto stats_plugin = InitWithStatsPlugin(
      ConfigBuilder()
          .SetRequestHeaderMode(true)
          .SetRequestBodyMode(true)
          .SetResponseHeaderMode(true)
          .SetResponseTrailerMode(true),
      ChannelArgs().Set(GRPC_ARG_IS_SERVER_FILTER_STACK, true));
  CallInitiator client = StartCall(NewClientMetadata({}, kPath));
  auto side_stream = transport_factory_->WaitForStream(target_, kExtProcMethod);
  ASSERT_NE(side_stream, nullptr);
  // Each event is held for 2 seconds while the filter waits for the ext_proc
  // server.
  EXPECT_THAT(GetNextSideStreamRequest(*side_stream),
              Optional(MatchesRequestHeaders(_)));
  event_engine()->TickForDuration(std::chrono::seconds(2));
  side_stream->SendMessageToClient(
      MakeRequestHeadersMutationResponse({}).SerializeAsString());
  std::optional<CallHandler> backend = GetNextHandler();
  ASSERT_TRUE(backend.has_value());
  EXPECT_TRUE(PullClientInitialMetadata(*backend).ok());
  PushClientHalfClose(client);
  EXPECT_THAT(GetNextSideStreamRequest(*side_stream),
              Optional(MatchesRequestBody(kEmptyBody, kEndOfStream)));
  event_engine()->TickForDuration(std::chrono::seconds(2));
  side_stream->SendMessageToClient(
      MakeRequestHalfCloseResponse().SerializeAsString());
  EXPECT_THAT(PullClientMessage(*backend), IsEndOfStream());
  PushServerInitialMetadata(*backend, NewServerMetadata());
  EXPECT_THAT(GetNextSideStreamRequest(*side_stream),
              Optional(MatchesResponseHeaders(_)));
  event_engine()->TickForDuration(std::chrono::seconds(2));
  side_stream->SendMessageToClient(
      MakeResponseHeadersMutationResponse({}).SerializeAsString());
  EXPECT_TRUE(PullServerInitialMetadata(client).ok());
  PushServerTrailingMetadata(*backend,
                             ServerMetadataFromStatus(GRPC_STATUS_OK));
  EXPECT_THAT(GetNextSideStreamRequest(*side_stream),
              Optional(MatchesResponseTrailers(_)));
  event_engine()->TickForDuration(std::chrono::seconds(2));
  side_stream->SendMessageToClient(
      MakeResponseTrailersMutationResponse({}).SerializeAsString());
  EXPECT_EQ(PullServerTrailingStatus(client), absl::OkStatus());
  // The server metrics have no labels. Each has one 2-second sample.
  for (absl::string_view metric :
       {"client_headers_duration", "client_half_close_duration",
        "server_headers_duration", "server_trailers_duration"}) {
    EXPECT_THAT(stats_plugin->GetHistogramValueByName(
                    absl::StrCat("grpc.server_ext_proc.", metric)),
                Optional(ElementsAre(0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0,
                                     0, 0, 0, 0, 0, 0)))
        << metric;
    EXPECT_EQ(stats_plugin->GetHistogramValueByName(
                  absl::StrCat("grpc.client_ext_proc.", metric), {kServerUri}),
              std::nullopt)
        << metric;
  }
  WaitForAllPendingWork();
}

FILTER_TEST(ExtProcFilterTest, ServerSideRequestAttributes) {
  Init(ConfigBuilder()
           .SetRequestHeaderMode(true)
           .SetResponseHeaderMode(false)
           .AddRequestAttribute("request.host")
           .AddRequestAttribute("source.address")
           .AddRequestAttribute("source.port"),
       ChannelArgs()
           .Set(GRPC_ARG_IS_SERVER_FILTER_STACK, true)
           .Set(GRPC_ARG_ENDPOINT_PEER_ADDRESS, "ipv4:192.168.1.100:54321"));
  // The client metadata has no :authority.
  CallInitiator client = StartCall(NewClientMetadata({}, kPath));
  auto side_stream = transport_factory_->WaitForStream(target_, kExtProcMethod);
  ASSERT_NE(side_stream, nullptr);
  std::optional<ProcessingRequest> request =
      GetNextSideStreamRequest(*side_stream);
  ASSERT_THAT(request, Optional(MatchesRequestHeaders(_)));
  auto it = request->attributes().find("envoy.filters.http.ext_proc");
  ASSERT_NE(it, request->attributes().end());
  const auto& attributes = it->second.fields();
  // The source attributes come from the connection.
  EXPECT_EQ(GetExtProcAttribute(*request, "source.address"), "192.168.1.100");
  ASSERT_EQ(attributes.count("source.port"), 1);
  EXPECT_DOUBLE_EQ(attributes.at("source.port").number_value(), 54321);
  // On the server, request.host is not defaulted to the channel's authority.
  EXPECT_EQ(attributes.count("request.host"), 0);
  side_stream->SendMessageToClient(
      MakeRequestHeadersMutationResponse({}).SerializeAsString());
  std::optional<CallHandler> backend = GetNextHandler();
  ASSERT_TRUE(backend.has_value());
  EXPECT_TRUE(PullClientInitialMetadata(*backend).ok());
  PushClientHalfClose(client);
  EXPECT_THAT(PullClientMessage(*backend), IsEndOfStream());
  PushServerInitialMetadata(*backend, NewServerMetadata());
  EXPECT_TRUE(PullServerInitialMetadata(client).ok());
  PushServerTrailingMetadata(*backend,
                             ServerMetadataFromStatus(GRPC_STATUS_OK));
  EXPECT_EQ(PullServerTrailingStatus(client), absl::OkStatus());
  WaitForAllPendingWork();
}

// The client-side counterpart of ServerSideRequestAttributes.
FILTER_TEST(ExtProcFilterTest, ClientSideRequestAttributes) {
  Init(ConfigBuilder()
           .SetRequestHeaderMode(true)
           .SetResponseHeaderMode(false)
           .AddRequestAttribute("request.host")
           .AddRequestAttribute("source.address")
           .AddRequestAttribute("source.port"),
       ChannelArgs().Set(GRPC_ARG_ENDPOINT_PEER_ADDRESS,
                         "ipv4:192.168.1.100:54321"));
  // The client metadata has no :authority.
  CallInitiator client = StartCall(NewClientMetadata({}, kPath));
  auto side_stream = transport_factory_->WaitForStream(target_, kExtProcMethod);
  ASSERT_NE(side_stream, nullptr);
  std::optional<ProcessingRequest> request =
      GetNextSideStreamRequest(*side_stream);
  ASSERT_THAT(request, Optional(MatchesRequestHeaders(_)));
  auto it = request->attributes().find("envoy.filters.http.ext_proc");
  ASSERT_NE(it, request->attributes().end());
  const auto& attributes = it->second.fields();
  // On the client, request.host defaults to the channel's authority.
  EXPECT_EQ(GetExtProcAttribute(*request, "request.host"),
            "server.example.com");
  // The connection attributes are computed only on the server.
  EXPECT_EQ(attributes.count("source.address"), 0);
  EXPECT_EQ(attributes.count("source.port"), 0);
  side_stream->SendMessageToClient(
      MakeRequestHeadersMutationResponse({}).SerializeAsString());
  std::optional<CallHandler> backend = GetNextHandler();
  ASSERT_TRUE(backend.has_value());
  EXPECT_TRUE(PullClientInitialMetadata(*backend).ok());
  PushClientHalfClose(client);
  EXPECT_THAT(PullClientMessage(*backend), IsEndOfStream());
  PushServerInitialMetadata(*backend, NewServerMetadata());
  EXPECT_TRUE(PullServerInitialMetadata(client).ok());
  PushServerTrailingMetadata(*backend,
                             ServerMetadataFromStatus(GRPC_STATUS_OK));
  EXPECT_EQ(PullServerTrailingStatus(client), absl::OkStatus());
  WaitForAllPendingWork();
}

}  // namespace
}  // namespace grpc_core
