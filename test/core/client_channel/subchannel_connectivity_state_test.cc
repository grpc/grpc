/*
 *
 * Copyright 2024 gRPC authors.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

#include "src/core/client_channel/subchannel_connectivity_state.h"

#include <gtest/gtest.h>

#include "src/core/util/status_helper.h"
#include "test/core/test_util/test_config.h"

namespace grpc_core {
namespace {

TEST(SubchannelConnectivityStateTest, InitialState) {
  SubchannelConnectivityState state(false);
  EXPECT_EQ(state.state(), GRPC_CHANNEL_IDLE);
  EXPECT_TRUE(state.status().ok());
  EXPECT_FALSE(state.created_from_endpoint());
}

TEST(SubchannelConnectivityStateTest, InitialStateFromEndpoint) {
  SubchannelConnectivityState state(true);
  EXPECT_EQ(state.state(), GRPC_CHANNEL_IDLE);
  EXPECT_TRUE(state.status().ok());
  EXPECT_TRUE(state.created_from_endpoint());
}

TEST(SubchannelConnectivityStateTest, ConnectionAttemptInFlight) {
  SubchannelConnectivityState state(false);
  state.SetConnectionAttemptInFlight(true);
  EXPECT_TRUE(state.CheckUpdate());
  EXPECT_EQ(state.state(), GRPC_CHANNEL_CONNECTING);
  EXPECT_TRUE(state.status().ok());

  state.SetConnectionAttemptInFlight(false);
  EXPECT_TRUE(state.CheckUpdate());
  EXPECT_EQ(state.state(), GRPC_CHANNEL_IDLE);
  EXPECT_TRUE(state.status().ok());
}

TEST(SubchannelConnectivityStateTest, ActiveConnection) {
  SubchannelConnectivityState state(false);
  state.SetHasActiveConnections(true);
  EXPECT_TRUE(state.CheckUpdate());
  EXPECT_EQ(state.state(), GRPC_CHANNEL_READY);
  EXPECT_TRUE(state.status().ok());

  state.SetHasActiveConnections(false);
  EXPECT_TRUE(state.CheckUpdate());
  EXPECT_EQ(state.state(), GRPC_CHANNEL_IDLE);
  EXPECT_TRUE(state.status().ok());
}

TEST(SubchannelConnectivityStateTest, RetryTimer) {
  SubchannelConnectivityState state(false);
  state.SetHasRetryTimer(true);
  EXPECT_TRUE(state.CheckUpdate());
  EXPECT_EQ(state.state(), GRPC_CHANNEL_TRANSIENT_FAILURE);
  EXPECT_TRUE(state.status().ok());

  state.SetHasRetryTimer(false);
  EXPECT_TRUE(state.CheckUpdate());
  EXPECT_EQ(state.state(), GRPC_CHANNEL_IDLE);
  EXPECT_TRUE(state.status().ok());
}

TEST(SubchannelConnectivityStateTest, FailureStatus) {
  SubchannelConnectivityState state(false);
  absl::Status failure = absl::UnavailableError("failed");
  state.SetLastFailureStatus(failure);
  state.SetHasRetryTimer(true);
  EXPECT_TRUE(state.CheckUpdate());
  EXPECT_EQ(state.state(), GRPC_CHANNEL_TRANSIENT_FAILURE);
  EXPECT_EQ(state.status(), failure);
}

TEST(SubchannelConnectivityStateTest, Priority) {
  SubchannelConnectivityState state(false);
  
  // Ready beats everything
  state.SetHasActiveConnections(true);
  state.SetConnectionAttemptInFlight(true);
  state.SetHasRetryTimer(true);
  EXPECT_TRUE(state.CheckUpdate());
  EXPECT_EQ(state.state(), GRPC_CHANNEL_READY);

  // Connecting beats Transient Failure
  state.SetHasActiveConnections(false);
  EXPECT_TRUE(state.CheckUpdate());
  EXPECT_EQ(state.state(), GRPC_CHANNEL_CONNECTING);

  // Transient Failure beats Idle
  state.SetConnectionAttemptInFlight(false);
  EXPECT_TRUE(state.CheckUpdate());
  EXPECT_EQ(state.state(), GRPC_CHANNEL_TRANSIENT_FAILURE);

  // Idle is default
  state.SetHasRetryTimer(false);
  EXPECT_TRUE(state.CheckUpdate());
  EXPECT_EQ(state.state(), GRPC_CHANNEL_IDLE);
}

}  // namespace
}  // namespace grpc_core

int main(int argc, char** argv) {
  grpc::testing::TestEnvironment env(&argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
