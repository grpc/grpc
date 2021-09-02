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

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <grpc/event_engine/endpoint_config.h>
#include <grpc/grpc.h>

#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/event_engine/endpoint_config_internal.h"
#include "test/core/util/test_config.h"

using ::grpc_event_engine::experimental::ChannelArgsEndpointConfig;

TEST(EndpointConfigTest, CanSRetrieveValuesFromChannelArgs) {
  grpc_arg arg = grpc_channel_arg_integer_create(const_cast<char*>("arst"), 3);
  const grpc_channel_args args = {1, &arg};
  ChannelArgsEndpointConfig config(&args);
  EXPECT_EQ(absl::get<int>(config.Get("arst")), 3);
}

TEST(EndpointConfigTest, ReturnsMonostateForMissingKeys) {
  ChannelArgsEndpointConfig config(nullptr);
  EXPECT_TRUE(
      absl::holds_alternative<absl::monostate>(config.Get("nonexistent")));
}

int main(int argc, char** argv) {
  testing::InitGoogleTest(&argc, argv);
  auto result = RUN_ALL_TESTS();
  return result;
}
