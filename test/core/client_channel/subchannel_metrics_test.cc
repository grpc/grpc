//
// Copyright 2025 gRPC authors.
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
#include <grpc/support/port_platform.h>

#include <memory>
#include <string>

#include "src/core/client_channel/subchannel.h"
#include "src/core/config/core_configuration.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/experiments/experiments.h"
#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/telemetry/metrics.h"
#include "test/core/test_util/fake_stats_plugin.h"
#include "test/core/test_util/test_config.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/status/status.h"

namespace grpc_core {
namespace testing {
namespace {

class SubchannelTest : public ::testing::Test {
 protected:
  void SetUp() override { grpc_init(); }

  void TearDown() override { grpc_shutdown_blocking(); }
};

TEST_F(SubchannelTest, MetricDefinitionDisconnections) {
  const auto* descriptor =
      GlobalInstrumentsRegistryTestPeer::FindMetricDescriptorByName(
          "grpc.subchannel.disconnections");
  ASSERT_NE(descriptor, nullptr);
  EXPECT_EQ(descriptor->value_type,
            GlobalInstrumentsRegistry::ValueType::kUInt64);
  EXPECT_EQ(descriptor->instrument_type,
            GlobalInstrumentsRegistry::InstrumentType::kCounter);
  EXPECT_EQ(descriptor->enable_by_default, false);
  EXPECT_EQ(descriptor->name, "grpc.subchannel.disconnections");
  EXPECT_EQ(descriptor->unit, "{disconnection}");
  EXPECT_THAT(descriptor->label_keys, ::testing::ElementsAre("grpc.target"));
  EXPECT_THAT(
      descriptor->optional_label_keys,
      ::testing::ElementsAre("grpc.lb.backend_service", "grpc.lb.locality",
                             "grpc.disconnect_error"));
}

TEST_F(SubchannelTest, MetricDefinitionConnectionAttemptsSucceeded) {
  const auto* descriptor =
      GlobalInstrumentsRegistryTestPeer::FindMetricDescriptorByName(
          "grpc.subchannel.connection_attempts_succeeded");
  ASSERT_NE(descriptor, nullptr);
  EXPECT_EQ(descriptor->value_type,
            GlobalInstrumentsRegistry::ValueType::kUInt64);
  EXPECT_EQ(descriptor->instrument_type,
            GlobalInstrumentsRegistry::InstrumentType::kCounter);
  EXPECT_EQ(descriptor->enable_by_default, false);
  EXPECT_EQ(descriptor->name, "grpc.subchannel.connection_attempts_succeeded");
  EXPECT_EQ(descriptor->unit, "{attempt}");
  EXPECT_THAT(descriptor->label_keys, ::testing::ElementsAre("grpc.target"));
  EXPECT_THAT(
      descriptor->optional_label_keys,
      ::testing::ElementsAre("grpc.lb.backend_service", "grpc.lb.locality"));
}

TEST_F(SubchannelTest, MetricDefinitionConnectionAttemptsFailed) {
  const auto* descriptor =
      GlobalInstrumentsRegistryTestPeer::FindMetricDescriptorByName(
          "grpc.subchannel.connection_attempts_failed");
  ASSERT_NE(descriptor, nullptr);
  EXPECT_EQ(descriptor->value_type,
            GlobalInstrumentsRegistry::ValueType::kUInt64);
  EXPECT_EQ(descriptor->instrument_type,
            GlobalInstrumentsRegistry::InstrumentType::kCounter);
  EXPECT_EQ(descriptor->enable_by_default, false);
  EXPECT_EQ(descriptor->name, "grpc.subchannel.connection_attempts_failed");
  EXPECT_EQ(descriptor->unit, "{attempt}");
  EXPECT_THAT(descriptor->label_keys, ::testing::ElementsAre("grpc.target"));
  EXPECT_THAT(
      descriptor->optional_label_keys,
      ::testing::ElementsAre("grpc.lb.backend_service", "grpc.lb.locality"));
}

TEST_F(SubchannelTest, MetricDefinitionOpenConnections) {
  const auto* descriptor =
      GlobalInstrumentsRegistryTestPeer::FindMetricDescriptorByName(
          "grpc.subchannel.open_connections");
  ASSERT_NE(descriptor, nullptr);
  EXPECT_EQ(descriptor->value_type,
            GlobalInstrumentsRegistry::ValueType::kUInt64);
  EXPECT_EQ(descriptor->instrument_type,
            GlobalInstrumentsRegistry::InstrumentType::kUpDownCounter);
  EXPECT_EQ(descriptor->enable_by_default, false);
  EXPECT_EQ(descriptor->name, "grpc.subchannel.open_connections");
  EXPECT_EQ(descriptor->unit, "{connection}");
  EXPECT_THAT(descriptor->label_keys, ::testing::ElementsAre("grpc.target"));
  EXPECT_THAT(
      descriptor->optional_label_keys,
      ::testing::ElementsAre("grpc.security_level", "grpc.lb.backend_service",
                             "grpc.lb.locality"));
}

}  // namespace
}  // namespace testing
}  // namespace grpc_core

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  grpc::testing::TestEnvironment env(&argc, argv);
  auto result = RUN_ALL_TESTS();
  return result;
}
