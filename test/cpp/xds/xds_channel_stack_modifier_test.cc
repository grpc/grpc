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

#include <gtest/gtest.h>

#include <grpcpp/opencensus.h>

#include "src/core/lib/config/core_configuration.h"
#include "src/core/lib/surface/channel_init.h"
#include "src/core/lib/transport/transport_impl.h"
#include "test/core/util/test_config.h"

namespace grpc {
namespace testing {
namespace {

// Test that XdsChannelStackModifier can be safely copied to channel args
// and destroyed
TEST(XdsChannelStackModifierTest, CopyChannelArgs) {
  grpc_init();
  auto server_config_selector_provider =
      grpc_core::MakeRefCounted<grpc_core::XdsChannelStackModifier>(
          std::vector<const grpc_channel_filter*>{});
  grpc_arg arg = server_config_selector_provider->MakeChannelArg();
  grpc_channel_args* args = grpc_channel_args_copy_and_add(nullptr, &arg, 1);
  EXPECT_EQ(server_config_selector_provider,
            grpc_core::XdsChannelStackModifier::GetFromChannelArgs(*args));
  grpc_channel_args_destroy(args);
  grpc_shutdown();
}

// Test compare on channel args with the same XdsChannelStackModifier
TEST(XdsChannelStackModifierTest, ChannelArgsCompare) {
  grpc_init();
  auto server_config_selector_provider =
      grpc_core::MakeRefCounted<grpc_core::XdsChannelStackModifier>(
          std::vector<const grpc_channel_filter*>{});
  grpc_arg arg = server_config_selector_provider->MakeChannelArg();
  grpc_channel_args* args = grpc_channel_args_copy_and_add(nullptr, &arg, 1);
  grpc_channel_args* new_args = grpc_channel_args_copy(args);
  EXPECT_EQ(grpc_core::XdsChannelStackModifier::GetFromChannelArgs(*new_args),
            grpc_core::XdsChannelStackModifier::GetFromChannelArgs(*args));
  grpc_channel_args_destroy(args);
  grpc_channel_args_destroy(new_args);
  grpc_shutdown();
}

constexpr char kTestFilter1[] = "test_filter_1";
constexpr char kTestFilter2[] = "test_filter_2";

// Test filters insertion
TEST(XdsChannelStackModifierTest, XdsHttpFiltersInsertion) {
  grpc_core::CoreConfiguration::Reset();
  grpc_init();
  // Add 2 dummy test filters to XdsChannelStackModifier
  const grpc_channel_filter test_filter_1 = {
      nullptr, nullptr, 0,       nullptr, nullptr,     nullptr,
      0,       nullptr, nullptr, nullptr, kTestFilter1};
  const grpc_channel_filter test_filter_2 = {
      nullptr, nullptr, 0,       nullptr, nullptr,     nullptr,
      0,       nullptr, nullptr, nullptr, kTestFilter2};
  auto server_config_selector_provider =
      grpc_core::MakeRefCounted<grpc_core::XdsChannelStackModifier>(
          std::vector<const grpc_channel_filter*>{&test_filter_1,
                                                  &test_filter_2});
  grpc_arg arg = server_config_selector_provider->MakeChannelArg();
  // Create a phony grpc_channel_stack_builder object
  grpc_channel_args* args = grpc_channel_args_copy_and_add(nullptr, &arg, 1);
  grpc_channel_stack_builder* builder = grpc_channel_stack_builder_create();
  grpc_channel_stack_builder_set_channel_arguments(builder, args);
  grpc_channel_args_destroy(args);
  grpc_transport_vtable fake_transport_vtable;
  memset(&fake_transport_vtable, 0, sizeof(grpc_transport_vtable));
  fake_transport_vtable.name = "fake";
  grpc_transport fake_transport = {&fake_transport_vtable};
  grpc_channel_stack_builder_set_transport(builder, &fake_transport);
  // Construct channel stack and verify that the test filters were successfully
  // added
  ASSERT_TRUE(grpc_core::CoreConfiguration::Get().channel_init().CreateStack(
      builder, GRPC_SERVER_CHANNEL));
  grpc_channel_stack_builder_iterator* it =
      grpc_channel_stack_builder_create_iterator_at_first(builder);
  ASSERT_TRUE(grpc_channel_stack_builder_move_next(it));
  ASSERT_STREQ(grpc_channel_stack_builder_iterator_filter_name(it), "server");
  ASSERT_TRUE(grpc_channel_stack_builder_move_next(it));
  ASSERT_STREQ(grpc_channel_stack_builder_iterator_filter_name(it),
               kTestFilter1);
  ASSERT_TRUE(grpc_channel_stack_builder_move_next(it));
  ASSERT_STREQ(grpc_channel_stack_builder_iterator_filter_name(it),
               kTestFilter2);
  grpc_channel_stack_builder_iterator_destroy(it);
  grpc_shutdown();
}

// Test filters insertion with OpenCensus plugin registered
TEST(XdsChannelStackModifierTest, XdsHttpFiltersInsertionAfterCensus) {
  grpc_core::CoreConfiguration::Reset();
  RegisterOpenCensusPlugin();
  grpc_init();
  // Add 2 dummy test filters to XdsChannelStackModifier
  const grpc_channel_filter test_filter_1 = {
      nullptr, nullptr, 0,       nullptr, nullptr,     nullptr,
      0,       nullptr, nullptr, nullptr, kTestFilter1};
  const grpc_channel_filter test_filter_2 = {
      nullptr, nullptr, 0,       nullptr, nullptr,     nullptr,
      0,       nullptr, nullptr, nullptr, kTestFilter2};
  auto server_config_selector_provider =
      grpc_core::MakeRefCounted<grpc_core::XdsChannelStackModifier>(
          std::vector<const grpc_channel_filter*>{&test_filter_1,
                                                  &test_filter_2});
  grpc_arg arg = server_config_selector_provider->MakeChannelArg();
  // Create a phony grpc_channel_stack_builder object
  grpc_channel_args* args = grpc_channel_args_copy_and_add(nullptr, &arg, 1);
  grpc_channel_stack_builder* builder = grpc_channel_stack_builder_create();
  grpc_channel_stack_builder_set_channel_arguments(builder, args);
  grpc_channel_args_destroy(args);
  grpc_transport_vtable fake_transport_vtable;
  memset(&fake_transport_vtable, 0, sizeof(grpc_transport_vtable));
  fake_transport_vtable.name = "fake";
  grpc_transport fake_transport = {&fake_transport_vtable};
  grpc_channel_stack_builder_set_transport(builder, &fake_transport);
  // Construct channel stack and verify that the test filters were successfully
  // added after the census filter
  ASSERT_TRUE(grpc_core::CoreConfiguration::Get().channel_init().CreateStack(
      builder, GRPC_SERVER_CHANNEL));
  grpc_channel_stack_builder_iterator* it =
      grpc_channel_stack_builder_create_iterator_at_first(builder);
  ASSERT_TRUE(grpc_channel_stack_builder_move_next(it));
  ASSERT_STREQ(grpc_channel_stack_builder_iterator_filter_name(it), "server");
  ASSERT_TRUE(grpc_channel_stack_builder_move_next(it));
  ASSERT_STREQ(grpc_channel_stack_builder_iterator_filter_name(it),
               "opencensus_server");
  ASSERT_TRUE(grpc_channel_stack_builder_move_next(it));
  ASSERT_STREQ(grpc_channel_stack_builder_iterator_filter_name(it),
               kTestFilter1);
  ASSERT_TRUE(grpc_channel_stack_builder_move_next(it));
  ASSERT_STREQ(grpc_channel_stack_builder_iterator_filter_name(it),
               kTestFilter2);
  grpc_channel_stack_builder_iterator_destroy(it);
  grpc_shutdown();
}

}  // namespace
}  // namespace testing
}  // namespace grpc

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  grpc::testing::TestEnvironment env(argc, argv);
  int ret = RUN_ALL_TESTS();
  return ret;
}
