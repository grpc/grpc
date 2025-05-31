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
#include <optional>

#include "fuzztest/fuzztest.h"
#include "gtest/gtest.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/event_engine/channel_args_endpoint_config.h"
#include "test/core/test_util/fuzzing_channel_args.h"
#include "test/core/test_util/fuzzing_channel_args.pb.h"

using ::grpc_event_engine::experimental::ChannelArgsEndpointConfig;

void ArbitraryAccess(
    const grpc::testing::FuzzingChannelArgs& fuzzing_channel_args,
    std::vector<std::string> keys_to_search) {
  grpc_core::testing::FuzzingEnvironment fuzzing_env;
  grpc_core::ChannelArgs args = CreateChannelArgsFromFuzzingConfiguration(
      fuzzing_channel_args, fuzzing_env);
  ChannelArgsEndpointConfig config(args);
  // Known keys are found
  for (const auto& fuzz_arg : fuzzing_channel_args.args()) {
    switch (fuzz_arg.value_case()) {
      case grpc::testing::FuzzingChannelArg::kStr: {
        auto val_s = config.GetString(fuzz_arg.key());
        ASSERT_TRUE(val_s.has_value());
        ASSERT_EQ(*val_s, fuzz_arg.str());
        break;
      }
      case grpc::testing::FuzzingChannelArg::kI: {
        auto val_i = config.GetInt(fuzz_arg.key());
        ASSERT_TRUE(val_i.has_value());
        ASSERT_EQ(*val_i, fuzz_arg.i());
        break;
      }
      case grpc::testing::FuzzingChannelArg::kResourceQuota: {
        auto val_p = config.GetVoidPointer(fuzz_arg.key());
        ASSERT_EQ(val_p, &fuzz_arg.resource_quota());
        break;
      }
      default:
        break;
    }
  }
  // Arbitrary keys do not crash
  for (const auto& key : keys_to_search) {
    auto result_i = config.GetInt(key);
    auto result_s = config.GetString(key);
    auto result_p = config.GetVoidPointer(key);
  }
}
FUZZ_TEST(EndpointConfigTest, ArbitraryAccess);
