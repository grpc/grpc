//
// Copyright 2017 gRPC authors.
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

#include <grpc/grpc.h>
#include <grpc/grpc_security.h>
#include <grpcpp/channel.h>
#include <grpcpp/client_context.h>
#include <grpcpp/create_channel.h>
#include <grpcpp/server.h>
#include <grpcpp/server_builder.h>
#include <grpcpp/xds_server_builder.h>

#include <memory>
#include <optional>

#include "absl/strings/str_cat.h"
#include "absl/time/time.h"
#include "envoy/config/listener/v3/listener.pb.h"
#include "envoy/config/route/v3/route.pb.h"
#include "envoy/extensions/filters/network/http_connection_manager/v3/http_connection_manager.pb.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "src/core/config/config_vars.h"
#include "src/core/util/env.h"
#include "src/core/util/time.h"
#include "src/proto/grpc/testing/echo.pb.h"
#include "test/core/test_util/port.h"
#include "test/core/test_util/resolve_localhost_ip46.h"
#include "test/core/test_util/scoped_env_var.h"
#include "test/core/test_util/test_config.h"
#include "test/cpp/end2end/xds/xds_end2end_test_lib.h"

namespace grpc {
namespace testing {
namespace {

using ::envoy::config::listener::v3::FilterChainMatch;

//
// Basic xDS-enabled server tests
//

class XdsEnabledServerTest : public XdsEnd2endTest {
 protected:
  void SetUp() override {}  // No-op -- individual tests do this themselves.

  void DoSetUp(
      const std::optional<XdsBootstrapBuilder>& builder = std::nullopt) {
    // We use insecure creds here as a convenience to be able to easily
    // create new channels in some of the tests below.  None of the
    // tests here actually depend on the channel creds anyway.
    InitClient(builder, /*lb_expected_authority=*/"",
               // Using a low timeout to quickly end negative tests.
               // Prefer using WaitOnServingStatusChange() or a similar
               // loop on the client side to wait on status changes
               // instead of increasing this timeout.
               /*xds_resource_does_not_exist_timeout_ms=*/500,
               /*balancer_authority_override=*/"", /*args=*/nullptr,
               InsecureChannelCredentials());
    CreateBackends(1, /*xds_enabled=*/true, InsecureServerCredentials());
    EdsResourceArgs args({{"locality0", CreateEndpointsForBackends(0, 1)}});
    balancer_->ads_service()->SetEdsResource(BuildEdsResource(args));
  }
};

// We are only testing the server here.
// Run with bootstrap from env var, so that we use a global XdsClient
// instance.  Otherwise, we would need to use a separate fake resolver
// result generator on the client and server sides.
INSTANTIATE_TEST_SUITE_P(XdsTest, XdsEnabledServerTest,
                         ::testing::Values(XdsTestType().set_bootstrap_source(
                             XdsTestType::kBootstrapFromEnvVar)),
                         &XdsTestType::Name);

TEST_P(XdsEnabledServerTest, Basic) {
  DoSetUp();
  StartBackend(0);
  ASSERT_TRUE(backends_[0]->WaitOnServingStatusChange(grpc::StatusCode::OK));
  WaitForBackend(DEBUG_LOCATION, 0);
}

TEST_P(XdsEnabledServerTest, ListenerDeletionFailsByDefault) {
  DoSetUp();
  StartBackend(0);
  ASSERT_TRUE(backends_[0]->WaitOnServingStatusChange(grpc::StatusCode::OK));
  WaitForBackend(DEBUG_LOCATION, 0);
  // Check that we ACKed.
  // TODO(roth): There may be multiple entries in the resource state response
  // queue, because the client doesn't necessarily subscribe to all resources
  // in a single message, and the server currently (I suspect incorrectly?)
  // thinks that each subscription message is an ACK.  So for now, we
  // drain the entire LDS resource state response queue, ensuring that
  // all responses are ACKs.  Need to look more closely at the protocol
  // semantics here and make sure the server is doing the right thing,
  // in which case we may be able to avoid this.
  while (true) {
    auto response_state = balancer_->ads_service()->lds_response_state();
    if (!response_state.has_value()) break;
    ASSERT_TRUE(response_state.has_value());
    EXPECT_EQ(response_state->state, AdsServiceImpl::ResponseState::ACKED);
  }
  // Now unset the resource.
  balancer_->ads_service()->UnsetResource(
      kLdsTypeUrl, GetServerListenerName(backends_[0]->port()));
  // Server should stop serving.
  ASSERT_TRUE(
      backends_[0]->WaitOnServingStatusChange(grpc::StatusCode::NOT_FOUND));
}

TEST_P(XdsEnabledServerTest, ListenerDeletionIgnoredIfConfigured) {
  DoSetUp(MakeBootstrapBuilder().SetIgnoreResourceDeletion());
  StartBackend(0);
  ASSERT_TRUE(backends_[0]->WaitOnServingStatusChange(grpc::StatusCode::OK));
  WaitForBackend(DEBUG_LOCATION, 0);
  // Check that we ACKed.
  // TODO(roth): There may be multiple entries in the resource state response
  // queue, because the client doesn't necessarily subscribe to all resources
  // in a single message, and the server currently (I suspect incorrectly?)
  // thinks that each subscription message is an ACK.  So for now, we
  // drain the entire LDS resource state response queue, ensuring that
  // all responses are ACKs.  Need to look more closely at the protocol
  // semantics here and make sure the server is doing the right thing,
  // in which case we may be able to avoid this.
  while (true) {
    auto response_state = balancer_->ads_service()->lds_response_state();
    if (!response_state.has_value()) break;
    ASSERT_TRUE(response_state.has_value());
    EXPECT_EQ(response_state->state, AdsServiceImpl::ResponseState::ACKED);
  }
  // Now unset the resource.
  balancer_->ads_service()->UnsetResource(
      kLdsTypeUrl, GetServerListenerName(backends_[0]->port()));
  // Wait for update to be ACKed.
  absl::Time deadline =
      absl::Now() + (absl::Seconds(10) * grpc_test_slowdown_factor());
  while (true) {
    auto response_state = balancer_->ads_service()->lds_response_state();
    if (!response_state.has_value()) {
      gpr_sleep_until(grpc_timeout_seconds_to_deadline(1));
      continue;
    }
    EXPECT_EQ(response_state->state, AdsServiceImpl::ResponseState::ACKED);
    ASSERT_LT(absl::Now(), deadline);
    break;
  }
  // Make sure server is still serving.
  CheckRpcSendOk(DEBUG_LOCATION);
}

TEST_P(XdsEnabledServerTest,
       ListenerDeletionFailsWithFailOnDataErrorsIfEnabled) {
  grpc_core::testing::ScopedExperimentalEnvVar env(
      "GRPC_EXPERIMENTAL_XDS_DATA_ERROR_HANDLING");
  DoSetUp(MakeBootstrapBuilder().SetFailOnDataErrors());
  StartBackend(0);
  ASSERT_TRUE(backends_[0]->WaitOnServingStatusChange(grpc::StatusCode::OK));
  WaitForBackend(DEBUG_LOCATION, 0);
  // Check that we ACKed.
  // TODO(roth): There may be multiple entries in the resource state response
  // queue, because the client doesn't necessarily subscribe to all resources
  // in a single message, and the server currently (I suspect incorrectly?)
  // thinks that each subscription message is an ACK.  So for now, we
  // drain the entire LDS resource state response queue, ensuring that
  // all responses are ACKs.  Need to look more closely at the protocol
  // semantics here and make sure the server is doing the right thing,
  // in which case we may be able to avoid this.
  while (true) {
    auto response_state = balancer_->ads_service()->lds_response_state();
    if (!response_state.has_value()) break;
    ASSERT_TRUE(response_state.has_value());
    EXPECT_EQ(response_state->state, AdsServiceImpl::ResponseState::ACKED);
  }
  // Now unset the resource.
  balancer_->ads_service()->UnsetResource(
      kLdsTypeUrl, GetServerListenerName(backends_[0]->port()));
  // Server should stop serving.
  ASSERT_TRUE(
      backends_[0]->WaitOnServingStatusChange(grpc::StatusCode::NOT_FOUND));
}

TEST_P(XdsEnabledServerTest,
       ListenerDeletionIgnoredByDefaultIfDataErrorHandlingEnabled) {
  grpc_core::testing::ScopedExperimentalEnvVar env(
      "GRPC_EXPERIMENTAL_XDS_DATA_ERROR_HANDLING");
  DoSetUp();
  StartBackend(0);
  ASSERT_TRUE(backends_[0]->WaitOnServingStatusChange(grpc::StatusCode::OK));
  WaitForBackend(DEBUG_LOCATION, 0);
  // Check that we ACKed.
  // TODO(roth): There may be multiple entries in the resource state response
  // queue, because the client doesn't necessarily subscribe to all resources
  // in a single message, and the server currently (I suspect incorrectly?)
  // thinks that each subscription message is an ACK.  So for now, we
  // drain the entire LDS resource state response queue, ensuring that
  // all responses are ACKs.  Need to look more closely at the protocol
  // semantics here and make sure the server is doing the right thing,
  // in which case we may be able to avoid this.
  while (true) {
    auto response_state = balancer_->ads_service()->lds_response_state();
    if (!response_state.has_value()) break;
    ASSERT_TRUE(response_state.has_value());
    EXPECT_EQ(response_state->state, AdsServiceImpl::ResponseState::ACKED);
  }
  // Now unset the resource.
  balancer_->ads_service()->UnsetResource(
      kLdsTypeUrl, GetServerListenerName(backends_[0]->port()));
  // Wait for update to be ACKed.
  absl::Time deadline =
      absl::Now() + (absl::Seconds(10) * grpc_test_slowdown_factor());
  while (true) {
    auto response_state = balancer_->ads_service()->lds_response_state();
    if (!response_state.has_value()) {
      gpr_sleep_until(grpc_timeout_seconds_to_deadline(1));
      continue;
    }
    EXPECT_EQ(response_state->state, AdsServiceImpl::ResponseState::ACKED);
    ASSERT_LT(absl::Now(), deadline);
    break;
  }
  // Make sure server is still serving.
  CheckRpcSendOk(DEBUG_LOCATION);
}

// Testing just one example of an invalid resource here.
// Unit tests for XdsListenerResourceType have exhaustive tests for all
// of the invalid cases.
TEST_P(XdsEnabledServerTest, BadLdsUpdateNoApiListenerNorAddress) {
  DoSetUp();
  Listener listener = default_server_listener_;
  listener.clear_address();
  listener.set_name(GetServerListenerName(backends_[0]->port()));
  balancer_->ads_service()->SetLdsResource(listener);
  StartBackend(0);
  const auto response_state = WaitForLdsNack(DEBUG_LOCATION);
  ASSERT_TRUE(response_state.has_value()) << "timed out waiting for NACK";
  EXPECT_THAT(response_state->error_message,
              ::testing::EndsWith(absl::StrCat(
                  GetServerListenerName(backends_[0]->port()),
                  ": INVALID_ARGUMENT: Listener has neither address nor "
                  "ApiListener]")));
}

// Verify that a non-TCP listener results in "not serving" status.
TEST_P(XdsEnabledServerTest, NonTcpListener) {
  DoSetUp();
  Listener listener = default_listener_;  // Client-side listener.
  listener = PopulateServerListenerNameAndPort(listener, backends_[0]->port());
  auto hcm = ClientHcmAccessor().Unpack(listener);
  auto* rds = hcm.mutable_rds();
  rds->set_route_config_name(kDefaultRouteConfigurationName);
  rds->mutable_config_source()->mutable_self();
  ClientHcmAccessor().Pack(hcm, &listener);
  balancer_->ads_service()->SetLdsResource(listener);
  StartBackend(0);
  ASSERT_TRUE(backends_[0]->WaitOnServingStatusChange(
      grpc::StatusCode::FAILED_PRECONDITION));
}

// Verify that a mismatch of listening address results in "not serving"
// status.
TEST_P(XdsEnabledServerTest, ListenerAddressMismatch) {
  DoSetUp();
  Listener listener = default_server_listener_;
  // Set a different listening address in the LDS update
  listener.mutable_address()->mutable_socket_address()->set_address(
      "192.168.1.1");
  SetServerListenerNameAndRouteConfiguration(balancer_.get(), listener,
                                             backends_[0]->port(),
                                             default_server_route_config_);
  StartBackend(0);
  ASSERT_TRUE(backends_[0]->WaitOnServingStatusChange(
      grpc::StatusCode::FAILED_PRECONDITION));
}

//
// server status notification tests
//

class XdsEnabledServerStatusNotificationTest : public XdsEnabledServerTest {
 protected:
  void SetValidLdsUpdate() {
    SetServerListenerNameAndRouteConfiguration(
        balancer_.get(), default_server_listener_, backends_[0]->port(),
        default_server_route_config_);
  }

  void SetInvalidLdsUpdate() {
    Listener listener = default_server_listener_;
    listener.clear_address();
    listener.set_name(GetServerListenerName(backends_[0]->port()));
    balancer_->ads_service()->SetLdsResource(listener);
  }

  void UnsetLdsUpdate() {
    balancer_->ads_service()->UnsetResource(
        kLdsTypeUrl, GetServerListenerName(backends_[0]->port()));
  }
};

INSTANTIATE_TEST_SUITE_P(XdsTest, XdsEnabledServerStatusNotificationTest,
                         ::testing::Values(XdsTestType()), &XdsTestType::Name);

TEST_P(XdsEnabledServerStatusNotificationTest, ServingStatus) {
  DoSetUp();
  StartBackend(0);
  ASSERT_TRUE(backends_[0]->WaitOnServingStatusChange(grpc::StatusCode::OK));
  CheckRpcSendOk(DEBUG_LOCATION, 1, RpcOptions().set_wait_for_ready(true));
}

TEST_P(XdsEnabledServerStatusNotificationTest, NotServingStatus) {
  DoSetUp();
  SetInvalidLdsUpdate();
  StartBackend(0);
  ASSERT_TRUE(backends_[0]->WaitOnServingStatusChange(
      grpc::StatusCode::INVALID_ARGUMENT));
  CheckRpcSendFailure(DEBUG_LOCATION, StatusCode::UNAVAILABLE,
                      MakeConnectionFailureRegex(
                          "connections to all backends failing; last error: "));
}

TEST_P(XdsEnabledServerStatusNotificationTest, ErrorUpdateWhenAlreadyServing) {
  DoSetUp();
  StartBackend(0);
  ASSERT_TRUE(backends_[0]->WaitOnServingStatusChange(grpc::StatusCode::OK));
  CheckRpcSendOk(DEBUG_LOCATION, 1, RpcOptions().set_wait_for_ready(true));
  // Invalid update does not lead to a change in the serving status.
  SetInvalidLdsUpdate();
  auto response_state =
      WaitForLdsNack(DEBUG_LOCATION, RpcOptions(), StatusCode::OK);
  ASSERT_TRUE(response_state.has_value()) << "timed out waiting for NACK";
  EXPECT_THAT(response_state->error_message,
              ::testing::EndsWith(absl::StrCat(
                  GetServerListenerName(backends_[0]->port()),
                  ": INVALID_ARGUMENT: Listener has neither address nor "
                  "ApiListener]")));
  ASSERT_TRUE(backends_[0]->WaitOnServingStatusChange(grpc::StatusCode::OK));
  CheckRpcSendOk(DEBUG_LOCATION);
}

TEST_P(XdsEnabledServerStatusNotificationTest,
       NotServingStatusToServingStatusTransition) {
  DoSetUp();
  SetInvalidLdsUpdate();
  StartBackend(0);
  ASSERT_TRUE(backends_[0]->WaitOnServingStatusChange(
      grpc::StatusCode::INVALID_ARGUMENT));
  CheckRpcSendFailure(DEBUG_LOCATION, StatusCode::UNAVAILABLE,
                      MakeConnectionFailureRegex(
                          "connections to all backends failing; last error: "));
  // Send a valid LDS update to change to serving status
  SetValidLdsUpdate();
  ASSERT_TRUE(backends_[0]->WaitOnServingStatusChange(grpc::StatusCode::OK));
  CheckRpcSendOk(DEBUG_LOCATION, 1, RpcOptions().set_wait_for_ready(true));
}

// This test verifies that the resource getting deleted when already serving
// results in future connections being dropped.
TEST_P(XdsEnabledServerStatusNotificationTest,
       ServingStatusToNonServingStatusTransition) {
  DoSetUp(MakeBootstrapBuilder().SetFailOnDataErrors());
  SetValidLdsUpdate();
  StartBackend(0);
  ASSERT_TRUE(backends_[0]->WaitOnServingStatusChange(grpc::StatusCode::OK));
  CheckRpcSendOk(DEBUG_LOCATION, 1, RpcOptions().set_wait_for_ready(true));
  // Deleting the resource should result in a non-serving status.
  UnsetLdsUpdate();
  ASSERT_TRUE(
      backends_[0]->WaitOnServingStatusChange(grpc::StatusCode::NOT_FOUND));
  SendRpcsUntilFailure(
      DEBUG_LOCATION, StatusCode::UNAVAILABLE,
      MakeConnectionFailureRegex(
          "connections to all backends failing; last error: "));
}

TEST_P(XdsEnabledServerStatusNotificationTest, RepeatedServingStatusChanges) {
  DoSetUp(MakeBootstrapBuilder().SetFailOnDataErrors());
  StartBackend(0);
  for (int i = 0; i < 5; ++i) {
    // Send a valid LDS update to get the server to start listening
    SetValidLdsUpdate();
    ASSERT_TRUE(backends_[0]->WaitOnServingStatusChange(grpc::StatusCode::OK));
    CheckRpcSendOk(DEBUG_LOCATION, 1, RpcOptions().set_wait_for_ready(true));
    // Deleting the resource will make the server start rejecting connections
    UnsetLdsUpdate();
    ASSERT_TRUE(
        backends_[0]->WaitOnServingStatusChange(grpc::StatusCode::NOT_FOUND));
    SendRpcsUntilFailure(
        DEBUG_LOCATION, StatusCode::UNAVAILABLE,
        MakeConnectionFailureRegex(
            "connections to all backends failing; last error: "));
  }
}

TEST_P(XdsEnabledServerStatusNotificationTest, ExistingRpcsOnResourceDeletion) {
  DoSetUp(MakeBootstrapBuilder().SetFailOnDataErrors());
  StartBackend(0);
  ASSERT_TRUE(backends_[0]->WaitOnServingStatusChange(grpc::StatusCode::OK));
  constexpr int kNumChannels = 10;
  struct StreamingRpc {
    std::shared_ptr<Channel> channel;
    std::unique_ptr<grpc::testing::EchoTestService::Stub> stub;
    ClientContext context;
    std::unique_ptr<ClientReaderWriter<EchoRequest, EchoResponse>> stream;
  } streaming_rpcs[kNumChannels];
  EchoRequest request;
  EchoResponse response;
  request.set_message("Hello");
  ChannelArguments args;
  args.SetInt(GRPC_ARG_USE_LOCAL_SUBCHANNEL_POOL, 1);
  for (int i = 0; i < kNumChannels; i++) {
    streaming_rpcs[i].channel =
        CreateCustomChannel(grpc_core::LocalIpUri(backends_[0]->port()),
                            InsecureChannelCredentials(), args);
    streaming_rpcs[i].stub =
        grpc::testing::EchoTestService::NewStub(streaming_rpcs[i].channel);
    streaming_rpcs[i].context.set_wait_for_ready(true);
    streaming_rpcs[i].stream =
        streaming_rpcs[i].stub->BidiStream(&streaming_rpcs[i].context);
    EXPECT_TRUE(streaming_rpcs[i].stream->Write(request));
    streaming_rpcs[i].stream->Read(&response);
    EXPECT_EQ(request.message(), response.message());
  }
  // Deleting the resource will make the server start rejecting connections
  UnsetLdsUpdate();
  ASSERT_TRUE(
      backends_[0]->WaitOnServingStatusChange(grpc::StatusCode::NOT_FOUND));
  SendRpcsUntilFailure(
      DEBUG_LOCATION, StatusCode::UNAVAILABLE,
      MakeConnectionFailureRegex(
          "connections to all backends failing; last error: "));
  for (int i = 0; i < kNumChannels; i++) {
    EXPECT_TRUE(streaming_rpcs[i].stream->Write(request));
    streaming_rpcs[i].stream->Read(&response);
    EXPECT_EQ(request.message(), response.message());
    EXPECT_TRUE(streaming_rpcs[i].stream->WritesDone());
    auto status = streaming_rpcs[i].stream->Finish();
    EXPECT_TRUE(status.ok())
        << status.error_message() << ", " << status.error_details() << ", "
        << streaming_rpcs[i].context.debug_error_string();
    // New RPCs on the existing channels should fail.
    ClientContext new_context;
    new_context.set_deadline(grpc_timeout_milliseconds_to_deadline(1000));
    EXPECT_FALSE(
        streaming_rpcs[i].stub->Echo(&new_context, request, &response).ok());
  }
}

TEST_P(XdsEnabledServerStatusNotificationTest,
       ExistingRpcsFailOnResourceUpdateAfterDrainGraceTimeExpires) {
  DoSetUp();
  constexpr int kDrainGraceTimeMs = 100;
  xds_drain_grace_time_ms_ = kDrainGraceTimeMs;
  StartBackend(0);
  ASSERT_TRUE(backends_[0]->WaitOnServingStatusChange(grpc::StatusCode::OK));
  constexpr int kNumChannels = 10;
  struct StreamingRpc {
    std::shared_ptr<Channel> channel;
    std::unique_ptr<grpc::testing::EchoTestService::Stub> stub;
    ClientContext context;
    std::unique_ptr<ClientReaderWriter<EchoRequest, EchoResponse>> stream;
  } streaming_rpcs[kNumChannels];
  EchoRequest request;
  EchoResponse response;
  request.set_message("Hello");
  ChannelArguments args;
  args.SetInt(GRPC_ARG_USE_LOCAL_SUBCHANNEL_POOL, 1);
  for (int i = 0; i < kNumChannels; i++) {
    streaming_rpcs[i].channel =
        CreateCustomChannel(grpc_core::LocalIpUri(backends_[0]->port()),
                            InsecureChannelCredentials(), args);
    streaming_rpcs[i].stub =
        grpc::testing::EchoTestService::NewStub(streaming_rpcs[i].channel);
    streaming_rpcs[i].context.set_wait_for_ready(true);
    streaming_rpcs[i].stream =
        streaming_rpcs[i].stub->BidiStream(&streaming_rpcs[i].context);
    EXPECT_TRUE(streaming_rpcs[i].stream->Write(request));
    streaming_rpcs[i].stream->Read(&response);
    EXPECT_EQ(request.message(), response.message());
  }
  grpc_core::Timestamp update_time = NowFromCycleCounter();
  // Update the resource.  We modify the route with an invalid entry, so
  // that we can tell from the RPC failure messages when the server has
  // seen the change.
  auto route_config = default_server_route_config_;
  route_config.mutable_virtual_hosts(0)->mutable_routes(0)->mutable_redirect();
  SetServerListenerNameAndRouteConfiguration(
      balancer_.get(), default_server_listener_, backends_[0]->port(),
      route_config);
  SendRpcsUntilFailure(DEBUG_LOCATION, StatusCode::UNAVAILABLE,
                       "UNAVAILABLE:matching route has unsupported action");
  // After the drain grace time expires, the existing RPCs should all fail.
  for (int i = 0; i < kNumChannels; i++) {
    // Wait for the drain grace time to expire
    EXPECT_FALSE(streaming_rpcs[i].stream->Read(&response));
    // Make sure that the drain grace interval is honored.
    EXPECT_GE(NowFromCycleCounter() - update_time,
              grpc_core::Duration::Milliseconds(kDrainGraceTimeMs));
    auto status = streaming_rpcs[i].stream->Finish();
    EXPECT_EQ(status.error_code(), grpc::StatusCode::UNAVAILABLE)
        << status.error_code() << ", " << status.error_message() << ", "
        << status.error_details() << ", "
        << streaming_rpcs[i].context.debug_error_string();
    EXPECT_EQ(status.error_message(),
              "Drain grace time expired. Closing connection immediately.");
  }
}

//
// filter chain matching tests
//

class XdsServerFilterChainMatchTest : public XdsEnabledServerTest {
 public:
  void SetUp() override { DoSetUp(); }

  HttpConnectionManager GetHttpConnectionManager(const Listener& listener) {
    HttpConnectionManager http_connection_manager =
        ServerHcmAccessor().Unpack(listener);
    *http_connection_manager.mutable_route_config() =
        default_server_route_config_;
    return http_connection_manager;
  }
};

// Run with bootstrap from env var so that we use one XdsClient.
INSTANTIATE_TEST_SUITE_P(XdsTest, XdsServerFilterChainMatchTest,
                         ::testing::Values(XdsTestType().set_bootstrap_source(
                             XdsTestType::kBootstrapFromEnvVar)),
                         &XdsTestType::Name);

TEST_P(XdsServerFilterChainMatchTest,
       DefaultFilterChainUsedWhenNoFilterChainMentioned) {
  StartBackend(0);
  ASSERT_TRUE(backends_[0]->WaitOnServingStatusChange(grpc::StatusCode::OK));
  CheckRpcSendOk(DEBUG_LOCATION, 1, RpcOptions().set_wait_for_ready(true));
}

TEST_P(XdsServerFilterChainMatchTest,
       DefaultFilterChainUsedWhenOtherFilterChainsDontMatch) {
  Listener listener = default_server_listener_;
  // Add a filter chain that will never get matched
  auto* filter_chain = listener.add_filter_chains();
  filter_chain->add_filters()->mutable_typed_config()->PackFrom(
      GetHttpConnectionManager(listener));
  filter_chain->mutable_filter_chain_match()
      ->mutable_destination_port()
      ->set_value(8080);
  SetServerListenerNameAndRouteConfiguration(balancer_.get(), listener,
                                             backends_[0]->port(),
                                             default_server_route_config_);
  StartBackend(0);
  ASSERT_TRUE(backends_[0]->WaitOnServingStatusChange(grpc::StatusCode::OK));
  CheckRpcSendOk(DEBUG_LOCATION, 1, RpcOptions().set_wait_for_ready(true));
}

TEST_P(XdsServerFilterChainMatchTest,
       FilterChainsWithDestinationPortDontMatch) {
  Listener listener = default_server_listener_;
  // Add filter chain with destination port that should never get matched
  auto* filter_chain = listener.add_filter_chains();
  filter_chain->add_filters()->mutable_typed_config()->PackFrom(
      GetHttpConnectionManager(listener));
  filter_chain->mutable_filter_chain_match()
      ->mutable_destination_port()
      ->set_value(8080);
  listener.clear_default_filter_chain();
  balancer_->ads_service()->SetLdsResource(
      PopulateServerListenerNameAndPort(listener, backends_[0]->port()));
  StartBackend(0);
  ASSERT_TRUE(backends_[0]->WaitOnServingStatusChange(grpc::StatusCode::OK));
  // RPC should fail since no matching filter chain was found and no default
  // filter chain is configured.
  CheckRpcSendFailure(DEBUG_LOCATION, StatusCode::UNAVAILABLE,
                      MakeConnectionFailureRegex(
                          "connections to all backends failing; last error: "));
}

TEST_P(XdsServerFilterChainMatchTest, FilterChainsWithServerNamesDontMatch) {
  Listener listener = default_server_listener_;
  // Add filter chain with server name that should never get matched
  auto* filter_chain = listener.add_filter_chains();
  filter_chain->add_filters()->mutable_typed_config()->PackFrom(
      GetHttpConnectionManager(listener));
  filter_chain->mutable_filter_chain_match()->add_server_names("server_name");
  listener.clear_default_filter_chain();
  balancer_->ads_service()->SetLdsResource(
      PopulateServerListenerNameAndPort(listener, backends_[0]->port()));
  StartBackend(0);
  ASSERT_TRUE(backends_[0]->WaitOnServingStatusChange(grpc::StatusCode::OK));
  // RPC should fail since no matching filter chain was found and no default
  // filter chain is configured.
  CheckRpcSendFailure(DEBUG_LOCATION, StatusCode::UNAVAILABLE,
                      MakeConnectionFailureRegex(
                          "connections to all backends failing; last error: "));
}

TEST_P(XdsServerFilterChainMatchTest,
       FilterChainsWithTransportProtocolsOtherThanRawBufferDontMatch) {
  Listener listener = default_server_listener_;
  // Add filter chain with transport protocol "tls" that should never match
  auto* filter_chain = listener.add_filter_chains();
  filter_chain->add_filters()->mutable_typed_config()->PackFrom(
      GetHttpConnectionManager(listener));
  filter_chain->mutable_filter_chain_match()->set_transport_protocol("tls");
  listener.clear_default_filter_chain();
  balancer_->ads_service()->SetLdsResource(
      PopulateServerListenerNameAndPort(listener, backends_[0]->port()));
  StartBackend(0);
  ASSERT_TRUE(backends_[0]->WaitOnServingStatusChange(grpc::StatusCode::OK));
  // RPC should fail since no matching filter chain was found and no default
  // filter chain is configured.
  CheckRpcSendFailure(DEBUG_LOCATION, StatusCode::UNAVAILABLE,
                      MakeConnectionFailureRegex(
                          "connections to all backends failing; last error: "));
}

TEST_P(XdsServerFilterChainMatchTest,
       FilterChainsWithApplicationProtocolsDontMatch) {
  Listener listener = default_server_listener_;
  // Add filter chain with application protocol that should never get matched
  auto* filter_chain = listener.add_filter_chains();
  filter_chain->add_filters()->mutable_typed_config()->PackFrom(
      GetHttpConnectionManager(listener));
  filter_chain->mutable_filter_chain_match()->add_application_protocols("h2");
  listener.clear_default_filter_chain();
  balancer_->ads_service()->SetLdsResource(
      PopulateServerListenerNameAndPort(listener, backends_[0]->port()));
  StartBackend(0);
  ASSERT_TRUE(backends_[0]->WaitOnServingStatusChange(grpc::StatusCode::OK));
  // RPC should fail since no matching filter chain was found and no default
  // filter chain is configured.
  CheckRpcSendFailure(DEBUG_LOCATION, StatusCode::UNAVAILABLE,
                      MakeConnectionFailureRegex(
                          "connections to all backends failing; last error: "));
}

TEST_P(XdsServerFilterChainMatchTest,
       FilterChainsWithTransportProtocolRawBufferIsPreferred) {
  Listener listener = default_server_listener_;
  // Add filter chain with "raw_buffer" transport protocol
  auto* filter_chain = listener.add_filter_chains();
  filter_chain->add_filters()->mutable_typed_config()->PackFrom(
      GetHttpConnectionManager(listener));
  filter_chain->mutable_filter_chain_match()->set_transport_protocol(
      "raw_buffer");
  // Add another filter chain with no transport protocol set but application
  // protocol set (fails match)
  filter_chain = listener.add_filter_chains();
  filter_chain->add_filters()->mutable_typed_config()->PackFrom(
      GetHttpConnectionManager(listener));
  filter_chain->mutable_filter_chain_match()->add_application_protocols("h2");
  listener.clear_default_filter_chain();
  balancer_->ads_service()->SetLdsResource(
      PopulateServerListenerNameAndPort(listener, backends_[0]->port()));
  StartBackend(0);
  ASSERT_TRUE(backends_[0]->WaitOnServingStatusChange(grpc::StatusCode::OK));
  // A successful RPC proves that filter chains that mention "raw_buffer" as
  // the transport protocol are chosen as the best match in the round.
  CheckRpcSendOk(DEBUG_LOCATION, 1, RpcOptions().set_wait_for_ready(true));
}

TEST_P(XdsServerFilterChainMatchTest,
       FilterChainsWithMoreSpecificDestinationPrefixRangesArePreferred) {
  Listener listener = default_server_listener_;
  // Add filter chain with prefix range (length 4 and 16) but with server name
  // mentioned. (Prefix range is matched first.)
  auto* filter_chain = listener.add_filter_chains();
  filter_chain->add_filters()->mutable_typed_config()->PackFrom(
      GetHttpConnectionManager(listener));
  auto* prefix_range =
      filter_chain->mutable_filter_chain_match()->add_prefix_ranges();
  prefix_range->set_address_prefix(grpc_core::LocalIp());
  prefix_range->mutable_prefix_len()->set_value(4);
  prefix_range =
      filter_chain->mutable_filter_chain_match()->add_prefix_ranges();
  prefix_range->set_address_prefix(grpc_core::LocalIp());
  prefix_range->mutable_prefix_len()->set_value(16);
  filter_chain->mutable_filter_chain_match()->add_server_names("server_name");
  // Add filter chain with two prefix ranges (length 8 and 24). Since 24 is
  // the highest match, it should be chosen.
  filter_chain = listener.add_filter_chains();
  filter_chain->add_filters()->mutable_typed_config()->PackFrom(
      GetHttpConnectionManager(listener));
  prefix_range =
      filter_chain->mutable_filter_chain_match()->add_prefix_ranges();
  prefix_range->set_address_prefix(grpc_core::LocalIp());
  prefix_range->mutable_prefix_len()->set_value(8);
  prefix_range =
      filter_chain->mutable_filter_chain_match()->add_prefix_ranges();
  prefix_range->set_address_prefix(grpc_core::LocalIp());
  prefix_range->mutable_prefix_len()->set_value(24);
  // Add another filter chain with a non-matching prefix range (with length
  // 30)
  filter_chain = listener.add_filter_chains();
  filter_chain->add_filters()->mutable_typed_config()->PackFrom(
      GetHttpConnectionManager(listener));
  prefix_range =
      filter_chain->mutable_filter_chain_match()->add_prefix_ranges();
  prefix_range->set_address_prefix("192.168.1.1");
  prefix_range->mutable_prefix_len()->set_value(30);
  filter_chain->mutable_filter_chain_match()->add_server_names("server_name");
  // Add another filter chain with no prefix range mentioned
  filter_chain = listener.add_filter_chains();
  filter_chain->add_filters()->mutable_typed_config()->PackFrom(
      GetHttpConnectionManager(listener));
  filter_chain->mutable_filter_chain_match()->add_server_names("server_name");
  listener.clear_default_filter_chain();
  balancer_->ads_service()->SetLdsResource(
      PopulateServerListenerNameAndPort(listener, backends_[0]->port()));
  StartBackend(0);
  ASSERT_TRUE(backends_[0]->WaitOnServingStatusChange(grpc::StatusCode::OK));
  // A successful RPC proves that the filter chain with the longest matching
  // prefix range was the best match.
  CheckRpcSendOk(DEBUG_LOCATION, 1, RpcOptions().set_wait_for_ready(true));
}

TEST_P(XdsServerFilterChainMatchTest,
       FilterChainsThatMentionSourceTypeArePreferred) {
  Listener listener = default_server_listener_;
  // Add filter chain with the local source type (best match)
  auto* filter_chain = listener.add_filter_chains();
  filter_chain->add_filters()->mutable_typed_config()->PackFrom(
      GetHttpConnectionManager(listener));
  filter_chain->mutable_filter_chain_match()->set_source_type(
      FilterChainMatch::SAME_IP_OR_LOOPBACK);
  // Add filter chain with the external source type but bad source port.
  // Note that backends_[0]->port() will never be a match for the source port
  // because it is already being used by a backend.
  filter_chain = listener.add_filter_chains();
  filter_chain->add_filters()->mutable_typed_config()->PackFrom(
      GetHttpConnectionManager(listener));
  filter_chain->mutable_filter_chain_match()->set_source_type(
      FilterChainMatch::EXTERNAL);
  filter_chain->mutable_filter_chain_match()->add_source_ports(
      backends_[0]->port());
  // Add filter chain with the default source type (ANY) but bad source port.
  filter_chain = listener.add_filter_chains();
  filter_chain->add_filters()->mutable_typed_config()->PackFrom(
      GetHttpConnectionManager(listener));
  filter_chain->mutable_filter_chain_match()->add_source_ports(
      backends_[0]->port());
  listener.clear_default_filter_chain();
  balancer_->ads_service()->SetLdsResource(
      PopulateServerListenerNameAndPort(listener, backends_[0]->port()));
  StartBackend(0);
  ASSERT_TRUE(backends_[0]->WaitOnServingStatusChange(grpc::StatusCode::OK));
  // A successful RPC proves that the filter chain with the longest matching
  // prefix range was the best match.
  CheckRpcSendOk(DEBUG_LOCATION, 1, RpcOptions().set_wait_for_ready(true));
}

TEST_P(XdsServerFilterChainMatchTest,
       FilterChainsWithMoreSpecificSourcePrefixRangesArePreferred) {
  Listener listener = default_server_listener_;
  // Add filter chain with source prefix range (length 16) but with a bad
  // source port mentioned. (Prefix range is matched first.) Note that
  // backends_[0]->port() will never be a match for the source port because it
  // is already being used by a backend.
  auto* filter_chain = listener.add_filter_chains();
  filter_chain->add_filters()->mutable_typed_config()->PackFrom(
      GetHttpConnectionManager(listener));
  auto* source_prefix_range =
      filter_chain->mutable_filter_chain_match()->add_source_prefix_ranges();
  source_prefix_range->set_address_prefix(grpc_core::LocalIp());
  source_prefix_range->mutable_prefix_len()->set_value(4);
  source_prefix_range =
      filter_chain->mutable_filter_chain_match()->add_source_prefix_ranges();
  source_prefix_range->set_address_prefix(grpc_core::LocalIp());
  source_prefix_range->mutable_prefix_len()->set_value(16);
  filter_chain->mutable_filter_chain_match()->add_source_ports(
      backends_[0]->port());
  // Add filter chain with two source prefix ranges (length 8 and 24). Since
  // 24 is the highest match, it should be chosen.
  filter_chain = listener.add_filter_chains();
  filter_chain->add_filters()->mutable_typed_config()->PackFrom(
      GetHttpConnectionManager(listener));
  source_prefix_range =
      filter_chain->mutable_filter_chain_match()->add_source_prefix_ranges();
  source_prefix_range->set_address_prefix(grpc_core::LocalIp());
  source_prefix_range->mutable_prefix_len()->set_value(8);
  source_prefix_range =
      filter_chain->mutable_filter_chain_match()->add_source_prefix_ranges();
  source_prefix_range->set_address_prefix(grpc_core::LocalIp());
  source_prefix_range->mutable_prefix_len()->set_value(24);
  // Add another filter chain with a non-matching source prefix range (with
  // length 30) and bad source port
  filter_chain = listener.add_filter_chains();
  filter_chain->add_filters()->mutable_typed_config()->PackFrom(
      GetHttpConnectionManager(listener));
  source_prefix_range =
      filter_chain->mutable_filter_chain_match()->add_source_prefix_ranges();
  source_prefix_range->set_address_prefix("192.168.1.1");
  source_prefix_range->mutable_prefix_len()->set_value(30);
  filter_chain->mutable_filter_chain_match()->add_source_ports(
      backends_[0]->port());
  // Add another filter chain with no source prefix range mentioned and bad
  // source port
  filter_chain = listener.add_filter_chains();
  filter_chain->add_filters()->mutable_typed_config()->PackFrom(
      GetHttpConnectionManager(listener));
  filter_chain->mutable_filter_chain_match()->add_source_ports(
      backends_[0]->port());
  listener.clear_default_filter_chain();
  balancer_->ads_service()->SetLdsResource(
      PopulateServerListenerNameAndPort(listener, backends_[0]->port()));
  StartBackend(0);
  ASSERT_TRUE(backends_[0]->WaitOnServingStatusChange(grpc::StatusCode::OK));
  // A successful RPC proves that the filter chain with the longest matching
  // source prefix range was the best match.
  CheckRpcSendOk(DEBUG_LOCATION, 1, RpcOptions().set_wait_for_ready(true));
}

TEST_P(XdsServerFilterChainMatchTest,
       FilterChainsWithMoreSpecificSourcePortArePreferred) {
  Listener listener = default_server_listener_;
  auto* filter_chain = listener.add_filter_chains();
  filter_chain->add_filters()->mutable_typed_config()->PackFrom(
      GetHttpConnectionManager(listener));
  // Since we don't know which port will be used by the channel, just add all
  // ports except for 0.
  for (int i = 1; i < 65536; i++) {
    filter_chain->mutable_filter_chain_match()->add_source_ports(i);
  }
  // Add another filter chain with no source port mentioned whose route
  // config has a route with an unsupported action.
  auto hcm = GetHttpConnectionManager(listener);
  hcm.mutable_route_config()
      ->mutable_virtual_hosts(0)
      ->mutable_routes(0)
      ->mutable_redirect();
  filter_chain = listener.add_filter_chains();
  filter_chain->add_filters()->mutable_typed_config()->PackFrom(hcm);
  listener.clear_default_filter_chain();
  balancer_->ads_service()->SetLdsResource(
      PopulateServerListenerNameAndPort(listener, backends_[0]->port()));
  StartBackend(0);
  ASSERT_TRUE(backends_[0]->WaitOnServingStatusChange(grpc::StatusCode::OK));
  // A successful RPC proves that the filter chain with matching source port
  // was chosen.
  CheckRpcSendOk(DEBUG_LOCATION, 1, RpcOptions().set_wait_for_ready(true));
}

//
// server-side RDS tests
//

class XdsServerRdsTest : public XdsEnabledServerTest {
 public:
  void SetUp() override { DoSetUp(); }
};

// Test both with and without RDS.
// Run with bootstrap from env var so that we use one XdsClient.
INSTANTIATE_TEST_SUITE_P(
    XdsTest, XdsServerRdsTest,
    ::testing::Values(
        XdsTestType().set_bootstrap_source(XdsTestType::kBootstrapFromEnvVar),
        XdsTestType()
            .set_bootstrap_source(XdsTestType::kBootstrapFromEnvVar)
            .set_enable_rds_testing()),
    &XdsTestType::Name);

TEST_P(XdsServerRdsTest, Basic) {
  StartBackend(0);
  ASSERT_TRUE(backends_[0]->WaitOnServingStatusChange(grpc::StatusCode::OK));
  CheckRpcSendOk(DEBUG_LOCATION, 1, RpcOptions().set_wait_for_ready(true));
}

TEST_P(XdsServerRdsTest, FailsRouteMatchesOtherThanNonForwardingAction) {
  SetServerListenerNameAndRouteConfiguration(
      balancer_.get(), default_server_listener_, backends_[0]->port(),
      default_route_config_ /* inappropriate route config for servers */);
  StartBackend(0);
  // The server should be ready to serve but RPCs should fail.
  ASSERT_TRUE(backends_[0]->WaitOnServingStatusChange(grpc::StatusCode::OK));
  CheckRpcSendFailure(DEBUG_LOCATION, StatusCode::UNAVAILABLE,
                      "UNAVAILABLE:matching route has unsupported action");
}

// Test that non-inline route configuration also works for non-default filter
// chains
TEST_P(XdsServerRdsTest, NonInlineRouteConfigurationNonDefaultFilterChain) {
  if (!GetParam().enable_rds_testing()) return;
  Listener listener = default_server_listener_;
  auto* filter_chain = listener.add_filter_chains();
  HttpConnectionManager http_connection_manager =
      ServerHcmAccessor().Unpack(listener);
  auto* rds = http_connection_manager.mutable_rds();
  rds->set_route_config_name(kDefaultServerRouteConfigurationName);
  rds->mutable_config_source()->mutable_self();
  filter_chain->add_filters()->mutable_typed_config()->PackFrom(
      http_connection_manager);
  SetServerListenerNameAndRouteConfiguration(balancer_.get(), listener,
                                             backends_[0]->port(),
                                             default_server_route_config_);
  StartBackend(0);
  ASSERT_TRUE(backends_[0]->WaitOnServingStatusChange(grpc::StatusCode::OK));
  CheckRpcSendOk(DEBUG_LOCATION, 1, RpcOptions().set_wait_for_ready(true));
}

TEST_P(XdsServerRdsTest, NonInlineRouteConfigurationNotAvailable) {
  if (!GetParam().enable_rds_testing()) return;
  Listener listener = default_server_listener_;
  HttpConnectionManager http_connection_manager =
      ServerHcmAccessor().Unpack(listener);
  auto* rds = http_connection_manager.mutable_rds();
  rds->set_route_config_name("unknown_server_route_config");
  rds->mutable_config_source()->mutable_self();
  listener.add_filter_chains()->add_filters()->mutable_typed_config()->PackFrom(
      http_connection_manager);
  SetServerListenerNameAndRouteConfiguration(balancer_.get(), listener,
                                             backends_[0]->port(),
                                             default_server_route_config_);
  StartBackend(0);
  ASSERT_TRUE(backends_[0]->WaitOnServingStatusChange(grpc::StatusCode::OK));
  CheckRpcSendFailure(DEBUG_LOCATION, StatusCode::UNAVAILABLE,
                      "RDS resource unknown_server_route_config: "
                      "does not exist \\(node ID:xds_end2end_test\\)");
}

// TODO(yashykt): Once https://github.com/grpc/grpc/issues/24035 is fixed, we
// should add tests that make sure that different route configs are used for
// incoming connections with a different match.
TEST_P(XdsServerRdsTest, MultipleRouteConfigurations) {
  Listener listener = default_server_listener_;
  // Set a filter chain with a new route config name
  auto new_route_config = default_server_route_config_;
  new_route_config.set_name("new_server_route_config");
  HttpConnectionManager http_connection_manager =
      ServerHcmAccessor().Unpack(listener);
  auto* rds = http_connection_manager.mutable_rds();
  rds->set_route_config_name(new_route_config.name());
  rds->mutable_config_source()->mutable_self();
  listener.add_filter_chains()->add_filters()->mutable_typed_config()->PackFrom(
      http_connection_manager);
  // Set another filter chain with another route config name
  auto another_route_config = default_server_route_config_;
  another_route_config.set_name("another_server_route_config");
  http_connection_manager.mutable_rds()->set_route_config_name(
      another_route_config.name());
  auto* filter_chain = listener.add_filter_chains();
  filter_chain->add_filters()->mutable_typed_config()->PackFrom(
      http_connection_manager);
  filter_chain->mutable_filter_chain_match()->set_source_type(
      FilterChainMatch::SAME_IP_OR_LOOPBACK);
  // Add another filter chain with the same route config name
  filter_chain = listener.add_filter_chains();
  filter_chain->add_filters()->mutable_typed_config()->PackFrom(
      http_connection_manager);
  filter_chain->mutable_filter_chain_match()->set_source_type(
      FilterChainMatch::EXTERNAL);
  // Add another filter chain with an inline route config
  filter_chain = listener.add_filter_chains();
  filter_chain->mutable_filter_chain_match()->add_source_ports(1234);
  http_connection_manager = ServerHcmAccessor().Unpack(listener);
  *http_connection_manager.mutable_route_config() =
      default_server_route_config_;
  filter_chain->add_filters()->mutable_typed_config()->PackFrom(
      http_connection_manager);
  // Set resources on the ADS service
  balancer_->ads_service()->SetRdsResource(new_route_config);
  balancer_->ads_service()->SetRdsResource(another_route_config);
  SetServerListenerNameAndRouteConfiguration(balancer_.get(), listener,
                                             backends_[0]->port(),
                                             default_server_route_config_);
  StartBackend(0);
  ASSERT_TRUE(backends_[0]->WaitOnServingStatusChange(grpc::StatusCode::OK));
  CheckRpcSendOk(DEBUG_LOCATION, 1, RpcOptions().set_wait_for_ready(true));
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
  overrides.trace =
      "call,channel,client_channel,client_channel_call,client_channel_lb_call,"
      "handshaker";
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
