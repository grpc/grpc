//
//
// Copyright 2021 gRPC authors.
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
//

#include "src/core/ext/filters/server_config_selector/server_config_selector.h"

#include <gtest/gtest.h>

#include <grpc/grpc.h>
#include <grpc/support/log.h>

#include "src/core/lib/channel/channel_args.h"
#include "test/core/util/test_config.h"

namespace grpc {
namespace testing {
namespace {

using grpc_core::ServerConfigSelector;
using grpc_core::ServerConfigSelectorProvider;

class TestServerConfigSelectorProvider : public ServerConfigSelectorProvider {
  absl::StatusOr<grpc_core::RefCountedPtr<ServerConfigSelector>> Watch(
      std::unique_ptr<ServerConfigSelectorWatcher> /* watcher */) override {
    return absl::UnavailableError("Test ServerConfigSelector");
  }

  void CancelWatch() override {}
};

// Test that ServerConfigSelectorProvider can be safely copied to channel args
// and destroyed
TEST(ServerConfigSelectorProviderTest, CopyChannelArgs) {
  auto server_config_selector_provider =
      grpc_core::MakeRefCounted<TestServerConfigSelectorProvider>();
  grpc_arg arg = server_config_selector_provider->MakeChannelArg();
  grpc_channel_args* args = grpc_channel_args_copy_and_add(nullptr, &arg, 1);
  EXPECT_EQ(server_config_selector_provider,
            ServerConfigSelectorProvider::GetFromChannelArgs(*args));
  grpc_channel_args_destroy(args);
}

// Test compare on channel args with the same ServerConfigSelectorProvider
TEST(ServerConfigSelectorProviderTest, ChannelArgsCompare) {
  auto server_config_selector_provider =
      grpc_core::MakeRefCounted<TestServerConfigSelectorProvider>();
  grpc_arg arg = server_config_selector_provider->MakeChannelArg();
  grpc_channel_args* args = grpc_channel_args_copy_and_add(nullptr, &arg, 1);
  grpc_channel_args* new_args = grpc_channel_args_copy(args);
  EXPECT_EQ(ServerConfigSelectorProvider::GetFromChannelArgs(*new_args),
            ServerConfigSelectorProvider::GetFromChannelArgs(*args));
  grpc_channel_args_destroy(args);
  grpc_channel_args_destroy(new_args);
}

}  // namespace
}  // namespace testing
}  // namespace grpc

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  grpc::testing::TestEnvironment env(argc, argv);
  grpc_init();
  int ret = RUN_ALL_TESTS();
  grpc_shutdown();
  return ret;
}
