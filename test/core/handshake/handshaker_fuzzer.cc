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

#include "fuzztest/fuzztest.h"
#include "gtest/gtest.h"
#include "test/core/event_engine/event_engine_test_utils.h"
#include "test/core/event_engine/fuzzing_event_engine/fuzzing_event_engine.pb.h"
#include "test/core/handshake/test_handshake.h"
#include "test/core/test_util/fuzzing_channel_args.h"
#include "test/core/test_util/fuzzing_channel_args.pb.h"

namespace grpc_core {
namespace {

auto BaseChannelArgs() { return ChannelArgs(); }

auto AnyChannelArgs() {
  return ::fuzztest::Map(
      [](const grpc::testing::FuzzingChannelArgs& args) {
        testing::FuzzingEnvironment env;
        return testing::CreateChannelArgsFromFuzzingConfiguration(args, env);
      },
      ::fuzztest::Arbitrary<grpc::testing::FuzzingChannelArgs>());
}

// Without supplying channel args, we should expect basic TCP connections to
// succeed every time.
void BasicHandshakeSucceeds(const fuzzing_event_engine::Actions& actions) {
  if (!grpc_event_engine::experimental::IsSaneTimerEnvironment()) {
    GTEST_SKIP() << "Needs most EventEngine experiments enabled";
  }
  CHECK_OK(TestHandshake(BaseChannelArgs(), BaseChannelArgs(), actions));
}
FUZZ_TEST(HandshakerFuzzer, BasicHandshakeSucceeds);

TEST(HandshakerFuzzer, BasicHandshakeSucceedsRegression1) {
  BasicHandshakeSucceeds(fuzzing_event_engine::Actions());
}

// Supplying effectively random channel args, we should expect no crashes (but
// hey, maybe we don't connect).
void RandomChannelArgsDontCauseCrashes(
    const ChannelArgs& client_args, const ChannelArgs& server_args,
    const fuzzing_event_engine::Actions& actions) {
  if (!grpc_event_engine::experimental::IsSaneTimerEnvironment()) {
    GTEST_SKIP() << "Needs most EventEngine experiments enabled";
  }
  std::ignore = TestHandshake(client_args, server_args, actions);
}
FUZZ_TEST(HandshakerFuzzer, RandomChannelArgsDontCauseCrashes)
    .WithDomains(AnyChannelArgs(), AnyChannelArgs(),
                 ::fuzztest::Arbitrary<fuzzing_event_engine::Actions>());

}  // namespace
}  // namespace grpc_core
