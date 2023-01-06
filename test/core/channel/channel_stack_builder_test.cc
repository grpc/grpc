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

#include <limits.h>
#include <string.h>

#include <algorithm>

#include "absl/status/status.h"
#include "gtest/gtest.h"

#include <grpc/grpc.h>
#include <grpc/grpc_security.h>
#include <grpc/support/log.h>

#include "src/core/lib/channel/channel_stack.h"
#include "src/core/lib/channel/channel_stack_builder_impl.h"
#include "src/core/lib/config/core_configuration.h"
#include "src/core/lib/iomgr/closure.h"
#include "src/core/lib/iomgr/error.h"
#include "test/core/util/test_config.h"

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

bool g_replacement_fn_called = false;
bool g_original_fn_called = false;

void SetReplacementFnCalled(grpc_channel_stack*, grpc_channel_element*) {
  g_replacement_fn_called = true;
}

void SetOriginalFnCalled(grpc_channel_stack*, grpc_channel_element*) {
  g_original_fn_called = true;
}

TEST(ChannelStackBuilderTest, ReplaceFilter) {
  grpc_channel_credentials* creds = grpc_insecure_credentials_create();
  grpc_channel* channel =
      grpc_channel_create("target name isn't used", creds, nullptr);
  grpc_channel_credentials_release(creds);
  GPR_ASSERT(channel != nullptr);
  // Make sure the high priority filter has been created.
  GPR_ASSERT(g_replacement_fn_called);
  // ... and that the low priority one hasn't.
  GPR_ASSERT(!g_original_fn_called);
  grpc_channel_destroy(channel);
}

const grpc_channel_filter replacement_filter = {
    grpc_call_next_op,    nullptr,
    grpc_channel_next_op, 0,
    CallInitFunc,         grpc_call_stack_ignore_set_pollset_or_pollset_set,
    CallDestroyFunc,      0,
    ChannelInitFunc,      SetReplacementFnCalled,
    ChannelDestroyFunc,   grpc_channel_next_get_info,
    "filter_name"};

const grpc_channel_filter original_filter = {
    grpc_call_next_op,    nullptr,
    grpc_channel_next_op, 0,
    CallInitFunc,         grpc_call_stack_ignore_set_pollset_or_pollset_set,
    CallDestroyFunc,      0,
    ChannelInitFunc,      SetOriginalFnCalled,
    ChannelDestroyFunc,   grpc_channel_next_get_info,
    "filter_name"};

bool AddReplacementFilter(ChannelStackBuilder* builder) {
  // Get rid of any other version of the filter, as determined by having the
  // same name.
  auto* stk = builder->mutable_stack();
  stk->erase(std::remove_if(stk->begin(), stk->end(),
                            [](const grpc_channel_filter* entry) {
                              return strcmp(entry->name, "filter_name") == 0;
                            }),
             stk->end());
  builder->PrependFilter(&replacement_filter);
  return true;
}

bool AddOriginalFilter(ChannelStackBuilder* builder) {
  builder->PrependFilter(&original_filter);
  return true;
}

TEST(ChannelStackBuilder, UnknownTarget) {
  ChannelStackBuilderImpl builder("alpha-beta-gamma", GRPC_CLIENT_CHANNEL,
                                  ChannelArgs());
  EXPECT_EQ(builder.target(), "unknown");
}

}  // namespace
}  // namespace testing
}  // namespace grpc_core

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  grpc::testing::TestEnvironment env(&argc, argv);
  grpc_core::CoreConfiguration::RegisterBuilder(
      [](grpc_core::CoreConfiguration::Builder* builder) {
        builder->channel_init()->RegisterStage(
            GRPC_CLIENT_CHANNEL, INT_MAX,
            grpc_core::testing::AddOriginalFilter);
        builder->channel_init()->RegisterStage(
            GRPC_CLIENT_CHANNEL, INT_MAX,
            grpc_core::testing::AddReplacementFilter);
      });
  grpc_init();
  int ret = RUN_ALL_TESTS();
  grpc_shutdown();
  return ret;
}
