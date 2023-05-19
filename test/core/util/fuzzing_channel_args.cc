// Copyright 2023 The gRPC Authors
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

#include "test/core/util/fuzzing_channel_args.h"

#include <string>

#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/resource_quota/resource_quota.h"
#include "test/core/util/fuzzing_channel_args.pb.h"

namespace grpc_core {
namespace testing {

namespace {
using grpc::testing::FuzzingChannelArg;
}  // namespace

ChannelArgs CreateChannelArgsFromFuzzingConfiguration(
    const grpc::testing::FuzzingChannelArgs& fuzzing_channel_args,
    const FuzzingEnvironment& fuzzing_environment) {
  ChannelArgs channel_args;
  for (const auto& fuzz_arg : fuzzing_channel_args.args()) {
    switch (fuzz_arg.value_case()) {
      case FuzzingChannelArg::kStr:
        channel_args = channel_args.Set(fuzz_arg.key(), fuzz_arg.str());
        break;
      case FuzzingChannelArg::kI:
        channel_args = channel_args.Set(fuzz_arg.key(), fuzz_arg.i());
        break;
      case FuzzingChannelArg::kResourceQuota:
        channel_args =
            channel_args.SetObject(fuzzing_environment.resource_quota);
        break;
      default:
        // ignore
        break;
    }
  }
  return channel_args;
}

}  // namespace testing
}  // namespace grpc_core
