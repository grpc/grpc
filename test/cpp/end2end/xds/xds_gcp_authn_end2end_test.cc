//
// Copyright 2024 gRPC authors.
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

#include <string>
#include <vector>

#include "gmock/gmock.h"
#include "gtest/gtest.h"

#include <grpc/support/string_util.h>

#include "src/core/client_channel/backup_poller.h"
#include "src/core/lib/config/config_vars.h"
#include "src/core/util/http_client/httpcli.h"
#include "src/proto/grpc/testing/xds/v3/cluster.grpc.pb.h"
#include "src/proto/grpc/testing/xds/v3/gcp_authn.grpc.pb.h"
#include "src/proto/grpc/testing/xds/v3/http_connection_manager.grpc.pb.h"
#include "src/proto/grpc/testing/xds/v3/router.grpc.pb.h"
#include "test/core/test_util/scoped_env_var.h"
#include "test/core/test_util/test_config.h"
#include "test/cpp/end2end/xds/xds_end2end_test_lib.h"

namespace grpc {
namespace testing {
namespace {

using ::envoy::extensions::filters::http::gcp_authn::v3::Audience;
using ::envoy::extensions::filters::http::gcp_authn::v3::GcpAuthnFilterConfig;
using ::envoy::extensions::filters::network::http_connection_manager::v3::
    HttpFilter;

constexpr absl::string_view kFilterInstanceName = "gcp_authn_instance";
constexpr absl::string_view kAudience = "audience";

class XdsGcpAuthnEnd2endTest : public XdsEnd2endTest {
 public:
  void SetUp() override {
    g_audience = "";
    g_token = nullptr;
    grpc_core::HttpRequest::SetOverride(HttpGetOverride, nullptr, nullptr);
    InitClient(MakeBootstrapBuilder(), /*lb_expected_authority=*/"",
               /*xds_resource_does_not_exist_timeout_ms=*/0,
               /*balancer_authority_override=*/"", /*args=*/nullptr,
               CreateTlsChannelCredentials());
  }

  void TearDown() override {
    XdsEnd2endTest::TearDown();
    grpc_core::HttpRequest::SetOverride(nullptr, nullptr, nullptr);
  }

  static void ValidateHttpRequest(const grpc_http_request* request,
                                  const grpc_core::URI& uri) {
    EXPECT_THAT(
        uri.query_parameter_map(),
        ::testing::ElementsAre(::testing::Pair("audience", g_audience)));
    ASSERT_EQ(request->hdr_count, 1);
    EXPECT_EQ(absl::string_view(request->hdrs[0].key), "Metadata-Flavor");
    EXPECT_EQ(absl::string_view(request->hdrs[0].value), "Google");
  }

  static int HttpGetOverride(const grpc_http_request* request,
                             const grpc_core::URI& uri,
                             grpc_core::Timestamp /*deadline*/,
                             grpc_closure* on_done,
                             grpc_http_response* response) {
    // Intercept only requests for GCP service account identity tokens.
    if (uri.authority() != "metadata.google.internal." ||
        uri.path() !=
        "/computeMetadata/v1/instance/service-accounts/default/identity") {
      return 0;
    }
    // Validate request.
    ValidateHttpRequest(request, uri);
    // Generate response.
    response->status = 200;
    response->body = gpr_strdup(const_cast<char*>(g_token));
    response->body_length = strlen(g_token);
    grpc_core::ExecCtx::Run(DEBUG_LOCATION, on_done, absl::OkStatus());
    return 1;
  }

  // Constructs a synthetic JWT token that's just valid enough for the
  // call creds to extract the expiration date.
  static std::string MakeToken(grpc_core::Timestamp expiration) {
    gpr_timespec ts = expiration.as_timespec(GPR_CLOCK_REALTIME);
    std::string json = absl::StrCat("{\"exp\":", ts.tv_sec, "}");
    return absl::StrCat("foo.", absl::WebSafeBase64Escape(json), ".bar");
  }

  Listener BuildListenerWithGcpAuthnFilter(
      const GcpAuthnFilterConfig& gcp_authn_config = GcpAuthnFilterConfig()) {
    Listener listener = default_listener_;
    HttpConnectionManager hcm = ClientHcmAccessor().Unpack(listener);
    HttpFilter* filter0 = hcm.mutable_http_filters(0);
    *hcm.add_http_filters() = *filter0;
    filter0->set_name(kFilterInstanceName);
    filter0->mutable_typed_config()->PackFrom(gcp_authn_config);
    ClientHcmAccessor().Pack(hcm, &listener);
    return listener;
  }

  Cluster BuildClusterWithAudience(absl::string_view audience) {
    Audience audience_proto;
    audience_proto.set_url(audience);
    Cluster cluster = default_cluster_;
    auto& filter_map =
        *cluster.mutable_metadata()->mutable_typed_filter_metadata();
    auto& entry = filter_map[kFilterInstanceName];
    entry.PackFrom(audience_proto);
    return cluster;
  }

  static absl::string_view g_audience;
  static const char* g_token;
};

absl::string_view XdsGcpAuthnEnd2endTest::g_audience;
const char* XdsGcpAuthnEnd2endTest::g_token;

INSTANTIATE_TEST_SUITE_P(XdsTest, XdsGcpAuthnEnd2endTest,
                         ::testing::Values(XdsTestType()), &XdsTestType::Name);

TEST_P(XdsGcpAuthnEnd2endTest, Basic) {
  grpc_core::testing::ScopedExperimentalEnvVar env(
      "GRPC_EXPERIMENTAL_XDS_GCP_AUTHENTICATION_FILTER");
  // Construct auth token.
  g_audience = kAudience;
  std::string token = MakeToken(grpc_core::Timestamp::InfFuture());
  g_token = token.c_str();
  // Set xDS resources.
  CreateAndStartBackends(1, /*xds_enabled=*/false,
                         CreateTlsServerCredentials());
  SetListenerAndRouteConfiguration(
      balancer_.get(), BuildListenerWithGcpAuthnFilter(),
      default_route_config_);
  balancer_->ads_service()->SetCdsResource(BuildClusterWithAudience(kAudience));
  EdsResourceArgs args({{"locality0", CreateEndpointsForBackends()}});
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
  // Send an RPC and check that it arrives with the right auth token.
  std::multimap<std::string, std::string> server_initial_metadata;
  Status status = SendRpc(RpcOptions().set_echo_metadata_initially(true),
                          /*response=*/nullptr, &server_initial_metadata);
  EXPECT_TRUE(status.ok())
      << "code=" << status.error_code() << " message="
      << status.error_message();
  EXPECT_THAT(server_initial_metadata, ::testing::Contains(
      ::testing::Pair("authorization", absl::StrCat("Bearer ", g_token))));
}

#if 0
// Without the listener config, the fault injection won't be enabled.
TEST_P(FaultInjectionTest, XdsFaultInjectionWithoutListenerFilter) {
  CreateAndStartBackends(1);
  const uint32_t kAbortPercentagePerHundred = 100;
  // Create an EDS resource
  EdsResourceArgs args({{"locality0", CreateEndpointsForBackends()}});
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
  // Construct the fault injection filter config
  HTTPFault http_fault;
  auto* abort_percentage = http_fault.mutable_abort()->mutable_percentage();
  abort_percentage->set_numerator(kAbortPercentagePerHundred);
  abort_percentage->set_denominator(FractionalPercent::HUNDRED);
  http_fault.mutable_abort()->set_grpc_status(
      static_cast<uint32_t>(StatusCode::ABORTED));
  // Turn on fault injection
  RouteConfiguration route =
      BuildRouteConfigurationWithFaultInjection(http_fault);
  SetListenerAndRouteConfiguration(balancer_.get(), default_listener_, route);
  // Fire several RPCs, and expect all of them to be pass.
  CheckRpcSendOk(DEBUG_LOCATION, 5, RpcOptions().set_wait_for_ready(true));
}

TEST_P(FaultInjectionTest, XdsFaultInjectionPercentageAbort) {
  CreateAndStartBackends(1);
  const uint32_t kAbortPercentagePerHundred = 50;
  const double kAbortRate = kAbortPercentagePerHundred / 100.0;
  const double kErrorTolerance = 0.1;
  const size_t kNumRpcs = ComputeIdealNumRpcs(kAbortRate, kErrorTolerance);
  // Create an EDS resource
  EdsResourceArgs args({{"locality0", CreateEndpointsForBackends()}});
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
  // Construct the fault injection filter config
  HTTPFault http_fault;
  auto* abort_percentage = http_fault.mutable_abort()->mutable_percentage();
  abort_percentage->set_numerator(kAbortPercentagePerHundred);
  abort_percentage->set_denominator(FractionalPercent::HUNDRED);
  http_fault.mutable_abort()->set_grpc_status(
      static_cast<uint32_t>(StatusCode::ABORTED));
  // Config fault injection via different setup
  SetFilterConfig(http_fault);
  // Send kNumRpcs RPCs and count the aborts.
  size_t num_aborted = SendRpcsAndCountFailuresWithMessage(
      DEBUG_LOCATION, kNumRpcs, StatusCode::ABORTED, "Fault injected");
  // The abort rate should be roughly equal to the expectation.
  const double seen_abort_rate = static_cast<double>(num_aborted) / kNumRpcs;
  EXPECT_THAT(seen_abort_rate,
              ::testing::DoubleNear(kAbortRate, kErrorTolerance));
}

TEST_P(FaultInjectionTest, XdsFaultInjectionPercentageAbortViaHeaders) {
  CreateAndStartBackends(1);
  const uint32_t kAbortPercentageCap = 100;
  const uint32_t kAbortPercentage = 50;
  const double kAbortRate = kAbortPercentage / 100.0;
  const double kErrorTolerance = 0.1;
  const size_t kNumRpcs = ComputeIdealNumRpcs(kAbortRate, kErrorTolerance);
  // Create an EDS resource
  EdsResourceArgs args({{"locality0", CreateEndpointsForBackends()}});
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
  // Construct the fault injection filter config
  HTTPFault http_fault;
  http_fault.mutable_abort()->mutable_header_abort();
  http_fault.mutable_abort()->mutable_percentage()->set_numerator(
      kAbortPercentageCap);
  // Config fault injection via different setup
  SetFilterConfig(http_fault);
  // Send kNumRpcs RPCs and count the aborts.
  std::vector<std::pair<std::string, std::string>> metadata = {
      {"x-envoy-fault-abort-grpc-request", "10"},
      {"x-envoy-fault-abort-percentage", std::to_string(kAbortPercentage)},
  };
  size_t num_aborted = SendRpcsAndCountFailuresWithMessage(
      DEBUG_LOCATION, kNumRpcs, StatusCode::ABORTED, "Fault injected",
      RpcOptions().set_metadata(metadata));
  // The abort rate should be roughly equal to the expectation.
  const double seen_abort_rate = static_cast<double>(num_aborted) / kNumRpcs;
  EXPECT_THAT(seen_abort_rate,
              ::testing::DoubleNear(kAbortRate, kErrorTolerance));
}

TEST_P(FaultInjectionTest, XdsFaultInjectionPercentageDelay) {
  CreateAndStartBackends(1);
  const auto kRpcTimeout = grpc_core::Duration::Seconds(10);
  const auto kFixedDelay = grpc_core::Duration::Seconds(20);
  const uint32_t kDelayPercentagePerHundred = 50;
  const double kDelayRate = kDelayPercentagePerHundred / 100.0;
  const double kErrorTolerance = 0.1;
  const size_t kNumRpcs = ComputeIdealNumRpcs(kDelayRate, kErrorTolerance);
  const size_t kMaxConcurrentRequests = kNumRpcs;
  // Create an EDS resource
  EdsResourceArgs args({{"locality0", CreateEndpointsForBackends()}});
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
  // Loosen the max concurrent request limit
  Cluster cluster = default_cluster_;
  auto* threshold = cluster.mutable_circuit_breakers()->add_thresholds();
  threshold->set_priority(RoutingPriority::DEFAULT);
  threshold->mutable_max_requests()->set_value(kMaxConcurrentRequests);
  balancer_->ads_service()->SetCdsResource(cluster);
  // Construct the fault injection filter config
  HTTPFault http_fault;
  auto* delay_percentage = http_fault.mutable_delay()->mutable_percentage();
  delay_percentage->set_numerator(kDelayPercentagePerHundred);
  delay_percentage->set_denominator(FractionalPercent::HUNDRED);
  SetProtoDuration(kFixedDelay,
                   http_fault.mutable_delay()->mutable_fixed_delay());
  // Config fault injection via different setup
  SetFilterConfig(http_fault);
  // Make sure channel is connected.  This avoids flakiness caused by
  // having multiple queued RPCs proceed in parallel when the name
  // resolution response is returned to the channel.
  channel_->WaitForConnected(grpc_timeout_milliseconds_to_deadline(15000));
  // Send kNumRpcs RPCs and count the delays.
  RpcOptions rpc_options =
      RpcOptions().set_timeout(kRpcTimeout).set_skip_cancelled_check(true);
  std::vector<ConcurrentRpc> rpcs =
      SendConcurrentRpcs(DEBUG_LOCATION, stub_.get(), kNumRpcs, rpc_options);
  size_t num_delayed = 0;
  for (auto& rpc : rpcs) {
    if (rpc.status.error_code() == StatusCode::OK) continue;
    EXPECT_EQ(StatusCode::DEADLINE_EXCEEDED, rpc.status.error_code());
    ++num_delayed;
  }
  // The delay rate should be roughly equal to the expectation.
  const double seen_delay_rate = static_cast<double>(num_delayed) / kNumRpcs;
  EXPECT_THAT(seen_delay_rate,
              ::testing::DoubleNear(kDelayRate, kErrorTolerance));
}

TEST_P(FaultInjectionTest, XdsFaultInjectionPercentageDelayViaHeaders) {
  CreateAndStartBackends(1);
  const auto kRpcTimeout = grpc_core::Duration::Seconds(10);
  const auto kFixedDelay = grpc_core::Duration::Seconds(20);
  const uint32_t kDelayPercentageCap = 100;
  const uint32_t kDelayPercentage = 50;
  const double kDelayRate = kDelayPercentage / 100.0;
  const double kErrorTolerance = 0.1;
  const size_t kNumRpcs = ComputeIdealNumRpcs(kDelayRate, kErrorTolerance);
  const size_t kMaxConcurrentRequests = kNumRpcs;
  // Create an EDS resource
  EdsResourceArgs args({{"locality0", CreateEndpointsForBackends()}});
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
  // Loosen the max concurrent request limit
  Cluster cluster = default_cluster_;
  auto* threshold = cluster.mutable_circuit_breakers()->add_thresholds();
  threshold->set_priority(RoutingPriority::DEFAULT);
  threshold->mutable_max_requests()->set_value(kMaxConcurrentRequests);
  balancer_->ads_service()->SetCdsResource(cluster);
  // Construct the fault injection filter config
  HTTPFault http_fault;
  http_fault.mutable_delay()->mutable_header_delay();
  http_fault.mutable_delay()->mutable_percentage()->set_numerator(
      kDelayPercentageCap);
  // Config fault injection via different setup
  SetFilterConfig(http_fault);
  // Make sure channel is connected.  This avoids flakiness caused by
  // having multiple queued RPCs proceed in parallel when the name
  // resolution response is returned to the channel.
  channel_->WaitForConnected(grpc_timeout_milliseconds_to_deadline(15000));
  // Send kNumRpcs RPCs and count the delays.
  std::vector<std::pair<std::string, std::string>> metadata = {
      {"x-envoy-fault-delay-request",
       std::to_string(kFixedDelay.millis() * grpc_test_slowdown_factor())},
      {"x-envoy-fault-delay-request-percentage",
       std::to_string(kDelayPercentage)},
  };
  RpcOptions rpc_options = RpcOptions()
                               .set_metadata(metadata)
                               .set_timeout(kRpcTimeout)
                               .set_skip_cancelled_check(true);
  std::vector<ConcurrentRpc> rpcs =
      SendConcurrentRpcs(DEBUG_LOCATION, stub_.get(), kNumRpcs, rpc_options);
  size_t num_delayed = 0;
  for (auto& rpc : rpcs) {
    if (rpc.status.error_code() == StatusCode::OK) continue;
    EXPECT_EQ(StatusCode::DEADLINE_EXCEEDED, rpc.status.error_code());
    ++num_delayed;
  }
  // The delay rate should be roughly equal to the expectation.
  const double seen_delay_rate = static_cast<double>(num_delayed) / kNumRpcs;
  EXPECT_THAT(seen_delay_rate,
              ::testing::DoubleNear(kDelayRate, kErrorTolerance));
}

TEST_P(FaultInjectionTest, XdsFaultInjectionAbortAfterDelayForStreamCall) {
  CreateAndStartBackends(1);
  const auto kRpcTimeout = grpc_core::Duration::Seconds(30);
  const auto kFixedDelay = grpc_core::Duration::Seconds(1);
  // Create an EDS resource
  EdsResourceArgs args({{"locality0", CreateEndpointsForBackends()}});
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
  // Construct the fault injection filter config
  HTTPFault http_fault;
  auto* abort_percentage = http_fault.mutable_abort()->mutable_percentage();
  abort_percentage->set_numerator(100);  // Always inject ABORT!
  abort_percentage->set_denominator(FractionalPercent::HUNDRED);
  http_fault.mutable_abort()->set_grpc_status(
      static_cast<uint32_t>(StatusCode::ABORTED));
  auto* delay_percentage = http_fault.mutable_delay()->mutable_percentage();
  delay_percentage->set_numerator(100);  // Always inject DELAY!
  delay_percentage->set_denominator(FractionalPercent::HUNDRED);
  SetProtoDuration(kFixedDelay,
                   http_fault.mutable_delay()->mutable_fixed_delay());
  // Config fault injection via different setup
  SetFilterConfig(http_fault);
  // Send a stream RPC and check its status code
  ClientContext context;
  context.set_deadline(
      grpc_timeout_milliseconds_to_deadline(kRpcTimeout.millis()));
  auto stream = stub_->BidiStream(&context);
  stream->WritesDone();
  auto status = stream->Finish();
  EXPECT_EQ(StatusCode::ABORTED, status.error_code())
      << status.error_message() << ", " << status.error_details() << ", "
      << context.debug_error_string();
}

TEST_P(FaultInjectionTest, XdsFaultInjectionAlwaysDelayPercentageAbort) {
  CreateAndStartBackends(1);
  const auto kRpcTimeout = grpc_core::Duration::Seconds(30);
  const auto kFixedDelay = grpc_core::Duration::Seconds(1);
  const uint32_t kAbortPercentagePerHundred = 50;
  const double kAbortRate = kAbortPercentagePerHundred / 100.0;
  const double kErrorTolerance = 0.1;
  const size_t kNumRpcs = ComputeIdealNumRpcs(kAbortRate, kErrorTolerance);
  const size_t kMaxConcurrentRequests = kNumRpcs;
  // Create an EDS resource
  EdsResourceArgs args({{"locality0", CreateEndpointsForBackends()}});
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
  // Loosen the max concurrent request limit
  Cluster cluster = default_cluster_;
  auto* threshold = cluster.mutable_circuit_breakers()->add_thresholds();
  threshold->set_priority(RoutingPriority::DEFAULT);
  threshold->mutable_max_requests()->set_value(kMaxConcurrentRequests);
  balancer_->ads_service()->SetCdsResource(cluster);
  // Construct the fault injection filter config
  HTTPFault http_fault;
  auto* abort_percentage = http_fault.mutable_abort()->mutable_percentage();
  abort_percentage->set_numerator(kAbortPercentagePerHundred);
  abort_percentage->set_denominator(FractionalPercent::HUNDRED);
  http_fault.mutable_abort()->set_grpc_status(
      static_cast<uint32_t>(StatusCode::ABORTED));
  auto* delay_percentage = http_fault.mutable_delay()->mutable_percentage();
  delay_percentage->set_numerator(1000000);  // Always inject DELAY!
  delay_percentage->set_denominator(FractionalPercent::MILLION);
  SetProtoDuration(kFixedDelay,
                   http_fault.mutable_delay()->mutable_fixed_delay());
  // Config fault injection via different setup
  SetFilterConfig(http_fault);
  // Make sure channel is connected.  This avoids flakiness caused by
  // having multiple queued RPCs proceed in parallel when the name
  // resolution response is returned to the channel.
  channel_->WaitForConnected(grpc_timeout_milliseconds_to_deadline(15000));
  // Send kNumRpcs RPCs and count the aborts.
  int num_aborted = 0;
  RpcOptions rpc_options = RpcOptions().set_timeout(kRpcTimeout);
  std::vector<ConcurrentRpc> rpcs =
      SendConcurrentRpcs(DEBUG_LOCATION, stub_.get(), kNumRpcs, rpc_options);
  for (auto& rpc : rpcs) {
    EXPECT_GE(rpc.elapsed_time, kFixedDelay * grpc_test_slowdown_factor());
    if (rpc.status.error_code() == StatusCode::OK) continue;
    EXPECT_EQ("Fault injected", rpc.status.error_message());
    ++num_aborted;
  }
  // The abort rate should be roughly equal to the expectation.
  const double seen_abort_rate = static_cast<double>(num_aborted) / kNumRpcs;
  EXPECT_THAT(seen_abort_rate,
              ::testing::DoubleNear(kAbortRate, kErrorTolerance));
}

// This test and the above test apply different denominators to delay and
// abort. This ensures that we are using the right denominator for each
// injected fault in our code.
TEST_P(FaultInjectionTest,
       XdsFaultInjectionAlwaysDelayPercentageAbortSwitchDenominator) {
  CreateAndStartBackends(1);
  const auto kRpcTimeout = grpc_core::Duration::Seconds(30);
  const auto kFixedDelay = grpc_core::Duration::Seconds(1);
  const uint32_t kAbortPercentagePerMillion = 500000;
  const double kAbortRate = kAbortPercentagePerMillion / 1000000.0;
  const double kErrorTolerance = 0.1;
  const size_t kNumRpcs = ComputeIdealNumRpcs(kAbortRate, kErrorTolerance);
  const size_t kMaxConcurrentRequests = kNumRpcs;
  // Create an EDS resource
  EdsResourceArgs args({{"locality0", CreateEndpointsForBackends()}});
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
  // Loosen the max concurrent request limit
  Cluster cluster = default_cluster_;
  auto* threshold = cluster.mutable_circuit_breakers()->add_thresholds();
  threshold->set_priority(RoutingPriority::DEFAULT);
  threshold->mutable_max_requests()->set_value(kMaxConcurrentRequests);
  balancer_->ads_service()->SetCdsResource(cluster);
  // Construct the fault injection filter config
  HTTPFault http_fault;
  auto* abort_percentage = http_fault.mutable_abort()->mutable_percentage();
  abort_percentage->set_numerator(kAbortPercentagePerMillion);
  abort_percentage->set_denominator(FractionalPercent::MILLION);
  http_fault.mutable_abort()->set_grpc_status(
      static_cast<uint32_t>(StatusCode::ABORTED));
  auto* delay_percentage = http_fault.mutable_delay()->mutable_percentage();
  delay_percentage->set_numerator(100);  // Always inject DELAY!
  delay_percentage->set_denominator(FractionalPercent::HUNDRED);
  SetProtoDuration(kFixedDelay,
                   http_fault.mutable_delay()->mutable_fixed_delay());
  // Config fault injection via different setup
  SetFilterConfig(http_fault);
  // Make sure channel is connected.  This avoids flakiness caused by
  // having multiple queued RPCs proceed in parallel when the name
  // resolution response is returned to the channel.
  channel_->WaitForConnected(grpc_timeout_milliseconds_to_deadline(15000));
  // Send kNumRpcs RPCs and count the aborts.
  int num_aborted = 0;
  RpcOptions rpc_options = RpcOptions().set_timeout(kRpcTimeout);
  std::vector<ConcurrentRpc> rpcs =
      SendConcurrentRpcs(DEBUG_LOCATION, stub_.get(), kNumRpcs, rpc_options);
  for (auto& rpc : rpcs) {
    EXPECT_GE(rpc.elapsed_time, kFixedDelay * grpc_test_slowdown_factor());
    if (rpc.status.error_code() == StatusCode::OK) continue;
    EXPECT_EQ("Fault injected", rpc.status.error_message());
    ++num_aborted;
  }
  // The abort rate should be roughly equal to the expectation.
  const double seen_abort_rate = static_cast<double>(num_aborted) / kNumRpcs;
  EXPECT_THAT(seen_abort_rate,
              ::testing::DoubleNear(kAbortRate, kErrorTolerance));
}

TEST_P(FaultInjectionTest, XdsFaultInjectionMaxFault) {
  CreateAndStartBackends(1);
  const auto kRpcTimeout = grpc_core::Duration::Seconds(4);
  const auto kFixedDelay = grpc_core::Duration::Seconds(20);
  const uint32_t kMaxFault = 10;
  const uint32_t kNumRpcs = 30;  // kNumRpcs should be bigger than kMaxFault
  const uint32_t kAlwaysDelayPercentage = 100;
  // Create an EDS resource
  EdsResourceArgs args({{"locality0", CreateEndpointsForBackends()}});
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
  // Construct the fault injection filter config
  HTTPFault http_fault;
  auto* delay_percentage = http_fault.mutable_delay()->mutable_percentage();
  delay_percentage->set_numerator(
      kAlwaysDelayPercentage);  // Always inject DELAY!
  delay_percentage->set_denominator(FractionalPercent::HUNDRED);
  SetProtoDuration(kFixedDelay,
                   http_fault.mutable_delay()->mutable_fixed_delay());
  http_fault.mutable_max_active_faults()->set_value(kMaxFault);
  // Config fault injection via different setup
  SetFilterConfig(http_fault);
  // Make sure channel is connected.  This avoids flakiness caused by
  // having multiple queued RPCs proceed in parallel when the name
  // resolution response is returned to the channel.
  channel_->WaitForConnected(grpc_timeout_milliseconds_to_deadline(15000));
  // Sends a batch of long running RPCs with long timeout to consume all
  // active faults quota.
  int num_delayed = 0;
  RpcOptions rpc_options = RpcOptions().set_timeout(kRpcTimeout);
  std::vector<ConcurrentRpc> rpcs =
      SendConcurrentRpcs(DEBUG_LOCATION, stub_.get(), kNumRpcs, rpc_options);
  for (auto& rpc : rpcs) {
    if (rpc.status.error_code() == StatusCode::OK) continue;
    EXPECT_EQ(StatusCode::DEADLINE_EXCEEDED, rpc.status.error_code());
    ++num_delayed;
  }
  // Only kMaxFault number of RPC should be fault injected.
  EXPECT_EQ(kMaxFault, num_delayed);
  // Conduct one more round of RPCs after previous calls are finished. The goal
  // is to validate if the max fault counter is restored to zero.
  num_delayed = 0;
  rpcs = SendConcurrentRpcs(DEBUG_LOCATION, stub_.get(), kNumRpcs, rpc_options);
  for (auto& rpc : rpcs) {
    if (rpc.status.error_code() == StatusCode::OK) continue;
    EXPECT_EQ(StatusCode::DEADLINE_EXCEEDED, rpc.status.error_code());
    ++num_delayed;
  }
  // Only kMaxFault number of RPC should be fault injected. If the max fault
  // isn't restored to zero, none of the new RPCs will be fault injected.
  EXPECT_EQ(kMaxFault, num_delayed);
}

TEST_P(FaultInjectionTest, XdsFaultInjectionBidiStreamDelayOk) {
  CreateAndStartBackends(1);
  const auto kRpcTimeout = grpc_core::Duration::Seconds(20);
  const auto kFixedDelay = grpc_core::Duration::Seconds(1);
  const uint32_t kDelayPercentagePerHundred = 100;
  // Create an EDS resource
  EdsResourceArgs args({{"locality0", CreateEndpointsForBackends()}});
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
  // Construct the fault injection filter config
  HTTPFault http_fault;
  auto* delay_percentage = http_fault.mutable_delay()->mutable_percentage();
  delay_percentage->set_numerator(kDelayPercentagePerHundred);
  delay_percentage->set_denominator(FractionalPercent::HUNDRED);
  SetProtoDuration(kFixedDelay,
                   http_fault.mutable_delay()->mutable_fixed_delay());
  // Config fault injection via different setup
  SetFilterConfig(http_fault);
  ClientContext context;
  context.set_deadline(
      grpc_timeout_milliseconds_to_deadline(kRpcTimeout.millis()));
  auto stream = stub_->BidiStream(&context);
  stream->WritesDone();
  auto status = stream->Finish();
  EXPECT_TRUE(status.ok()) << status.error_message() << ", "
                           << status.error_details() << ", "
                           << context.debug_error_string();
}

// This case catches a bug in the retry code that was triggered by a bad
// interaction with the FI code.  See https://github.com/grpc/grpc/pull/27217
// for description.
TEST_P(FaultInjectionTest, XdsFaultInjectionBidiStreamDelayError) {
  CreateAndStartBackends(1);
  const auto kRpcTimeout = grpc_core::Duration::Seconds(10);
  const auto kFixedDelay = grpc_core::Duration::Seconds(30);
  const uint32_t kDelayPercentagePerHundred = 100;
  // Create an EDS resource
  EdsResourceArgs args({{"locality0", CreateEndpointsForBackends()}});
  balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
  // Construct the fault injection filter config
  HTTPFault http_fault;
  auto* delay_percentage = http_fault.mutable_delay()->mutable_percentage();
  delay_percentage->set_numerator(kDelayPercentagePerHundred);
  delay_percentage->set_denominator(FractionalPercent::HUNDRED);
  SetProtoDuration(kFixedDelay,
                   http_fault.mutable_delay()->mutable_fixed_delay());
  // Config fault injection via different setup
  SetFilterConfig(http_fault);
  ClientContext context;
  context.set_deadline(
      grpc_timeout_milliseconds_to_deadline(kRpcTimeout.millis()));
  auto stream = stub_->BidiStream(&context);
  stream->WritesDone();
  auto status = stream->Finish();
  EXPECT_EQ(StatusCode::DEADLINE_EXCEEDED, status.error_code())
      << status.error_message() << ", " << status.error_details() << ", "
      << context.debug_error_string();
}
#endif

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
#if TARGET_OS_IPHONE
  // Workaround Apple CFStream bug
  grpc_core::SetEnv("grpc_cfstream", "0");
#endif
  grpc_init();
  const auto result = RUN_ALL_TESTS();
  grpc_shutdown();
  return result;
}
