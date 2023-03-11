// Copyright 2022 gRPC authors.
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

#include <memory>
#include <vector>

#include "end2end_tests.h"
#include "gtest/gtest.h"

#include <grpc/grpc.h>

#include "src/core/lib/channel/channel_args.h"
#include "test/core/end2end/end2end_tests.h"
#include "test/core/end2end/fixtures/secure_fixture.h"
#include "test/core/end2end/fixtures/sockpair_fixture.h"
#include "test/core/util/test_config.h"

namespace grpc_core {
class SockpairWithMinstackFixture : public SockpairFixture {
 public:
  using SockpairFixture::SockpairFixture;

 private:
  grpc_core::ChannelArgs MutateClientArgs(
      grpc_core::ChannelArgs args) override {
    return args.Set(GRPC_ARG_MINIMAL_STACK, true);
  }
  grpc_core::ChannelArgs MutateServerArgs(
      grpc_core::ChannelArgs args) override {
    return args.Set(GRPC_ARG_MINIMAL_STACK, true);
  }
};

const char* NameFromConfig(
    const ::testing::TestParamInfo<const CoreTestConfiguration*>& config) {
  return config.param->name;
}

const NoDestruct<std::vector<CoreTestConfiguration>> all_configs{
    std::vector<CoreTestConfiguration>{
        CoreTestConfiguration{"Chttp2Fullstack",
                              FEATURE_MASK_SUPPORTS_DELAYED_CONNECTION |
                                  FEATURE_MASK_SUPPORTS_CLIENT_CHANNEL |
                                  FEATURE_MASK_SUPPORTS_AUTHORITY_HEADER,
                              nullptr,
                              [](const ChannelArgs& /*client_args*/,
                                 const ChannelArgs& /*server_args*/) {
                                return std::make_unique<InsecureFixture>();
                              }},
        CoreTestConfiguration{
            "Chttp2SocketPair", FEATURE_MASK_SUPPORTS_AUTHORITY_HEADER, nullptr,
            [](const grpc_core::ChannelArgs&, const grpc_core::ChannelArgs&) {
              return std::make_unique<SockpairFixture>(
                  grpc_core::ChannelArgs());
            }},
        CoreTestConfiguration{
            "Chttp2SocketPairMinstack",
            FEATURE_MASK_SUPPORTS_AUTHORITY_HEADER |
                FEATURE_MASK_DOES_NOT_SUPPORT_DEADLINES |
                FEATURE_MASK_IS_MINSTACK,
            nullptr,
            [](const grpc_core::ChannelArgs&, const grpc_core::ChannelArgs&) {
              return std::make_unique<SockpairWithMinstackFixture>(
                  grpc_core::ChannelArgs());
            }}}};  // namespace grpc_core

std::vector<const CoreTestConfiguration*> QueryConfigs(uint32_t enforce_flags,
                                                       uint32_t exclude_flags) {
  std::vector<const CoreTestConfiguration*> out;
  for (const CoreTestConfiguration& config : *all_configs) {
    if ((config.feature_mask & enforce_flags) == enforce_flags &&
        (config.feature_mask & exclude_flags) == 0) {
      out.push_back(&config);
    }
  }
  return out;
}

INSTANTIATE_TEST_SUITE_P(CoreEnd2endTests, CoreEnd2endTest,
                         ::testing::ValuesIn(QueryConfigs(0, 0)),
                         NameFromConfig);

INSTANTIATE_TEST_SUITE_P(CoreDeadlineTests, CoreDeadlineTest,
                         ::testing::ValuesIn(QueryConfigs(
                             0, FEATURE_MASK_DOES_NOT_SUPPORT_DEADLINES)),
                         NameFromConfig);

INSTANTIATE_TEST_SUITE_P(
    CoreClientChannelTests, CoreClientChannelTest,
    ::testing::ValuesIn(QueryConfigs(FEATURE_MASK_SUPPORTS_CLIENT_CHANNEL, 0)),
    NameFromConfig);

}  // namespace grpc_core

int main(int argc, char** argv) {
  grpc::testing::TestEnvironment env(&argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  grpc_init();
  int r = RUN_ALL_TESTS();
  grpc_shutdown();
  return r;
}
