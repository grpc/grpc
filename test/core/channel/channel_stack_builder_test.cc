//
//
// Copyright 2017 gRPC authors.
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

#include "src/core/lib/channel/channel_stack_builder.h"

#include <grpc/grpc.h>

#include <map>
#include <memory>
#include <utility>

#include "absl/status/status.h"
#include "gtest/gtest.h"
#include "src/core/lib/channel/channel_stack.h"
#include "src/core/lib/channel/channel_stack_builder_impl.h"
#include "src/core/lib/iomgr/closure.h"
#include "src/core/lib/iomgr/error.h"
#include "src/core/lib/iomgr/exec_ctx.h"
#include "test/core/test_util/test_config.h"

namespace grpc_core {
namespace testing {
namespace {

grpc_error_handle ChannelInitFunc(grpc_channel_element* /*elem*/,
                                  grpc_channel_element_args* /*args*/) {
  return absl::OkStatus();
}

grpc_error_handle CallInitFunc(grpc_call_element* /*elem*/,
                               const grpc_call_element_args* /*args*/) {
  return absl::OkStatus();
}

void ChannelDestroyFunc(grpc_channel_element* /*elem*/) {}

void CallDestroyFunc(grpc_call_element* /*elem*/,
                     const grpc_call_final_info* /*final_info*/,
                     grpc_closure* /*ignored*/) {}

const grpc_channel_filter* FilterNamed(absl::string_view name) {
  static auto* filters =
      new std::map<absl::string_view, const grpc_channel_filter*>;
  auto it = filters->find(name);
  if (it != filters->end()) return it->second;
  static auto* name_factories =
      new std::vector<std::unique_ptr<UniqueTypeName::Factory>>();
  name_factories->emplace_back(std::make_unique<UniqueTypeName::Factory>(name));
  auto unique_type_name = name_factories->back()->Create();
  return filters
      ->emplace(
          name,
          new grpc_channel_filter{
              grpc_call_next_op, grpc_channel_next_op, 0, CallInitFunc,
              grpc_call_stack_ignore_set_pollset_or_pollset_set,
              CallDestroyFunc, 0, ChannelInitFunc,
              [](grpc_channel_stack*, grpc_channel_element*) {},
              ChannelDestroyFunc, grpc_channel_next_get_info, unique_type_name})
      .first->second;
}

TEST(ChannelStackBuilder, UnknownTarget) {
  ChannelStackBuilderImpl builder("alpha-beta-gamma", GRPC_CLIENT_CHANNEL,
                                  ChannelArgs());
  EXPECT_EQ(builder.target(), "unknown");
}

TEST(ChannelStackBuilder, CanPrepend) {
  ExecCtx exec_ctx;
  ChannelStackBuilderImpl builder("alpha-beta-gamma", GRPC_CLIENT_CHANNEL,
                                  ChannelArgs());
  builder.PrependFilter(FilterNamed("filter1"));
  builder.PrependFilter(FilterNamed("filter2"));
  auto stack = builder.Build();
  EXPECT_TRUE(stack.ok());
  EXPECT_EQ((*stack)->count, 2);
  EXPECT_EQ(grpc_channel_stack_element(stack->get(), 0)->filter,
            FilterNamed("filter2"));
  EXPECT_EQ(grpc_channel_stack_element(stack->get(), 1)->filter,
            FilterNamed("filter1"));
}

TEST(ChannelStackBuilder, CanAppend) {
  ExecCtx exec_ctx;
  ChannelStackBuilderImpl builder("alpha-beta-gamma", GRPC_CLIENT_CHANNEL,
                                  ChannelArgs());
  builder.AppendFilter(FilterNamed("filter1"));
  builder.AppendFilter(FilterNamed("filter2"));
  auto stack = builder.Build();
  EXPECT_TRUE(stack.ok());
  EXPECT_EQ((*stack)->count, 2);
  EXPECT_EQ(grpc_channel_stack_element(stack->get(), 0)->filter,
            FilterNamed("filter1"));
  EXPECT_EQ(grpc_channel_stack_element(stack->get(), 1)->filter,
            FilterNamed("filter2"));
}

}  // namespace
}  // namespace testing
}  // namespace grpc_core

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  grpc::testing::TestEnvironment env(&argc, argv);
  grpc_init();
  int ret = RUN_ALL_TESTS();
  grpc_shutdown();
  return ret;
}
