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

// Behaviour tests for the ext_authz filter.
//
// ExtAuthzFilter runs in a v3 interception chain built by FilterTest. The test
// plays all three remote parties:
// - the client, via the call started by StartCall() (PullServer*);
// - the backend, via the child call collected by GetNextHandler()
//   (PullClient*/PushServer*);
// - the authorization service, via a FakeXdsTransportFactory standing in for
//   the side-channel transport.
//
// The filter starts its Check call only when the backend pulls the client
// initial metadata, and that pull resolves only once the authorization service
// has responded. So tests start the pull with StartPullClientInitialMetadata(),
// play the authorization service, and then get the pull's result with
// FinishPullClientInitialMetadata(). All of this happens on the test thread,
// so that the FuzzingEventEngine alone decides how events interleave.
//
// xDS config parsing is covered by test/core/xds/xds_http_filters_test.cc,
// CheckRequest/CheckResponse conversion by ext_authz_message_test.cc, and
// end-to-end wiring by test/cpp/end2end/xds/xds_ext_authz_end2end_test.cc.

#include "src/core/filter/ext_authz/ext_authz_filter.h"

#include <grpc/grpc_security_constants.h>
#include <grpc/status.h>
#include <grpc/support/metrics.h>

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>

#include "envoy/config/core/v3/base.pb.h"
#include "envoy/service/auth/v3/attribute_context.pb.h"
#include "envoy/service/auth/v3/external_auth.pb.h"
#include "envoy/type/v3/http_status.pb.h"
#include "src/core/call/call_spine.h"
#include "src/core/call/metadata.h"
#include "src/core/call/metadata_batch.h"
#include "src/core/client_channel/client_channel_args.h"
#include "src/core/filter/filter_args.h"
#include "src/core/handshaker/endpoint_info/endpoint_info_handshaker.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/event_engine/channel_args_endpoint_config.h"
#include "src/core/lib/promise/map.h"
#include "src/core/lib/promise/poll.h"
#include "src/core/lib/promise/status_flag.h"
#include "src/core/telemetry/metrics.h"
#include "src/core/transport/auth_context.h"
#include "src/core/util/matchers.h"
#include "src/core/util/ref_counted_ptr.h"
#include "src/core/xds/grpc/xds_common_types.h"
#include "src/core/xds/grpc/xds_server_grpc.h"
#include "test/core/filters/filter_matchers.h"
#include "test/core/filters/filter_test.h"
#include "test/core/test_util/fake_stats_plugin.h"
#include "test/core/xds/xds_transport_fake.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/status/status.h"
#include "absl/strings/string_view.h"

namespace grpc_core {
namespace {

using ::envoy::config::core::v3::HeaderValue;
using ::envoy::config::core::v3::HeaderValueOption;
using ::envoy::service::auth::v3::CheckRequest;
using ::envoy::service::auth::v3::CheckResponse;
using ::testing::AllOf;
using ::testing::Contains;
using ::testing::ElementsAre;
using ::testing::IsEmpty;
using ::testing::Not;
using ::testing::Optional;
using ::testing::Pointee;
using ::testing::Property;
using ::testing::StrEq;

//
// Constants
//

// The authorization service method the filter calls on its side channel.
constexpr char kExtAuthzMethod[] = "/envoy.service.auth.v3.Authorization/Check";
// Target of the data plane channel; also the "grpc.target" label on the
// client-side metrics.
constexpr char kServerUri[] = "dns:///server.example.com";
constexpr char kPath[] = "/package.Service/Method";
// Status on error for the configs that set one. It differs from both the
// default status_on_error and the status of a denied response, so that the
// expected status of a failed RPC shows where it came from.
constexpr grpc_status_code kStatusOnError = GRPC_STATUS_UNAVAILABLE;

// Headers.
constexpr char kFailureModeAllowedHeaderKey[] =
    "x-envoy-auth-failure-mode-allowed";
constexpr char kFailureModeAllowedHeaderValue[] = "true";
constexpr char kCustomHeaderKey[] = "x-custom-header";
constexpr char kCustomHeaderValue[] = "custom-value";
constexpr char kCustomHeaderKey2[] = "x-custom-header-2";
constexpr char kCustomHeaderValue2[] = "custom-value-2";
constexpr char kHostHeaderKey[] = "host";

// A CheckResponse that does not parse: field 1 (status) with a length of 255
// but no payload.
constexpr char kMalformedCheckResponse[] = "\x0a\xff";

// Status messages.
constexpr char kCheckResponseStatusMessage[] = "token expired";
constexpr char kDeniedErrorMessage[] =
    "ExtAuthz request is denied, error message: token expired";
constexpr char kDeniedNoMessageErrorMessage[] = "ExtAuthz request is denied";
constexpr char kDisabledErrorMessage[] = "ExtAuthz filter is not enabled";
constexpr char kChannelUnavailableErrorMessage[] =
    "ext_authz channel or transport not available";
constexpr char kFilterConfigMissingErrorMessage[] =
    "ext_authz filter config is missing";
constexpr char kNoResponseErrorMessage[] = "No response message received";
constexpr char kParseErrorMessage[] = "Failed to parse CheckResponse";
constexpr char kForbiddenHeaderMutationErrorMessage[] =
    "Forbidden header mutation: x-custom-header";
constexpr char kForbiddenHeaderRemovalErrorMessage[] =
    "Forbidden header removal: x-custom-header";
constexpr char kTransportErrorMessage[] = "transport failure";

// Connection attributes used by the server-side tests.
constexpr char kUriSan[] = "spiffe://foo.com/bar/baz";
constexpr char kLocalPrincipal[] = "spiffe://foo.com/server";
constexpr char kPeerAddressUri[] = "ipv4:1.2.3.4:1234";
constexpr char kPeerAddressHost[] = "1.2.3.4";
constexpr int kPeerAddressPort = 1234;
constexpr char kLocalAddressUri[] = "ipv4:5.6.7.8:5678";
constexpr char kLocalAddressHost[] = "5.6.7.8";
constexpr int kLocalAddressPort = 5678;
constexpr char kPemCert[] =
    "-----BEGIN CERTIFICATE-----\n"
    "abc=\n"
    "-----END CERTIFICATE-----\n";
constexpr char kEncodedCert[] =
    "-----BEGIN%20CERTIFICATE-----%0Aabc%3D%0A-----END%20CERTIFICATE-----%0A";

MATCHER_P(HasClientMetrics, expected, "") {
  return ::testing::ExplainMatchResult(::testing::NotNull(), arg,
                                       result_listener) &&
         ::testing::ExplainMatchResult(
             Optional(::testing::Eq(expected.allowed)),
             arg->GetUInt64MetricValueByName(
                 "grpc.client_ext_authz.allowed_rpcs", {kServerUri}),
             result_listener) &&
         ::testing::ExplainMatchResult(
             Optional(::testing::Eq(expected.denied)),
             arg->GetUInt64MetricValueByName(
                 "grpc.client_ext_authz.denied_rpcs", {kServerUri}),
             result_listener) &&
         ::testing::ExplainMatchResult(
             Optional(::testing::Eq(expected.failed)),
             arg->GetUInt64MetricValueByName(
                 "grpc.client_ext_authz.failed_rpcs", {kServerUri}),
             result_listener) &&
         ::testing::ExplainMatchResult(
             Optional(::testing::Eq(expected.filter_disabled)),
             arg->GetUInt64MetricValueByName(
                 "grpc.client_ext_authz.filter_disabled_rpcs", {kServerUri}),
             result_listener);
}

MATCHER_P(HasServerMetrics, expected, "") {
  return ::testing::ExplainMatchResult(::testing::NotNull(), arg,
                                       result_listener) &&
         ::testing::ExplainMatchResult(
             Optional(::testing::Eq(expected.allowed)),
             arg->GetUInt64MetricValueByName(
                 "grpc.server_ext_authz.allowed_rpcs"),
             result_listener) &&
         ::testing::ExplainMatchResult(Optional(::testing::Eq(expected.denied)),
                                       arg->GetUInt64MetricValueByName(
                                           "grpc.server_ext_authz.denied_rpcs"),
                                       result_listener) &&
         ::testing::ExplainMatchResult(Optional(::testing::Eq(expected.failed)),
                                       arg->GetUInt64MetricValueByName(
                                           "grpc.server_ext_authz.failed_rpcs"),
                                       result_listener) &&
         ::testing::ExplainMatchResult(
             Optional(::testing::Eq(expected.filter_disabled)),
             arg->GetUInt64MetricValueByName(
                 "grpc.server_ext_authz.filter_disabled_rpcs"),
             result_listener);
}

//
// Test fixture
//

class ExtAuthzFilterTest : public FilterTest {
 protected:
  using FilterTest::FilterTest;

  struct ExtAuthzMetrics {
    uint64_t allowed = 0;
    uint64_t denied = 0;
    uint64_t failed = 0;
    uint64_t filter_disabled = 0;
  };

  // Builds an ExtAuthzFilter::Config directly, bypassing xDS parsing. Unset
  // fields keep the Config defaults. Unless WithoutChannel() is called, the
  // config gets a channel to the fake authorization service.
  class ConfigBuilder {
   public:
    ConfigBuilder() : config_(MakeRefCounted<ExtAuthzFilter::Config>()) {}

    ConfigBuilder& SetFilterEnabled(uint32_t parts_per_million) {
      config_->filter_enabled = parts_per_million;
      return *this;
    }
    ConfigBuilder& SetDenyAtDisable(bool deny) {
      config_->deny_at_disable = deny;
      return *this;
    }
    ConfigBuilder& SetFailureModeAllow(bool allow) {
      config_->failure_mode_allow = allow;
      return *this;
    }
    ConfigBuilder& SetFailureModeAllowHeaderAdd(bool header_add) {
      config_->failure_mode_allow_header_add = header_add;
      return *this;
    }
    ConfigBuilder& SetStatusOnError(grpc_status_code status) {
      config_->status_on_error = status;
      return *this;
    }
    ConfigBuilder& AddAllowedHeader(absl::string_view name) {
      config_->allowed_headers.push_back(
          StringMatcher::Create(StringMatcher::Type::kExact, name).value());
      return *this;
    }
    ConfigBuilder& AddDisallowedHeader(absl::string_view name) {
      config_->disallowed_headers.push_back(
          StringMatcher::Create(StringMatcher::Type::kExact, name).value());
      return *this;
    }
    ConfigBuilder& SetHeaderMutationRules(HeaderMutationRules rules) {
      config_->decoder_header_mutation_rules = std::move(rules);
      return *this;
    }
    ConfigBuilder& SetIncludePeerCertificate(bool include) {
      config_->include_peer_certificate = include;
      return *this;
    }
    ConfigBuilder& WithoutChannel() {
      with_channel_ = false;
      return *this;
    }

    bool with_channel() const { return with_channel_; }

    RefCountedPtr<ExtAuthzFilter::Config> Build(
        RefCountedPtr<ExtAuthzFilter::ExtAuthzChannel> channel) {
      config_->channel_info = std::move(channel);
      return std::move(config_);
    }

   private:
    RefCountedPtr<ExtAuthzFilter::Config> config_;
    bool with_channel_ = true;
  };

  // A pull of the client initial metadata started by
  // StartPullClientInitialMetadata(). Holds the pull's result once it
  // resolves.
  using PendingClientInitialMetadata =
      std::shared_ptr<std::optional<ValueOrFailure<ClientMetadataHandle>>>;

  // Builds the chain [ExtAuthzFilter] -> backend, with the filter's Check
  // calls going to the fake transport.
  void Init(ConfigBuilder builder, const ChannelArgs& args = ChannelArgs()) {
    transport_factory_ = MakeRefCounted<FakeXdsTransportFactory>(
        [] {
          ADD_FAILURE() << "too many pending reads on ext_authz Check call";
        },
        event_engine());
    RefCountedPtr<ExtAuthzFilter::ExtAuthzChannel> channel;
    if (builder.with_channel()) {
      absl::Status status;
      auto transport = transport_factory_->GetTransport(target_, &status);
      ASSERT_TRUE(status.ok()) << status;
      channel = MakeRefCounted<ExtAuthzFilter::ExtAuthzChannel>(
          target_, std::move(transport));
    }
    absl::Status chain_status = CreateFilterChain<ExtAuthzFilter>(
        args.Set(GRPC_ARG_SERVER_URI, kServerUri),
        builder.Build(std::move(channel)));
    ASSERT_TRUE(chain_status.ok()) << chain_status;
  }

  // As Init(), but also registers a stats plugin for the data plane channel
  // (or server, if `args` sets GRPC_ARG_IS_SERVER_FILTER_STACK) and returns it.
  std::shared_ptr<FakeStatsPlugin> InitWithStatsPlugin(
      ConfigBuilder builder, const ChannelArgs& args = ChannelArgs()) {
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

  // Builds an auth context with the peer and local X.509 properties that
  // AttributeContext.source.principal and .destination.principal are derived
  // from.
  RefCountedPtr<grpc_auth_context> MakeAuthContext() {
    auto auth_context = MakeRefCounted<grpc_auth_context>(nullptr);
    auth_context->add_cstring_property(GRPC_PEER_URI_PROPERTY_NAME, kUriSan);
    auth_context->add_cstring_property(GRPC_X509_LOCAL_PRINCIPAL_PROPERTY_NAME,
                                       kLocalPrincipal);
    return auth_context;
  }

  // Channel args as seen by a filter instantiated on a server filter chain.
  // A null auth_context models a connection that has none.
  ChannelArgs ServerArgs(RefCountedPtr<grpc_auth_context> auth_context) {
    ChannelArgs args =
        ChannelArgs()
            .Set(GRPC_ARG_IS_SERVER_FILTER_STACK, true)
            .Set(GRPC_ARG_ENDPOINT_PEER_ADDRESS, kPeerAddressUri)
            .Set(GRPC_ARG_ENDPOINT_LOCAL_ADDRESS, kLocalAddressUri);
    if (auth_context != nullptr) {
      args = args.SetObject(std::move(auth_context));
    }
    return args;
  }

  //
  // Backend
  //

  // Starts the backend's pull of the client initial metadata, which runs the
  // filter's OnClientInitialMetadata(), without waiting for it to resolve:
  // the filter may first need a response from the authorization service.
  PendingClientInitialMetadata StartPullClientInitialMetadata(
      CallHandler backend) {
    auto result =
        std::make_shared<std::optional<ValueOrFailure<ClientMetadataHandle>>>();
    backend.SpawnInfallible(
        "pull-client-initial-metadata",
        [backend = std::optional<CallHandler>(backend), result]() mutable {
          auto pull = backend->PullClientInitialMetadata();
          // Drop the ref to the call: holding it until the pull resolves
          // would keep the call alive, and so stop the pull from being
          // cancelled, for as long as the filter is waiting.
          backend.reset();
          return Map(std::move(pull),
                     [result](ValueOrFailure<ClientMetadataHandle> md) {
                       *result = std::move(md);
                       return Empty{};
                     });
        });
    return result;
  }

  // Ticks the event engine until `pull` resolves, and returns its result: a
  // failure if the filter failed the call.
  ValueOrFailure<ClientMetadataHandle> FinishPullClientInitialMetadata(
      const PendingClientInitialMetadata& pull) {
    auto poll = [pull]() -> Poll<ValueOrFailure<ClientMetadataHandle>> {
      if (!pull->has_value()) return Pending{};
      return std::move(**pull);
    };
    return TickUntil(
        absl::FunctionRef<Poll<ValueOrFailure<ClientMetadataHandle>>()>(poll));
  }

  //
  // CheckResponse builders
  //

  static void AddHeader(HeaderValueOption* option, absl::string_view key,
                        absl::string_view value) {
    option->mutable_header()->set_key(std::string(key));
    option->mutable_header()->set_value(std::string(value));
  }

  // A CheckResponse that allows the RPC.
  static CheckResponse MakeOkCheckResponse() {
    CheckResponse response;
    response.mutable_status()->set_code(GRPC_STATUS_OK);
    response.mutable_ok_response();
    return response;
  }

  // A CheckResponse that denies the RPC with HTTP status 401, which maps to
  // UNAUTHENTICATED.
  static CheckResponse MakeDeniedCheckResponse() {
    CheckResponse response;
    response.mutable_status()->set_code(GRPC_STATUS_PERMISSION_DENIED);
    response.mutable_status()->set_message(kCheckResponseStatusMessage);
    response.mutable_denied_response()->mutable_status()->set_code(
        envoy::type::v3::StatusCode::Unauthorized);
    return response;
  }

  // Header mutation rules that disallow every mutation.
  static HeaderMutationRules DisallowAllRules(bool disallow_is_error) {
    HeaderMutationRules rules;
    rules.disallow_all = true;
    rules.disallow_is_error = disallow_is_error;
    return rules;
  }

  //
  // Check call (fake authorization service)
  //

  // Waits for the Check call opened on the fake authorization service, or
  // returns null if the filter doesn't open one.
  RefCountedPtr<FakeXdsTransportFactory::FakeStreamingCall> WaitForCheckCall() {
    return transport_factory_->WaitForStream(target_, kExtAuthzMethod);
  }

  // Returns the CheckRequest the filter sent on `check_call`, or nullopt if
  // it doesn't send one.
  std::optional<CheckRequest> GetCheckRequest(
      FakeXdsTransportFactory::FakeStreamingCall& check_call) {
    std::optional<std::string> payload = check_call.WaitForMessageFromClient();
    if (!payload.has_value()) return std::nullopt;
    CheckRequest request;
    if (!request.ParseFromString(*payload)) {
      ADD_FAILURE() << "failed to parse CheckRequest from Check call";
      return std::nullopt;
    }
    return request;
  }

  // Runs one RPC that the authorization service allows, and returns the
  // CheckRequest the filter sent for it, or nullopt if it sends none. For the
  // tests of the CheckRequest contents.
  std::optional<CheckRequest> CaptureCheckRequest(
      ClientMetadataHandle client_initial_metadata) {
    CallInitiator client = StartCall(std::move(client_initial_metadata));
    std::optional<CallHandler> backend = GetNextHandler();
    if (!backend.has_value()) {
      ADD_FAILURE() << "no call started on the backend";
      return std::nullopt;
    }
    auto pull = StartPullClientInitialMetadata(*backend);
    auto check_call = WaitForCheckCall();
    if (check_call == nullptr) {
      ADD_FAILURE() << "no Check call started";
      return std::nullopt;
    }
    std::optional<CheckRequest> request = GetCheckRequest(*check_call);
    check_call->SendMessageToClient(MakeOkCheckResponse().SerializeAsString());
    check_call->MaybeSendStatusToClient(absl::OkStatus());
    EXPECT_TRUE(FinishPullClientInitialMetadata(pull).ok());
    PushServerTrailingMetadata(*backend,
                               ServerMetadataFromStatus(GRPC_STATUS_OK));
    EXPECT_EQ(PullServerTrailingStatus(client), absl::OkStatus());
    WaitForAllPendingWork();
    return request;
  }

 private:
  // Drops everything holding a ref to event_engine() before YodelTest waits
  // for it to have a single owner. Runs after FilterTest has torn down the
  // stack under test.
  void Cleanup() override {
    if (transport_factory_ != nullptr) {
      // A test that stops early may leave the filter waiting on its Check
      // call. Finish that call, so that the filter drops it: it holds
      // references to the fake transport and, through it, to event_engine().
      // WaitForStream() does not wait for a new call: it returns the Check
      // call, if the filter still holds it, or null if it does not.
      auto check_call = WaitForCheckCall();
      if (check_call != nullptr) {
        check_call->MaybeSendStatusToClient(absl::CancelledError());
      }
    }
    transport_factory_.reset();
    if (stats_plugin_group_ != nullptr) {
      stats_plugin_group_.reset();
      GlobalStatsPluginRegistryTestPeer::ResetGlobalStatsPluginRegistry();
    }
  }

  // The Check calls (fake authorization service) are opened on this
  // transport factory.
  const GrpcXdsServerTarget target_{"dns:///ext_authz.example.com",
                                    /*channel_creds_config=*/nullptr,
                                    /*call_creds_configs=*/{}};
  RefCountedPtr<FakeXdsTransportFactory> transport_factory_;
  // Set by InitWithStatsPlugin(). Held for the whole test, as a channel holds
  // it via its args: the filter's metrics are only visible to the stats plugin
  // while the group's collection scope is alive.
  std::shared_ptr<GlobalStatsPluginRegistry::StatsPluginGroup>
      stats_plugin_group_;
};

//
// Filter creation tests
//

FILTER_TEST(ExtAuthzFilterTest, CreateFailsWithoutConfig) {
  EXPECT_EQ(CreateFilterChain<ExtAuthzFilter>(ChannelArgs(), nullptr),
            absl::InvalidArgumentError(kFilterConfigMissingErrorMessage));
}

//
// Filter enablement tests (filter_enabled / deny_at_disable)
//

// When the filter is disabled, the RPC goes through without a Check call and
// counts as allowed and filter-disabled.
FILTER_TEST(ExtAuthzFilterTest, FilterDisabledAllows) {
  auto stats_plugin = InitWithStatsPlugin(
      ConfigBuilder().SetFilterEnabled(/*parts_per_million=*/0));
  CallInitiator client = StartCall(NewClientMetadata({}, kPath));
  std::optional<CallHandler> backend = GetNextHandler();
  ASSERT_TRUE(backend.has_value());
  auto pull = StartPullClientInitialMetadata(*backend);
  EXPECT_EQ(WaitForCheckCall(), nullptr);
  EXPECT_TRUE(FinishPullClientInitialMetadata(pull).ok());
  PushServerTrailingMetadata(*backend,
                             ServerMetadataFromStatus(GRPC_STATUS_OK));
  EXPECT_EQ(PullServerTrailingStatus(client), absl::OkStatus());
  EXPECT_THAT(stats_plugin,
              HasClientMetrics(ExtAuthzMetrics{/*allowed=*/1, /*denied=*/0,
                                               /*failed=*/0,
                                               /*filter_disabled=*/1}));
  WaitForAllPendingWork();
}

FILTER_TEST(ExtAuthzFilterTest, FilterDisabledWithDenyAtDisableFails) {
  auto stats_plugin =
      InitWithStatsPlugin(ConfigBuilder()
                              .SetFilterEnabled(/*parts_per_million=*/0)
                              .SetDenyAtDisable(true)
                              .SetStatusOnError(kStatusOnError));
  CallInitiator client = StartCall(NewClientMetadata({}, kPath));
  std::optional<CallHandler> backend = GetNextHandler();
  ASSERT_TRUE(backend.has_value());
  EXPECT_FALSE(PullClientInitialMetadata(*backend).ok());
  EXPECT_EQ(PullServerTrailingStatus(client),
            absl::UnavailableError(kDisabledErrorMessage));
  EXPECT_THAT(stats_plugin,
              HasClientMetrics(ExtAuthzMetrics{/*allowed=*/0, /*denied=*/1,
                                               /*failed=*/0,
                                               /*filter_disabled=*/1}));
  WaitForAllPendingWork();
}

// failure_mode_allow only covers failures to get a decision, so it does not
// stop deny_at_disable from failing the RPC.
FILTER_TEST(ExtAuthzFilterTest,
            FilterDisabledWithDenyAtDisableIgnoresFailureModeAllow) {
  Init(ConfigBuilder()
           .SetFilterEnabled(/*parts_per_million=*/0)
           .SetDenyAtDisable(true)
           .SetFailureModeAllow(true)
           .SetFailureModeAllowHeaderAdd(true)
           .SetStatusOnError(kStatusOnError));
  CallInitiator client = StartCall(NewClientMetadata({}, kPath));
  std::optional<CallHandler> backend = GetNextHandler();
  ASSERT_TRUE(backend.has_value());
  EXPECT_FALSE(PullClientInitialMetadata(*backend).ok());
  EXPECT_EQ(PullServerTrailingStatus(client),
            absl::UnavailableError(kDisabledErrorMessage));
  WaitForAllPendingWork();
}

//
// Check call failure tests (failure_mode_allow)
//

FILTER_TEST(ExtAuthzFilterTest, MissingChannelWithFailureModeDeny) {
  Init(ConfigBuilder().WithoutChannel().SetStatusOnError(kStatusOnError));
  CallInitiator client = StartCall(NewClientMetadata({}, kPath));
  std::optional<CallHandler> backend = GetNextHandler();
  ASSERT_TRUE(backend.has_value());
  EXPECT_FALSE(PullClientInitialMetadata(*backend).ok());
  EXPECT_EQ(PullServerTrailingStatus(client),
            absl::UnavailableError(kChannelUnavailableErrorMessage));
  WaitForAllPendingWork();
}

FILTER_TEST(ExtAuthzFilterTest, MissingChannelWithFailureModeAllow) {
  Init(ConfigBuilder()
           .WithoutChannel()
           .SetFailureModeAllow(true)
           .SetFailureModeAllowHeaderAdd(true)
           .SetStatusOnError(kStatusOnError));
  CallInitiator client = StartCall(NewClientMetadata({}, kPath));
  std::optional<CallHandler> backend = GetNextHandler();
  ASSERT_TRUE(backend.has_value());
  EXPECT_THAT(
      PullClientInitialMetadata(*backend),
      IsOkAndHolds(Pointee(HasMetadataKeyValue(
          kFailureModeAllowedHeaderKey, kFailureModeAllowedHeaderValue))));
  PushServerTrailingMetadata(*backend,
                             ServerMetadataFromStatus(GRPC_STATUS_OK));
  EXPECT_EQ(PullServerTrailingStatus(client), absl::OkStatus());
  WaitForAllPendingWork();
}

// The status of a failed Check call is not propagated: the RPC fails with
// status_on_error and counts as both failed and denied.
FILTER_TEST(ExtAuthzFilterTest, TransportFailureWithFailureModeDeny) {
  auto stats_plugin =
      InitWithStatsPlugin(ConfigBuilder().SetStatusOnError(kStatusOnError));
  CallInitiator client = StartCall(NewClientMetadata({}, kPath));
  std::optional<CallHandler> backend = GetNextHandler();
  ASSERT_TRUE(backend.has_value());
  auto pull = StartPullClientInitialMetadata(*backend);
  auto check_call = WaitForCheckCall();
  ASSERT_NE(check_call, nullptr);
  EXPECT_TRUE(GetCheckRequest(*check_call).has_value());
  check_call->MaybeSendStatusToClient(
      absl::InternalError(kTransportErrorMessage));
  EXPECT_FALSE(FinishPullClientInitialMetadata(pull).ok());
  EXPECT_EQ(PullServerTrailingStatus(client),
            absl::UnavailableError(kTransportErrorMessage));
  EXPECT_THAT(stats_plugin,
              HasClientMetrics(ExtAuthzMetrics{/*allowed=*/0, /*denied=*/1,
                                               /*failed=*/1,
                                               /*filter_disabled=*/0}));
  WaitForAllPendingWork();
}

// failure_mode_allow lets the RPC through (counting as both failed and
// allowed) but, without failure_mode_allow_header_add, does not tell the
// backend.
FILTER_TEST(ExtAuthzFilterTest, TransportFailureWithFailureModeAllow) {
  auto stats_plugin =
      InitWithStatsPlugin(ConfigBuilder().SetFailureModeAllow(true));
  CallInitiator client = StartCall(NewClientMetadata({}, kPath));
  std::optional<CallHandler> backend = GetNextHandler();
  ASSERT_TRUE(backend.has_value());
  auto pull = StartPullClientInitialMetadata(*backend);
  auto check_call = WaitForCheckCall();
  ASSERT_NE(check_call, nullptr);
  EXPECT_TRUE(GetCheckRequest(*check_call).has_value());
  check_call->MaybeSendStatusToClient(
      absl::InternalError(kTransportErrorMessage));
  EXPECT_THAT(
      FinishPullClientInitialMetadata(pull),
      IsOkAndHolds(Pointee(LacksMetadataKey(kFailureModeAllowedHeaderKey))));
  PushServerTrailingMetadata(*backend,
                             ServerMetadataFromStatus(GRPC_STATUS_OK));
  EXPECT_EQ(PullServerTrailingStatus(client), absl::OkStatus());
  EXPECT_THAT(stats_plugin,
              HasClientMetrics(ExtAuthzMetrics{/*allowed=*/1, /*denied=*/0,
                                               /*failed=*/1,
                                               /*filter_disabled=*/0}));
  WaitForAllPendingWork();
}

FILTER_TEST(ExtAuthzFilterTest,
            TransportFailureWithFailureModeAllowInjectsHeader) {
  Init(ConfigBuilder().SetFailureModeAllow(true).SetFailureModeAllowHeaderAdd(
      true));
  CallInitiator client = StartCall(NewClientMetadata({}, kPath));
  std::optional<CallHandler> backend = GetNextHandler();
  ASSERT_TRUE(backend.has_value());
  auto pull = StartPullClientInitialMetadata(*backend);
  auto check_call = WaitForCheckCall();
  ASSERT_NE(check_call, nullptr);
  EXPECT_TRUE(GetCheckRequest(*check_call).has_value());
  check_call->MaybeSendStatusToClient(
      absl::InternalError(kTransportErrorMessage));
  EXPECT_THAT(
      FinishPullClientInitialMetadata(pull),
      IsOkAndHolds(Pointee(HasMetadataKeyValue(
          kFailureModeAllowedHeaderKey, kFailureModeAllowedHeaderValue))));
  PushServerTrailingMetadata(*backend,
                             ServerMetadataFromStatus(GRPC_STATUS_OK));
  EXPECT_EQ(PullServerTrailingStatus(client), absl::OkStatus());
  WaitForAllPendingWork();
}

// The authorization service ends the Check call with OK but never sends a
// CheckResponse.
FILTER_TEST(ExtAuthzFilterTest, NoResponseMessageWithFailureModeDeny) {
  Init(ConfigBuilder().SetStatusOnError(kStatusOnError));
  CallInitiator client = StartCall(NewClientMetadata({}, kPath));
  std::optional<CallHandler> backend = GetNextHandler();
  ASSERT_TRUE(backend.has_value());
  auto pull = StartPullClientInitialMetadata(*backend);
  auto check_call = WaitForCheckCall();
  ASSERT_NE(check_call, nullptr);
  EXPECT_TRUE(GetCheckRequest(*check_call).has_value());
  check_call->MaybeSendStatusToClient(absl::OkStatus());
  EXPECT_FALSE(FinishPullClientInitialMetadata(pull).ok());
  EXPECT_EQ(PullServerTrailingStatus(client),
            absl::UnavailableError(kNoResponseErrorMessage));
  WaitForAllPendingWork();
}

FILTER_TEST(ExtAuthzFilterTest, NoResponseMessageWithFailureModeAllow) {
  Init(ConfigBuilder().SetFailureModeAllow(true).SetFailureModeAllowHeaderAdd(
      true));
  CallInitiator client = StartCall(NewClientMetadata({}, kPath));
  std::optional<CallHandler> backend = GetNextHandler();
  ASSERT_TRUE(backend.has_value());
  auto pull = StartPullClientInitialMetadata(*backend);
  auto check_call = WaitForCheckCall();
  ASSERT_NE(check_call, nullptr);
  EXPECT_TRUE(GetCheckRequest(*check_call).has_value());
  check_call->MaybeSendStatusToClient(absl::OkStatus());
  EXPECT_THAT(
      FinishPullClientInitialMetadata(pull),
      IsOkAndHolds(Pointee(HasMetadataKeyValue(
          kFailureModeAllowedHeaderKey, kFailureModeAllowedHeaderValue))));
  PushServerTrailingMetadata(*backend,
                             ServerMetadataFromStatus(GRPC_STATUS_OK));
  EXPECT_EQ(PullServerTrailingStatus(client), absl::OkStatus());
  WaitForAllPendingWork();
}

// A valid CheckResponse does not count if the Check call then fails: the
// call's status is checked before the response is used.
FILTER_TEST(ExtAuthzFilterTest, OkResponseThenCallFailureWithFailureModeDeny) {
  Init(ConfigBuilder().SetStatusOnError(kStatusOnError));
  CallInitiator client = StartCall(NewClientMetadata({}, kPath));
  std::optional<CallHandler> backend = GetNextHandler();
  ASSERT_TRUE(backend.has_value());
  auto pull = StartPullClientInitialMetadata(*backend);
  auto check_call = WaitForCheckCall();
  ASSERT_NE(check_call, nullptr);
  EXPECT_TRUE(GetCheckRequest(*check_call).has_value());
  check_call->SendMessageToClient(MakeOkCheckResponse().SerializeAsString());
  check_call->MaybeSendStatusToClient(
      absl::InternalError(kTransportErrorMessage));
  EXPECT_FALSE(FinishPullClientInitialMetadata(pull).ok());
  EXPECT_EQ(PullServerTrailingStatus(client),
            absl::UnavailableError(kTransportErrorMessage));
  WaitForAllPendingWork();
}

// As above, with failure_mode_allow: the RPC goes through as for any other
// failure, without the header mutations of the discarded CheckResponse.
FILTER_TEST(ExtAuthzFilterTest, OkResponseThenCallFailureWithFailureModeAllow) {
  Init(ConfigBuilder().SetFailureModeAllow(true).SetFailureModeAllowHeaderAdd(
      true));
  CallInitiator client = StartCall(NewClientMetadata({}, kPath));
  std::optional<CallHandler> backend = GetNextHandler();
  ASSERT_TRUE(backend.has_value());
  auto pull = StartPullClientInitialMetadata(*backend);
  auto check_call = WaitForCheckCall();
  ASSERT_NE(check_call, nullptr);
  EXPECT_TRUE(GetCheckRequest(*check_call).has_value());
  CheckResponse response = MakeOkCheckResponse();
  AddHeader(response.mutable_ok_response()->add_headers(), kCustomHeaderKey,
            kCustomHeaderValue);
  check_call->SendMessageToClient(response.SerializeAsString());
  check_call->MaybeSendStatusToClient(
      absl::InternalError(kTransportErrorMessage));
  EXPECT_THAT(FinishPullClientInitialMetadata(pull),
              IsOkAndHolds(Pointee(
                  AllOf(HasMetadataKeyValue(kFailureModeAllowedHeaderKey,
                                            kFailureModeAllowedHeaderValue),
                        LacksMetadataKey(kCustomHeaderKey)))));
  PushServerTrailingMetadata(*backend,
                             ServerMetadataFromStatus(GRPC_STATUS_OK));
  EXPECT_EQ(PullServerTrailingStatus(client), absl::OkStatus());
  WaitForAllPendingWork();
}

FILTER_TEST(ExtAuthzFilterTest, MalformedResponseWithFailureModeDeny) {
  Init(ConfigBuilder().SetStatusOnError(kStatusOnError));
  CallInitiator client = StartCall(NewClientMetadata({}, kPath));
  std::optional<CallHandler> backend = GetNextHandler();
  ASSERT_TRUE(backend.has_value());
  auto pull = StartPullClientInitialMetadata(*backend);
  auto check_call = WaitForCheckCall();
  ASSERT_NE(check_call, nullptr);
  EXPECT_TRUE(GetCheckRequest(*check_call).has_value());
  check_call->SendMessageToClient(kMalformedCheckResponse);
  check_call->MaybeSendStatusToClient(absl::OkStatus());
  EXPECT_FALSE(FinishPullClientInitialMetadata(pull).ok());
  EXPECT_EQ(PullServerTrailingStatus(client),
            absl::UnavailableError(kParseErrorMessage));
  WaitForAllPendingWork();
}

// A CheckResponse that fails to parse is a failure, not a deny, so
// failure_mode_allow lets the call through.
FILTER_TEST(ExtAuthzFilterTest, MalformedResponseWithFailureModeAllow) {
  Init(ConfigBuilder().SetFailureModeAllow(true).SetFailureModeAllowHeaderAdd(
      true));
  CallInitiator client = StartCall(NewClientMetadata({}, kPath));
  std::optional<CallHandler> backend = GetNextHandler();
  ASSERT_TRUE(backend.has_value());
  auto pull = StartPullClientInitialMetadata(*backend);
  auto check_call = WaitForCheckCall();
  ASSERT_NE(check_call, nullptr);
  EXPECT_TRUE(GetCheckRequest(*check_call).has_value());
  check_call->SendMessageToClient(kMalformedCheckResponse);
  check_call->MaybeSendStatusToClient(absl::OkStatus());
  EXPECT_THAT(
      FinishPullClientInitialMetadata(pull),
      IsOkAndHolds(Pointee(HasMetadataKeyValue(
          kFailureModeAllowedHeaderKey, kFailureModeAllowedHeaderValue))));
  PushServerTrailingMetadata(*backend,
                             ServerMetadataFromStatus(GRPC_STATUS_OK));
  EXPECT_EQ(PullServerTrailingStatus(client), absl::OkStatus());
  WaitForAllPendingWork();
}

// Cancelling the RPC while the Check call is pending cancels the Check call
// once the RPC is destroyed: that drops the filter's pending
// OnClientInitialMetadata() promise, which holds the last ref to the Check
// call.
FILTER_TEST(ExtAuthzFilterTest, CancelWhileCheckPendingOrphansCheckCall) {
  Init(ConfigBuilder());
  std::optional<CallInitiator> client = StartCall(NewClientMetadata({}, kPath));
  std::optional<CallHandler> backend = GetNextHandler();
  ASSERT_TRUE(backend.has_value());
  StartPullClientInitialMetadata(*backend);
  auto check_call = WaitForCheckCall();
  ASSERT_NE(check_call, nullptr);
  EXPECT_TRUE(GetCheckRequest(*check_call).has_value());
  client->SpawnCancel();
  EXPECT_EQ(PullServerTrailingStatus(*client), absl::CancelledError());
  client.reset();
  backend.reset();
  event_engine()->TickUntilIdle();
  EXPECT_TRUE(check_call->IsOrphaned());
  WaitForAllPendingWork();
}

//
// OK response tests
//

FILTER_TEST(ExtAuthzFilterTest, OkResponseAllows) {
  auto stats_plugin = InitWithStatsPlugin(ConfigBuilder());
  CallInitiator client = StartCall(NewClientMetadata({}, kPath));
  std::optional<CallHandler> backend = GetNextHandler();
  ASSERT_TRUE(backend.has_value());
  auto pull = StartPullClientInitialMetadata(*backend);
  auto check_call = WaitForCheckCall();
  ASSERT_NE(check_call, nullptr);
  EXPECT_TRUE(GetCheckRequest(*check_call).has_value());
  check_call->SendMessageToClient(MakeOkCheckResponse().SerializeAsString());
  check_call->MaybeSendStatusToClient(absl::OkStatus());
  EXPECT_TRUE(FinishPullClientInitialMetadata(pull).ok());
  PushServerTrailingMetadata(*backend,
                             ServerMetadataFromStatus(GRPC_STATUS_OK));
  EXPECT_EQ(PullServerTrailingStatus(client), absl::OkStatus());
  EXPECT_THAT(stats_plugin,
              HasClientMetrics(ExtAuthzMetrics{/*allowed=*/1, /*denied=*/0,
                                               /*failed=*/0,
                                               /*filter_disabled=*/0}));
  WaitForAllPendingWork();
}

// Per gRFC A92, CheckResponse.status alone decides whether the RPC is allowed,
// so a non-OK status denies the RPC even if ok_response is set. Without a
// denied_response, the RPC fails with the default denied status
// (PERMISSION_DENIED), and without a status message, the error has no
// ", error message: ..." suffix.
FILTER_TEST(ExtAuthzFilterTest, NonOkStatusWithOkResponseDenies) {
  Init(ConfigBuilder().SetStatusOnError(kStatusOnError));
  CallInitiator client = StartCall(NewClientMetadata({}, kPath));
  std::optional<CallHandler> backend = GetNextHandler();
  ASSERT_TRUE(backend.has_value());
  auto pull = StartPullClientInitialMetadata(*backend);
  auto check_call = WaitForCheckCall();
  ASSERT_NE(check_call, nullptr);
  EXPECT_TRUE(GetCheckRequest(*check_call).has_value());
  CheckResponse response;
  response.mutable_status()->set_code(GRPC_STATUS_PERMISSION_DENIED);
  response.mutable_ok_response();
  check_call->SendMessageToClient(response.SerializeAsString());
  check_call->MaybeSendStatusToClient(absl::OkStatus());
  EXPECT_FALSE(FinishPullClientInitialMetadata(pull).ok());
  EXPECT_EQ(PullServerTrailingStatus(client),
            absl::PermissionDeniedError(kDeniedNoMessageErrorMessage));
  WaitForAllPendingWork();
}

FILTER_TEST(ExtAuthzFilterTest, OkResponseAdditionsBeforeRemovals) {
  Init(ConfigBuilder());
  CallInitiator client = StartCall(NewClientMetadata({}, kPath));
  std::optional<CallHandler> backend = GetNextHandler();
  ASSERT_TRUE(backend.has_value());
  auto pull = StartPullClientInitialMetadata(*backend);
  auto check_call = WaitForCheckCall();
  ASSERT_NE(check_call, nullptr);
  EXPECT_TRUE(GetCheckRequest(*check_call).has_value());
  CheckResponse response = MakeOkCheckResponse();
  auto* ok = response.mutable_ok_response();
  AddHeader(ok->add_headers(), kCustomHeaderKey, kCustomHeaderValue);
  AddHeader(ok->add_headers(), kCustomHeaderKey2, kCustomHeaderValue2);
  ok->add_headers_to_remove(kCustomHeaderKey);
  check_call->SendMessageToClient(response.SerializeAsString());
  check_call->MaybeSendStatusToClient(absl::OkStatus());
  EXPECT_THAT(FinishPullClientInitialMetadata(pull),
              IsOkAndHolds(Pointee(AllOf(
                  HasMetadataKeyValue(kCustomHeaderKey2, kCustomHeaderValue2),
                  LacksMetadataKey(kCustomHeaderKey)))));
  PushServerTrailingMetadata(*backend,
                             ServerMetadataFromStatus(GRPC_STATUS_OK));
  EXPECT_EQ(PullServerTrailingStatus(client), absl::OkStatus());
  WaitForAllPendingWork();
}

FILTER_TEST(ExtAuthzFilterTest, OkResponseModifyResponseHeaders) {
  Init(ConfigBuilder());
  CallInitiator client = StartCall(NewClientMetadata({}, kPath));
  std::optional<CallHandler> backend = GetNextHandler();
  ASSERT_TRUE(backend.has_value());
  auto pull = StartPullClientInitialMetadata(*backend);
  auto check_call = WaitForCheckCall();
  ASSERT_NE(check_call, nullptr);
  EXPECT_TRUE(GetCheckRequest(*check_call).has_value());
  CheckResponse response = MakeOkCheckResponse();
  auto* ok = response.mutable_ok_response();
  AddHeader(ok->add_response_headers_to_add(), kCustomHeaderKey,
            kCustomHeaderValue);
  AddHeader(ok->add_response_headers_to_add(), kCustomHeaderKey2,
            kCustomHeaderValue2);
  check_call->SendMessageToClient(response.SerializeAsString());
  check_call->MaybeSendStatusToClient(absl::OkStatus());
  EXPECT_TRUE(FinishPullClientInitialMetadata(pull).ok());
  PushServerInitialMetadata(*backend, NewServerMetadata());
  EXPECT_THAT(
      PullServerInitialMetadata(client),
      IsOkAndHolds(Optional(Pointee(AllOf(
          HasMetadataKeyValue(kCustomHeaderKey, kCustomHeaderValue),
          HasMetadataKeyValue(kCustomHeaderKey2, kCustomHeaderValue2))))));
  PushServerTrailingMetadata(*backend,
                             ServerMetadataFromStatus(GRPC_STATUS_OK));
  EXPECT_THAT(
      PullServerTrailingMetadata(client),
      IsOkAndHolds(Pointee(AllOf(HasMetadataResult(absl::OkStatus()),
                                 LacksMetadataKey(kCustomHeaderKey),
                                 LacksMetadataKey(kCustomHeaderKey2)))));
  WaitForAllPendingWork();
}

// response_headers_to_add is applied to the trailing metadata when the server
// replies with trailers-only.
FILTER_TEST(ExtAuthzFilterTest, TrailersOnlyAddsResponseHeadersToTrailers) {
  Init(ConfigBuilder());
  CallInitiator client = StartCall(NewClientMetadata({}, kPath));
  std::optional<CallHandler> backend = GetNextHandler();
  ASSERT_TRUE(backend.has_value());
  auto pull = StartPullClientInitialMetadata(*backend);
  auto check_call = WaitForCheckCall();
  ASSERT_NE(check_call, nullptr);
  EXPECT_TRUE(GetCheckRequest(*check_call).has_value());
  CheckResponse response = MakeOkCheckResponse();
  AddHeader(response.mutable_ok_response()->add_response_headers_to_add(),
            kCustomHeaderKey, kCustomHeaderValue);
  check_call->SendMessageToClient(response.SerializeAsString());
  check_call->MaybeSendStatusToClient(absl::OkStatus());
  EXPECT_TRUE(FinishPullClientInitialMetadata(pull).ok());
  ServerMetadataHandle trailers_only_md = NewServerMetadata();
  trailers_only_md->Set(GrpcTrailersOnly(), true);
  PushServerInitialMetadata(*backend, std::move(trailers_only_md));
  EXPECT_THAT(
      PullServerInitialMetadata(client),
      IsOkAndHolds(Optional(Pointee(LacksMetadataKey(kCustomHeaderKey)))));
  PushServerTrailingMetadata(*backend,
                             ServerMetadataFromStatus(GRPC_STATUS_OK));
  EXPECT_THAT(PullServerTrailingMetadata(client),
              IsOkAndHolds(Pointee(AllOf(
                  HasMetadataResult(absl::OkStatus()),
                  HasMetadataKeyValue(kCustomHeaderKey, kCustomHeaderValue)))));
  WaitForAllPendingWork();
}

// response_headers_to_add is applied to the trailing metadata when the server
// sends trailing metadata without any initial metadata.
FILTER_TEST(ExtAuthzFilterTest,
            NoServerInitialMetadataAddsResponseHeadersToTrailers) {
  Init(ConfigBuilder());
  CallInitiator client = StartCall(NewClientMetadata({}, kPath));
  std::optional<CallHandler> backend = GetNextHandler();
  ASSERT_TRUE(backend.has_value());
  auto pull = StartPullClientInitialMetadata(*backend);
  auto check_call = WaitForCheckCall();
  ASSERT_NE(check_call, nullptr);
  EXPECT_TRUE(GetCheckRequest(*check_call).has_value());
  CheckResponse response = MakeOkCheckResponse();
  AddHeader(response.mutable_ok_response()->add_response_headers_to_add(),
            kCustomHeaderKey, kCustomHeaderValue);
  check_call->SendMessageToClient(response.SerializeAsString());
  check_call->MaybeSendStatusToClient(absl::OkStatus());
  EXPECT_TRUE(FinishPullClientInitialMetadata(pull).ok());
  PushServerTrailingMetadata(*backend,
                             ServerMetadataFromStatus(GRPC_STATUS_OK));
  EXPECT_THAT(PullServerTrailingMetadata(client),
              IsOkAndHolds(Pointee(AllOf(
                  HasMetadataResult(absl::OkStatus()),
                  HasMetadataKeyValue(kCustomHeaderKey, kCustomHeaderValue)))));
  WaitForAllPendingWork();
}

//
// Denied response tests
//

// The RPC fails with the denied response's status (not status_on_error), and
// the denied response's headers are added to the trailing metadata.
FILTER_TEST(ExtAuthzFilterTest, DeniedResponseFails) {
  auto stats_plugin =
      InitWithStatsPlugin(ConfigBuilder().SetStatusOnError(kStatusOnError));
  CallInitiator client = StartCall(NewClientMetadata({}, kPath));
  std::optional<CallHandler> backend = GetNextHandler();
  ASSERT_TRUE(backend.has_value());
  auto pull = StartPullClientInitialMetadata(*backend);
  auto check_call = WaitForCheckCall();
  ASSERT_NE(check_call, nullptr);
  EXPECT_TRUE(GetCheckRequest(*check_call).has_value());
  CheckResponse response = MakeDeniedCheckResponse();
  auto* denied = response.mutable_denied_response();
  AddHeader(denied->add_headers(), kCustomHeaderKey, kCustomHeaderValue);
  AddHeader(denied->add_headers(), kCustomHeaderKey2, kCustomHeaderValue2);
  check_call->SendMessageToClient(response.SerializeAsString());
  check_call->MaybeSendStatusToClient(absl::OkStatus());
  EXPECT_FALSE(FinishPullClientInitialMetadata(pull).ok());
  EXPECT_THAT(
      PullServerTrailingMetadata(client),
      IsOkAndHolds(Pointee(AllOf(
          HasMetadataResult(absl::UnauthenticatedError(kDeniedErrorMessage)),
          HasMetadataKeyValue(kCustomHeaderKey, kCustomHeaderValue),
          HasMetadataKeyValue(kCustomHeaderKey2, kCustomHeaderValue2)))));
  EXPECT_THAT(stats_plugin,
              HasClientMetrics(ExtAuthzMetrics{/*allowed=*/0, /*denied=*/1,
                                               /*failed=*/0,
                                               /*filter_disabled=*/0}));
  WaitForAllPendingWork();
}

// failure_mode_allow only covers failures to get a decision. A real deny from
// the authorization service must still fail the call.
FILTER_TEST(ExtAuthzFilterTest, DeniedResponseWithFailureModeAllowStillDenies) {
  Init(ConfigBuilder()
           .SetFailureModeAllow(true)
           .SetFailureModeAllowHeaderAdd(true)
           .SetStatusOnError(kStatusOnError));
  CallInitiator client = StartCall(NewClientMetadata({}, kPath));
  std::optional<CallHandler> backend = GetNextHandler();
  ASSERT_TRUE(backend.has_value());
  auto pull = StartPullClientInitialMetadata(*backend);
  auto check_call = WaitForCheckCall();
  ASSERT_NE(check_call, nullptr);
  EXPECT_TRUE(GetCheckRequest(*check_call).has_value());
  check_call->SendMessageToClient(
      MakeDeniedCheckResponse().SerializeAsString());
  check_call->MaybeSendStatusToClient(absl::OkStatus());
  EXPECT_FALSE(FinishPullClientInitialMetadata(pull).ok());
  EXPECT_EQ(PullServerTrailingStatus(client),
            absl::UnauthenticatedError(kDeniedErrorMessage));
  WaitForAllPendingWork();
}

//
// Header mutation rules tests
//

FILTER_TEST(ExtAuthzFilterTest,
            DisallowedRequestHeaderMutationFailureModeDeny) {
  auto stats_plugin =
      InitWithStatsPlugin(ConfigBuilder()
                              .SetStatusOnError(kStatusOnError)
                              .SetHeaderMutationRules(DisallowAllRules(
                                  /*disallow_is_error=*/true)));
  CallInitiator client = StartCall(NewClientMetadata({}, kPath));
  std::optional<CallHandler> backend = GetNextHandler();
  ASSERT_TRUE(backend.has_value());
  auto pull = StartPullClientInitialMetadata(*backend);
  auto check_call = WaitForCheckCall();
  ASSERT_NE(check_call, nullptr);
  EXPECT_TRUE(GetCheckRequest(*check_call).has_value());
  CheckResponse response = MakeOkCheckResponse();
  AddHeader(response.mutable_ok_response()->add_headers(), kCustomHeaderKey,
            kCustomHeaderValue);
  check_call->SendMessageToClient(response.SerializeAsString());
  check_call->MaybeSendStatusToClient(absl::OkStatus());
  EXPECT_FALSE(FinishPullClientInitialMetadata(pull).ok());
  EXPECT_EQ(PullServerTrailingStatus(client),
            absl::UnavailableError(kForbiddenHeaderMutationErrorMessage));
  EXPECT_THAT(stats_plugin,
              HasClientMetrics(ExtAuthzMetrics{/*allowed=*/0, /*denied=*/1,
                                               /*failed=*/1,
                                               /*filter_disabled=*/0}));
  WaitForAllPendingWork();
}

FILTER_TEST(ExtAuthzFilterTest,
            DisallowedRequestHeaderMutationFailureModeAllow) {
  auto stats_plugin =
      InitWithStatsPlugin(ConfigBuilder()
                              .SetFailureModeAllow(true)
                              .SetFailureModeAllowHeaderAdd(true)
                              .SetHeaderMutationRules(DisallowAllRules(
                                  /*disallow_is_error=*/true)));
  CallInitiator client = StartCall(NewClientMetadata({}, kPath));
  std::optional<CallHandler> backend = GetNextHandler();
  ASSERT_TRUE(backend.has_value());
  auto pull = StartPullClientInitialMetadata(*backend);
  auto check_call = WaitForCheckCall();
  ASSERT_NE(check_call, nullptr);
  EXPECT_TRUE(GetCheckRequest(*check_call).has_value());
  CheckResponse response = MakeOkCheckResponse();
  AddHeader(response.mutable_ok_response()->add_headers(), kCustomHeaderKey,
            kCustomHeaderValue);
  check_call->SendMessageToClient(response.SerializeAsString());
  check_call->MaybeSendStatusToClient(absl::OkStatus());
  EXPECT_THAT(FinishPullClientInitialMetadata(pull),
              IsOkAndHolds(Pointee(
                  AllOf(HasMetadataKeyValue(kFailureModeAllowedHeaderKey,
                                            kFailureModeAllowedHeaderValue),
                        LacksMetadataKey(kCustomHeaderKey)))));
  PushServerTrailingMetadata(*backend,
                             ServerMetadataFromStatus(GRPC_STATUS_OK));
  EXPECT_EQ(PullServerTrailingStatus(client), absl::OkStatus());
  EXPECT_THAT(stats_plugin,
              HasClientMetrics(ExtAuthzMetrics{/*allowed=*/1, /*denied=*/0,
                                               /*failed=*/1,
                                               /*filter_disabled=*/0}));
  WaitForAllPendingWork();
}

FILTER_TEST(ExtAuthzFilterTest, DisallowedRequestHeaderRemovalFailureModeDeny) {
  Init(ConfigBuilder()
           .SetStatusOnError(kStatusOnError)
           .SetHeaderMutationRules(
               DisallowAllRules(/*disallow_is_error=*/true)));
  CallInitiator client = StartCall(
      NewClientMetadata({{kCustomHeaderKey, kCustomHeaderValue}}, kPath));
  std::optional<CallHandler> backend = GetNextHandler();
  ASSERT_TRUE(backend.has_value());
  auto pull = StartPullClientInitialMetadata(*backend);
  auto check_call = WaitForCheckCall();
  ASSERT_NE(check_call, nullptr);
  EXPECT_TRUE(GetCheckRequest(*check_call).has_value());
  CheckResponse response = MakeOkCheckResponse();
  response.mutable_ok_response()->add_headers_to_remove(kCustomHeaderKey);
  check_call->SendMessageToClient(response.SerializeAsString());
  check_call->MaybeSendStatusToClient(absl::OkStatus());
  EXPECT_FALSE(FinishPullClientInitialMetadata(pull).ok());
  EXPECT_EQ(PullServerTrailingStatus(client),
            absl::UnavailableError(kForbiddenHeaderRemovalErrorMessage));
  WaitForAllPendingWork();
}

FILTER_TEST(ExtAuthzFilterTest,
            DisallowedRequestHeaderRemovalFailureModeAllow) {
  Init(ConfigBuilder()
           .SetFailureModeAllow(true)
           .SetFailureModeAllowHeaderAdd(true)
           .SetHeaderMutationRules(
               DisallowAllRules(/*disallow_is_error=*/true)));
  CallInitiator client = StartCall(
      NewClientMetadata({{kCustomHeaderKey, kCustomHeaderValue}}, kPath));
  std::optional<CallHandler> backend = GetNextHandler();
  ASSERT_TRUE(backend.has_value());
  auto pull = StartPullClientInitialMetadata(*backend);
  auto check_call = WaitForCheckCall();
  ASSERT_NE(check_call, nullptr);
  EXPECT_TRUE(GetCheckRequest(*check_call).has_value());
  CheckResponse response = MakeOkCheckResponse();
  response.mutable_ok_response()->add_headers_to_remove(kCustomHeaderKey);
  check_call->SendMessageToClient(response.SerializeAsString());
  check_call->MaybeSendStatusToClient(absl::OkStatus());
  EXPECT_THAT(FinishPullClientInitialMetadata(pull),
              IsOkAndHolds(Pointee(AllOf(
                  HasMetadataKeyValue(kFailureModeAllowedHeaderKey,
                                      kFailureModeAllowedHeaderValue),
                  HasMetadataKeyValue(kCustomHeaderKey, kCustomHeaderValue)))));
  PushServerTrailingMetadata(*backend,
                             ServerMetadataFromStatus(GRPC_STATUS_OK));
  EXPECT_EQ(PullServerTrailingStatus(client), absl::OkStatus());
  WaitForAllPendingWork();
}

FILTER_TEST(ExtAuthzFilterTest,
            DisallowedResponseHeaderMutationFailureModeDeny) {
  Init(ConfigBuilder()
           .SetStatusOnError(kStatusOnError)
           .SetHeaderMutationRules(
               DisallowAllRules(/*disallow_is_error=*/true)));
  CallInitiator client = StartCall(NewClientMetadata({}, kPath));
  std::optional<CallHandler> backend = GetNextHandler();
  ASSERT_TRUE(backend.has_value());
  auto pull = StartPullClientInitialMetadata(*backend);
  auto check_call = WaitForCheckCall();
  ASSERT_NE(check_call, nullptr);
  EXPECT_TRUE(GetCheckRequest(*check_call).has_value());
  CheckResponse response = MakeOkCheckResponse();
  AddHeader(response.mutable_ok_response()->add_response_headers_to_add(),
            kCustomHeaderKey, kCustomHeaderValue);
  check_call->SendMessageToClient(response.SerializeAsString());
  check_call->MaybeSendStatusToClient(absl::OkStatus());
  EXPECT_FALSE(FinishPullClientInitialMetadata(pull).ok());
  EXPECT_EQ(PullServerTrailingStatus(client),
            absl::UnavailableError(kForbiddenHeaderMutationErrorMessage));
  WaitForAllPendingWork();
}

// Like any other failure to get a valid decision, a disallowed response header
// mutation allows the call when failure_mode_allow is set. The response
// header is not added.
FILTER_TEST(ExtAuthzFilterTest,
            DisallowedResponseHeaderMutationFailureModeAllow) {
  Init(ConfigBuilder()
           .SetFailureModeAllow(true)
           .SetFailureModeAllowHeaderAdd(true)
           .SetHeaderMutationRules(
               DisallowAllRules(/*disallow_is_error=*/true)));
  CallInitiator client = StartCall(NewClientMetadata({}, kPath));
  std::optional<CallHandler> backend = GetNextHandler();
  ASSERT_TRUE(backend.has_value());
  auto pull = StartPullClientInitialMetadata(*backend);
  auto check_call = WaitForCheckCall();
  ASSERT_NE(check_call, nullptr);
  EXPECT_TRUE(GetCheckRequest(*check_call).has_value());
  CheckResponse response = MakeOkCheckResponse();
  AddHeader(response.mutable_ok_response()->add_response_headers_to_add(),
            kCustomHeaderKey, kCustomHeaderValue);
  check_call->SendMessageToClient(response.SerializeAsString());
  check_call->MaybeSendStatusToClient(absl::OkStatus());
  EXPECT_THAT(
      FinishPullClientInitialMetadata(pull),
      IsOkAndHolds(Pointee(HasMetadataKeyValue(
          kFailureModeAllowedHeaderKey, kFailureModeAllowedHeaderValue))));
  PushServerInitialMetadata(*backend, NewServerMetadata());
  EXPECT_THAT(
      PullServerInitialMetadata(client),
      IsOkAndHolds(Optional(Pointee(LacksMetadataKey(kCustomHeaderKey)))));
  PushServerTrailingMetadata(*backend,
                             ServerMetadataFromStatus(GRPC_STATUS_OK));
  EXPECT_EQ(PullServerTrailingStatus(client), absl::OkStatus());
  WaitForAllPendingWork();
}

FILTER_TEST(ExtAuthzFilterTest,
            DisallowedDeniedResponseHeaderMutationFailureModeDeny) {
  auto stats_plugin =
      InitWithStatsPlugin(ConfigBuilder()
                              .SetStatusOnError(kStatusOnError)
                              .SetHeaderMutationRules(DisallowAllRules(
                                  /*disallow_is_error=*/true)));
  CallInitiator client = StartCall(NewClientMetadata({}, kPath));
  std::optional<CallHandler> backend = GetNextHandler();
  ASSERT_TRUE(backend.has_value());
  auto pull = StartPullClientInitialMetadata(*backend);
  auto check_call = WaitForCheckCall();
  ASSERT_NE(check_call, nullptr);
  EXPECT_TRUE(GetCheckRequest(*check_call).has_value());
  CheckResponse response = MakeDeniedCheckResponse();
  AddHeader(response.mutable_denied_response()->add_headers(), kCustomHeaderKey,
            kCustomHeaderValue);
  check_call->SendMessageToClient(response.SerializeAsString());
  check_call->MaybeSendStatusToClient(absl::OkStatus());
  EXPECT_FALSE(FinishPullClientInitialMetadata(pull).ok());
  EXPECT_EQ(PullServerTrailingStatus(client),
            absl::UnavailableError(kForbiddenHeaderMutationErrorMessage));
  EXPECT_THAT(stats_plugin,
              HasClientMetrics(ExtAuthzMetrics{/*allowed=*/0, /*denied=*/1,
                                               /*failed=*/1,
                                               /*filter_disabled=*/0}));
  WaitForAllPendingWork();
}

// A disallowed trailer mutation on a denied response counts as a failure to
// get a valid decision, so failure_mode_allow allows the call.
FILTER_TEST(ExtAuthzFilterTest,
            DisallowedDeniedResponseHeaderMutationFailureModeAllow) {
  auto stats_plugin =
      InitWithStatsPlugin(ConfigBuilder()
                              .SetFailureModeAllow(true)
                              .SetFailureModeAllowHeaderAdd(true)
                              .SetHeaderMutationRules(DisallowAllRules(
                                  /*disallow_is_error=*/true)));
  CallInitiator client = StartCall(NewClientMetadata({}, kPath));
  std::optional<CallHandler> backend = GetNextHandler();
  ASSERT_TRUE(backend.has_value());
  auto pull = StartPullClientInitialMetadata(*backend);
  auto check_call = WaitForCheckCall();
  ASSERT_NE(check_call, nullptr);
  EXPECT_TRUE(GetCheckRequest(*check_call).has_value());
  CheckResponse response = MakeDeniedCheckResponse();
  AddHeader(response.mutable_denied_response()->add_headers(), kCustomHeaderKey,
            kCustomHeaderValue);
  check_call->SendMessageToClient(response.SerializeAsString());
  check_call->MaybeSendStatusToClient(absl::OkStatus());
  EXPECT_THAT(
      FinishPullClientInitialMetadata(pull),
      IsOkAndHolds(Pointee(HasMetadataKeyValue(
          kFailureModeAllowedHeaderKey, kFailureModeAllowedHeaderValue))));
  PushServerTrailingMetadata(*backend,
                             ServerMetadataFromStatus(GRPC_STATUS_OK));
  EXPECT_THAT(PullServerTrailingMetadata(client),
              IsOkAndHolds(Pointee(AllOf(HasMetadataResult(absl::OkStatus()),
                                         LacksMetadataKey(kCustomHeaderKey)))));
  EXPECT_THAT(stats_plugin,
              HasClientMetrics(ExtAuthzMetrics{/*allowed=*/1, /*denied=*/0,
                                               /*failed=*/1,
                                               /*filter_disabled=*/0}));
  WaitForAllPendingWork();
}

// All mutations are checked before any are applied, so when one of them is
// disallowed, the allowed ones are not applied either.
FILTER_TEST(ExtAuthzFilterTest,
            DisallowedMutationPreventsAllMutationsWithFailureModeAllow) {
  // With only disallow_is_error set, the rules allow every mutation except
  // those that gRPC never allows, such as removing "host", and fail on those.
  HeaderMutationRules rules;
  rules.disallow_is_error = true;
  Init(ConfigBuilder()
           .SetFailureModeAllow(true)
           .SetFailureModeAllowHeaderAdd(true)
           .SetHeaderMutationRules(std::move(rules)));
  CallInitiator client = StartCall(NewClientMetadata({}, kPath));
  std::optional<CallHandler> backend = GetNextHandler();
  ASSERT_TRUE(backend.has_value());
  auto pull = StartPullClientInitialMetadata(*backend);
  auto check_call = WaitForCheckCall();
  ASSERT_NE(check_call, nullptr);
  EXPECT_TRUE(GetCheckRequest(*check_call).has_value());
  CheckResponse response = MakeOkCheckResponse();
  auto* ok = response.mutable_ok_response();
  AddHeader(ok->add_headers(), kCustomHeaderKey, kCustomHeaderValue);
  ok->add_headers_to_remove(kHostHeaderKey);
  AddHeader(ok->add_response_headers_to_add(), kCustomHeaderKey2,
            kCustomHeaderValue2);
  check_call->SendMessageToClient(response.SerializeAsString());
  check_call->MaybeSendStatusToClient(absl::OkStatus());
  EXPECT_THAT(FinishPullClientInitialMetadata(pull),
              IsOkAndHolds(Pointee(
                  AllOf(HasMetadataKeyValue(kFailureModeAllowedHeaderKey,
                                            kFailureModeAllowedHeaderValue),
                        LacksMetadataKey(kCustomHeaderKey)))));
  PushServerInitialMetadata(*backend, NewServerMetadata());
  EXPECT_THAT(
      PullServerInitialMetadata(client),
      IsOkAndHolds(Optional(Pointee(LacksMetadataKey(kCustomHeaderKey2)))));
  PushServerTrailingMetadata(*backend,
                             ServerMetadataFromStatus(GRPC_STATUS_OK));
  EXPECT_EQ(PullServerTrailingStatus(client), absl::OkStatus());
  WaitForAllPendingWork();
}

// With disallow_is_error=false, disallowed mutations on an OK response are
// skipped, and are not treated as a failure: even with failure_mode_allow,
// the failure header is not added.
FILTER_TEST(ExtAuthzFilterTest, DisallowedMutationsIgnoredOnOkResponse) {
  Init(ConfigBuilder()
           .SetFailureModeAllow(true)
           .SetFailureModeAllowHeaderAdd(true)
           .SetHeaderMutationRules(
               DisallowAllRules(/*disallow_is_error=*/false)));
  CallInitiator client = StartCall(
      NewClientMetadata({{kCustomHeaderKey2, kCustomHeaderValue2}}, kPath));
  std::optional<CallHandler> backend = GetNextHandler();
  ASSERT_TRUE(backend.has_value());
  auto pull = StartPullClientInitialMetadata(*backend);
  auto check_call = WaitForCheckCall();
  ASSERT_NE(check_call, nullptr);
  EXPECT_TRUE(GetCheckRequest(*check_call).has_value());
  CheckResponse response = MakeOkCheckResponse();
  auto* ok = response.mutable_ok_response();
  AddHeader(ok->add_headers(), kCustomHeaderKey, kCustomHeaderValue);
  ok->add_headers_to_remove(kCustomHeaderKey2);
  AddHeader(ok->add_response_headers_to_add(), kCustomHeaderKey,
            kCustomHeaderValue);
  check_call->SendMessageToClient(response.SerializeAsString());
  check_call->MaybeSendStatusToClient(absl::OkStatus());
  EXPECT_THAT(FinishPullClientInitialMetadata(pull),
              IsOkAndHolds(Pointee(AllOf(
                  LacksMetadataKey(kCustomHeaderKey),
                  HasMetadataKeyValue(kCustomHeaderKey2, kCustomHeaderValue2),
                  LacksMetadataKey(kFailureModeAllowedHeaderKey)))));
  PushServerInitialMetadata(*backend, NewServerMetadata());
  EXPECT_THAT(
      PullServerInitialMetadata(client),
      IsOkAndHolds(Optional(Pointee(LacksMetadataKey(kCustomHeaderKey)))));
  PushServerTrailingMetadata(*backend,
                             ServerMetadataFromStatus(GRPC_STATUS_OK));
  EXPECT_EQ(PullServerTrailingStatus(client), absl::OkStatus());
  WaitForAllPendingWork();
}

// With disallow_is_error=false, disallowed headers on a denied response are
// skipped and the deny status is kept.
FILTER_TEST(ExtAuthzFilterTest, DisallowedMutationsIgnoredOnDeniedResponse) {
  Init(ConfigBuilder()
           .SetStatusOnError(kStatusOnError)
           .SetHeaderMutationRules(
               DisallowAllRules(/*disallow_is_error=*/false)));
  CallInitiator client = StartCall(NewClientMetadata({}, kPath));
  std::optional<CallHandler> backend = GetNextHandler();
  ASSERT_TRUE(backend.has_value());
  auto pull = StartPullClientInitialMetadata(*backend);
  auto check_call = WaitForCheckCall();
  ASSERT_NE(check_call, nullptr);
  EXPECT_TRUE(GetCheckRequest(*check_call).has_value());
  CheckResponse response = MakeDeniedCheckResponse();
  AddHeader(response.mutable_denied_response()->add_headers(), kCustomHeaderKey,
            kCustomHeaderValue);
  check_call->SendMessageToClient(response.SerializeAsString());
  check_call->MaybeSendStatusToClient(absl::OkStatus());
  EXPECT_FALSE(FinishPullClientInitialMetadata(pull).ok());
  EXPECT_THAT(
      PullServerTrailingMetadata(client),
      IsOkAndHolds(Pointee(AllOf(
          HasMetadataResult(absl::UnauthenticatedError(kDeniedErrorMessage)),
          LacksMetadataKey(kCustomHeaderKey)))));
  WaitForAllPendingWork();
}

//
// CheckRequest tests
//

FILTER_TEST(ExtAuthzFilterTest, CheckRequestHasCallPath) {
  Init(ConfigBuilder());
  std::optional<CheckRequest> request =
      CaptureCheckRequest(NewClientMetadata({}, kPath));
  ASSERT_TRUE(request.has_value());
  EXPECT_EQ(request->attributes().request().http().path(), kPath);
}

// Only the headers matching allowed_headers are sent to the authorization
// service.
FILTER_TEST(ExtAuthzFilterTest, HeaderMapContainsOnlyAllowedHeaders) {
  Init(ConfigBuilder().AddAllowedHeader(kCustomHeaderKey));
  std::optional<CheckRequest> request = CaptureCheckRequest(
      NewClientMetadata({{kCustomHeaderKey, kCustomHeaderValue},
                         {kCustomHeaderKey2, kCustomHeaderValue2}},
                        kPath));
  ASSERT_TRUE(request.has_value());
  EXPECT_THAT(request->attributes().request().http().header_map().headers(),
              ElementsAre(AllOf(
                  Property(&HeaderValue::key, kCustomHeaderKey),
                  Property(&HeaderValue::raw_value, kCustomHeaderValue))));
}

// Headers matching disallowed_headers are not sent to the authorization
// service.
FILTER_TEST(ExtAuthzFilterTest, HeaderMapOmitsDisallowedHeaders) {
  Init(ConfigBuilder().AddDisallowedHeader(kCustomHeaderKey2));
  std::optional<CheckRequest> request = CaptureCheckRequest(
      NewClientMetadata({{kCustomHeaderKey, kCustomHeaderValue},
                         {kCustomHeaderKey2, kCustomHeaderValue2}},
                        kPath));
  ASSERT_TRUE(request.has_value());
  const auto& headers =
      request->attributes().request().http().header_map().headers();
  EXPECT_THAT(headers, Contains(Property(&HeaderValue::key, kCustomHeaderKey)));
  EXPECT_THAT(headers,
              Not(Contains(Property(&HeaderValue::key, kCustomHeaderKey2))));
}

// The same connection information is present in the channel args on the
// client side, but per gRFC A92 it must not be reported there.
FILTER_TEST(ExtAuthzFilterTest, ClientCallOmitsSourceAndDestination) {
  Init(ConfigBuilder(),
       ChannelArgs()
           .Set(GRPC_ARG_ENDPOINT_PEER_ADDRESS, kPeerAddressUri)
           .Set(GRPC_ARG_ENDPOINT_LOCAL_ADDRESS, kLocalAddressUri)
           .SetObject(MakeAuthContext()));
  std::optional<CheckRequest> request =
      CaptureCheckRequest(NewClientMetadata({}, kPath));
  ASSERT_TRUE(request.has_value());
  EXPECT_TRUE(request->has_attributes());
  const auto& attr = request->attributes();
  EXPECT_TRUE(attr.has_request());
  EXPECT_FALSE(attr.has_source());
  EXPECT_FALSE(attr.has_destination());
}

//
// Server-side tests
//
// The filter behaves the same on the client and server, except that on the
// server it records metrics in the server telemetry domain and populates
// AttributeContext.source and .destination from the connection.
//

FILTER_TEST(ExtAuthzFilterTest, ServerSideMetrics) {
  auto stats_plugin = InitWithStatsPlugin(
      ConfigBuilder(),
      ChannelArgs().Set(GRPC_ARG_IS_SERVER_FILTER_STACK, true));
  CallInitiator client = StartCall(NewClientMetadata({}, kPath));
  std::optional<CallHandler> backend = GetNextHandler();
  ASSERT_TRUE(backend.has_value());
  auto pull = StartPullClientInitialMetadata(*backend);
  auto check_call = WaitForCheckCall();
  ASSERT_NE(check_call, nullptr);
  EXPECT_TRUE(GetCheckRequest(*check_call).has_value());
  check_call->SendMessageToClient(MakeOkCheckResponse().SerializeAsString());
  check_call->MaybeSendStatusToClient(absl::OkStatus());
  EXPECT_TRUE(FinishPullClientInitialMetadata(pull).ok());
  PushServerTrailingMetadata(*backend,
                             ServerMetadataFromStatus(GRPC_STATUS_OK));
  EXPECT_EQ(PullServerTrailingStatus(client), absl::OkStatus());
  // The server metrics have no labels. Only allowed_rpcs was incremented.
  EXPECT_THAT(stats_plugin,
              HasServerMetrics(ExtAuthzMetrics{/*allowed=*/1, /*denied=*/0,
                                               /*failed=*/0,
                                               /*filter_disabled=*/0}));
  EXPECT_EQ(stats_plugin->GetUInt64MetricValueByName(
                "grpc.client_ext_authz.allowed_rpcs", {kServerUri}),
            std::nullopt);
  WaitForAllPendingWork();
}

FILTER_TEST(ExtAuthzFilterTest, ServerSidePopulatesSourceAndDestination) {
  Init(ConfigBuilder(), ServerArgs(MakeAuthContext()));
  std::optional<CheckRequest> request =
      CaptureCheckRequest(NewClientMetadata({}, kPath));
  ASSERT_TRUE(request.has_value());
  const auto& attr = request->attributes();
  EXPECT_THAT(attr.source().principal(), StrEq(kUriSan));
  EXPECT_EQ(attr.source().address().socket_address().address(),
            kPeerAddressHost);
  EXPECT_EQ(attr.source().address().socket_address().port_value(),
            kPeerAddressPort);
  // The destination principal comes from this endpoint's own certificate.
  EXPECT_THAT(attr.destination().principal(), StrEq(kLocalPrincipal));
  EXPECT_EQ(attr.destination().address().socket_address().address(),
            kLocalAddressHost);
  EXPECT_EQ(attr.destination().address().socket_address().port_value(),
            kLocalAddressPort);
}

FILTER_TEST(ExtAuthzFilterTest,
            ServerSideIncludesPeerCertificateWhenConfigured) {
  auto auth_context = MakeAuthContext();
  auth_context->add_cstring_property(GRPC_X509_PEM_CERT_PROPERTY_NAME,
                                     kPemCert);
  Init(ConfigBuilder().SetIncludePeerCertificate(true),
       ServerArgs(std::move(auth_context)));
  std::optional<CheckRequest> request =
      CaptureCheckRequest(NewClientMetadata({}, kPath));
  ASSERT_TRUE(request.has_value());
  EXPECT_EQ(request->attributes().source().certificate(), kEncodedCert);
}

// The peer certificate is sent only when the config asks for it, even if the
// connection has one.
FILTER_TEST(ExtAuthzFilterTest, ServerSideOmitsPeerCertificateByDefault) {
  auto auth_context = MakeAuthContext();
  auth_context->add_cstring_property(GRPC_X509_PEM_CERT_PROPERTY_NAME,
                                     kPemCert);
  Init(ConfigBuilder(), ServerArgs(std::move(auth_context)));
  std::optional<CheckRequest> request =
      CaptureCheckRequest(NewClientMetadata({}, kPath));
  ASSERT_TRUE(request.has_value());
  EXPECT_TRUE(request->attributes().has_source());
  EXPECT_THAT(request->attributes().source().certificate(), IsEmpty());
}

// A connection need not have an auth context; addresses are still reported.
FILTER_TEST(ExtAuthzFilterTest, ServerSideWithoutAuthContext) {
  Init(ConfigBuilder().SetIncludePeerCertificate(true),
       ServerArgs(/*auth_context=*/nullptr));
  std::optional<CheckRequest> request =
      CaptureCheckRequest(NewClientMetadata({}, kPath));
  ASSERT_TRUE(request.has_value());
  const auto& attr = request->attributes();
  EXPECT_THAT(attr.source().principal(), IsEmpty());
  EXPECT_THAT(attr.source().certificate(), IsEmpty());
  EXPECT_THAT(attr.destination().principal(), IsEmpty());
  EXPECT_EQ(attr.source().address().socket_address().address(),
            kPeerAddressHost);
  EXPECT_EQ(attr.destination().address().socket_address().address(),
            kLocalAddressHost);
}

}  // namespace
}  // namespace grpc_core
