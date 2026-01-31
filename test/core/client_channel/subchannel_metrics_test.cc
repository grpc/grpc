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

#include <grpc/support/port_platform.h>

#include "src/core/telemetry/instrument.h"
#include "test/core/test_util/fake_stats_plugin.h"
#include "test/core/test_util/test_config.h"
#include "gtest/gtest.h"

namespace grpc_core {
namespace testing {
namespace {

class SubchannelTest : public ::testing::Test {
 protected:
  void SetUp() override { grpc_init(); }
  void TearDown() override { grpc_shutdown_blocking(); }
};

TEST_F(SubchannelTest, MetricDefinitionDisconnections) {
  const auto* descriptor = instrument_detail::InstrumentIndex::Get().Find(
      "grpc.subchannel.disconnections");
  ASSERT_NE(descriptor, nullptr);
  EXPECT_EQ(descriptor->name, "grpc.subchannel.disconnections");
  EXPECT_EQ(descriptor->description, "Number of times the selected subchannel becomes disconnected.");
  EXPECT_EQ(descriptor->unit, "disconnection");
  EXPECT_TRUE(std::holds_alternative<grpc_core::InstrumentMetadata::CounterShape>(
      descriptor->shape));
  EXPECT_THAT(descriptor->domain->label_names(),
              ::testing::ElementsAre("grpc.target",
                                     "grpc.lb.backend_service",
                                     "grpc.lb.locality",
                                     "grpc.disconnect_error"));
}

TEST_F(SubchannelTest, MetricDefinitionConnectionAttemptsSucceeded) {
  for (const auto& [name, description] : std::vector<std::pair<absl::string_view, absl::string_view>> {
    {"grpc.subchannel.connection_attempts_succeeded", "Number of successful connection attempts."},
    {"grpc.subchannel.connection_attempts_failed", "Number of failed connection attempts."},}) {
    const auto* descriptor = instrument_detail::InstrumentIndex::Get().Find(name);
    ASSERT_NE(descriptor, nullptr);
    EXPECT_EQ(descriptor->name, name);
    EXPECT_EQ(descriptor->description, description);
    EXPECT_EQ(descriptor->unit, "attempt");
    EXPECT_TRUE(std::holds_alternative<grpc_core::InstrumentMetadata::CounterShape>(
        descriptor->shape));
    EXPECT_THAT(descriptor->domain->label_names(),
                ::testing::ElementsAre("grpc.target",
                                      "grpc.lb.backend_service",
                                      "grpc.lb.locality"));
    }
  }

TEST_F(SubchannelTest, MetricDefinitionOpenConnections) {
  const auto* descriptor = instrument_detail::InstrumentIndex::Get().Find(
      "grpc.subchannel.open_connections");
  ASSERT_NE(descriptor, nullptr);
  EXPECT_EQ(descriptor->name, "grpc.subchannel.open_connections");
  EXPECT_EQ(descriptor->description, "Number of open subchannel connections.");
  EXPECT_EQ(descriptor->unit, "connection");
  EXPECT_TRUE(std::holds_alternative<grpc_core::InstrumentMetadata::UpDownCounterShape>(
      descriptor->shape));
  EXPECT_THAT(descriptor->domain->label_names(),
              ::testing::ElementsAre("grpc.target",
                                     "grpc.security_level",
                                     "grpc.lb.backend_service",
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
