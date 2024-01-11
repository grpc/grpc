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

#include "src/core/ext/xds/xds_channel_stack_modifier.h"

#include <algorithm>
#include <string>

#include "gtest/gtest.h"

#include <grpc/grpc.h>

#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/channel/channel_stack.h"
#include "src/core/lib/channel/channel_stack_builder_impl.h"
#include "src/core/lib/config/core_configuration.h"
#include "src/core/lib/iomgr/endpoint.h"
#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/lib/iomgr/iomgr_fwd.h"
#include "src/core/lib/surface/channel_init.h"
#include "src/core/lib/surface/channel_stack_type.h"
#include "src/core/lib/transport/transport.h"
#include "test/core/util/test_config.h"

namespace grpc_core {
namespace testing {
namespace {

// Test that XdsChannelStackModifier can be safely copied to channel args
// and destroyed
TEST(XdsChannelStackModifierTest, CopyChannelArgs) {
  grpc_init();
  auto channel_stack_modifier = MakeRefCounted<XdsChannelStackModifier>(
      std::vector<const grpc_channel_filter*>{});
  grpc_arg arg = channel_stack_modifier->MakeChannelArg();
  grpc_channel_args* args = grpc_channel_args_copy_and_add(nullptr, &arg, 1);
  EXPECT_EQ(channel_stack_modifier,
            XdsChannelStackModifier::GetFromChannelArgs(*args));
  grpc_channel_args_destroy(args);
  grpc_shutdown();
}

// Test compare on channel args with the same XdsChannelStackModifier
TEST(XdsChannelStackModifierTest, ChannelArgsCompare) {
  grpc_init();
  auto channel_stack_modifier = MakeRefCounted<XdsChannelStackModifier>(
      std::vector<const grpc_channel_filter*>{});
  grpc_arg arg = channel_stack_modifier->MakeChannelArg();
  grpc_channel_args* args = grpc_channel_args_copy_and_add(nullptr, &arg, 1);
  grpc_channel_args* new_args = grpc_channel_args_copy(args);
  EXPECT_EQ(XdsChannelStackModifier::GetFromChannelArgs(*new_args),
            XdsChannelStackModifier::GetFromChannelArgs(*args));
  grpc_channel_args_destroy(args);
  grpc_channel_args_destroy(new_args);
  grpc_shutdown();
}

constexpr char kTestFilter1[] = "test_filter_1";
constexpr char kTestFilter2[] = "test_filter_2";

namespace {
class FakeTransport final : public Transport {
 public:
  FilterStackTransport* filter_stack_transport() override { return nullptr; }
  ClientTransport* client_transport() override { return nullptr; }
  ServerTransport* server_transport() override { return nullptr; }

  absl::string_view GetTransportName() const override { return "fake"; }
  void SetPollset(grpc_stream*, grpc_pollset*) override {}
  void SetPollsetSet(grpc_stream*, grpc_pollset_set*) override {}
  void PerformOp(grpc_transport_op*) override {}
  grpc_endpoint* GetEndpoint() override { return nullptr; }
  void Orphan() override {}
};
}  // namespace

// Test filters insertion
TEST(XdsChannelStackModifierTest, XdsHttpFiltersInsertion) {
  CoreConfiguration::Reset();
  grpc_init();
  // Add 2 test filters to XdsChannelStackModifier
  const grpc_channel_filter test_filter_1 = {
      nullptr, nullptr, nullptr, nullptr, 0,       nullptr, nullptr,
      nullptr, 0,       nullptr, nullptr, nullptr, nullptr, kTestFilter1};
  const grpc_channel_filter test_filter_2 = {
      nullptr, nullptr, nullptr, nullptr, 0,       nullptr, nullptr,
      nullptr, 0,       nullptr, nullptr, nullptr, nullptr, kTestFilter2};
  auto channel_stack_modifier = MakeRefCounted<XdsChannelStackModifier>(
      std::vector<const grpc_channel_filter*>{&test_filter_1, &test_filter_2});
  grpc_arg arg = channel_stack_modifier->MakeChannelArg();
  FakeTransport fake_transport;
  // Create a phony ChannelStackBuilder object
  grpc_channel_args* args = grpc_channel_args_copy_and_add(nullptr, &arg, 1);
  ChannelStackBuilderImpl builder(
      "test", GRPC_SERVER_CHANNEL,
      ChannelArgs::FromC(args).SetObject<Transport>(&fake_transport));
  grpc_channel_args_destroy(args);
  // Construct channel stack and verify that the test filters were successfully
  // added
  {
    ExecCtx exec_ctx;
    ASSERT_TRUE(CoreConfiguration::Get().channel_init().CreateStack(&builder));
  }
  std::vector<std::string> filters;
  for (const auto& entry : *builder.mutable_stack()) {
    filters.push_back(entry->name);
  }
  filters.resize(3);
  EXPECT_EQ(filters,
            std::vector<std::string>({"server", kTestFilter1, kTestFilter2}));
  grpc_shutdown();
}

}  // namespace
}  // namespace testing
}  // namespace grpc_core

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  grpc::testing::TestEnvironment env(&argc, argv);
  int ret = RUN_ALL_TESTS();
  return ret;
}
