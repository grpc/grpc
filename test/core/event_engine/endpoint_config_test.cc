// Copyright 2021 The gRPC Authors
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
#include <grpc/support/port_platform.h>

#include <memory>

#include "absl/types/optional.h"
#include "gtest/gtest.h"

#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/event_engine/channel_args_endpoint_config.h"

using ::grpc_event_engine::experimental::ChannelArgsEndpointConfig;

TEST(EndpointConfigTest, CanSRetrieveValuesFromChannelArgs) {
  grpc_core::ChannelArgs args;
  args = args.Set("arst", 3);
  ChannelArgsEndpointConfig config(args);
  EXPECT_EQ(*config.GetInt("arst"), 3);
}

TEST(EndpointConfigTest, ReturnsNoValueForMissingKeys) {
  ChannelArgsEndpointConfig config;
  EXPECT_TRUE(!config.GetInt("nonexistent").has_value());
  EXPECT_TRUE(!config.GetString("nonexistent").has_value());
  EXPECT_EQ(config.GetVoidPointer("nonexistent"), nullptr);
}

int main(int argc, char** argv) {
  testing::InitGoogleTest(&argc, argv);
  auto result = RUN_ALL_TESTS();
  return result;
}
