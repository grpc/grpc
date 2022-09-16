/*
 *
 * Copyright 2015 gRPC authors.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

#include "src/core/lib/channel/channel_stack.h"

#include <string.h>

#include <gtest/gtest.h>

#include <grpc/grpc.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>

#include "src/core/lib/slice/slice_internal.h"
#include "test/core/util/test_config.h"

static grpc_error_handle channel_init_func(grpc_channel_element* elem,
                                           grpc_channel_element_args* args) {
  EXPECT_EQ(args->channel_args->num_args, 1);
  EXPECT_EQ(args->channel_args->args[0].type, GRPC_ARG_INTEGER);
  EXPECT_STREQ(args->channel_args->args[0].key, "test_key");
  EXPECT_EQ(args->channel_args->args[0].value.integer, 42);
  EXPECT_TRUE(args->is_first);
  EXPECT_TRUE(args->is_last);
  *static_cast<int*>(elem->channel_data) = 0;
  return GRPC_ERROR_NONE;
}

static grpc_error_handle call_init_func(
    grpc_call_element* elem, const grpc_call_element_args* /*args*/) {
  ++*static_cast<int*>(elem->channel_data);
  *static_cast<int*>(elem->call_data) = 0;
  return GRPC_ERROR_NONE;
}

static void channel_destroy_func(grpc_channel_element* /*elem*/) {}

static void call_destroy_func(grpc_call_element* elem,
                              const grpc_call_final_info* /*final_info*/,
                              grpc_closure* /*ignored*/) {
  ++*static_cast<int*>(elem->channel_data);
}

static void call_func(grpc_call_element* elem,
                      grpc_transport_stream_op_batch* /*op*/) {
  ++*static_cast<int*>(elem->call_data);
}

static void channel_func(grpc_channel_element* elem,
                         grpc_transport_op* /*op*/) {
  ++*static_cast<int*>(elem->channel_data);
}

static void free_channel(void* arg, grpc_error_handle /*error*/) {
  grpc_channel_stack_destroy(static_cast<grpc_channel_stack*>(arg));
  gpr_free(arg);
}

static void free_call(void* arg, grpc_error_handle /*error*/) {
  grpc_call_stack_destroy(static_cast<grpc_call_stack*>(arg), nullptr, nullptr);
  gpr_free(arg);
}

TEST(ChannelStackTest, CreateChannelStack) {
  const grpc_channel_filter filter = {
      call_func,
      nullptr,
      channel_func,
      sizeof(int),
      call_init_func,
      grpc_call_stack_ignore_set_pollset_or_pollset_set,
      call_destroy_func,
      sizeof(int),
      channel_init_func,
      grpc_channel_stack_no_post_init,
      channel_destroy_func,
      grpc_channel_next_get_info,
      "some_test_filter"};
  const grpc_channel_filter* filters = &filter;
  grpc_channel_stack* channel_stack;
  grpc_call_stack* call_stack;
  grpc_channel_element* channel_elem;
  grpc_call_element* call_elem;
  grpc_arg arg;
  grpc_channel_args chan_args;
  int* channel_data;
  int* call_data;
  grpc_core::ExecCtx exec_ctx;
  grpc_slice path = grpc_slice_from_static_string("/service/method");

  arg.type = GRPC_ARG_INTEGER;
  arg.key = const_cast<char*>("test_key");
  arg.value.integer = 42;

  chan_args.num_args = 1;
  chan_args.args = &arg;

  channel_stack = static_cast<grpc_channel_stack*>(
      gpr_malloc(grpc_channel_stack_size(&filters, 1)));
  ASSERT_TRUE(GRPC_LOG_IF_ERROR(
      "grpc_channel_stack_init",
      grpc_channel_stack_init(1, free_channel, channel_stack, &filters, 1,
                              &chan_args, "test", channel_stack)));
  EXPECT_EQ(channel_stack->count, 1);
  channel_elem = grpc_channel_stack_element(channel_stack, 0);
  channel_data = static_cast<int*>(channel_elem->channel_data);
  EXPECT_EQ(*channel_data, 0);

  call_stack =
      static_cast<grpc_call_stack*>(gpr_malloc(channel_stack->call_stack_size));
  const grpc_call_element_args args = {
      call_stack,                        /* call_stack */
      nullptr,                           /* server_transport_data */
      nullptr,                           /* context */
      path,                              /* path */
      gpr_get_cycle_counter(),           /* start_time */
      grpc_core::Timestamp::InfFuture(), /* deadline */
      nullptr,                           /* arena */
      nullptr,                           /* call_combiner */
  };
  grpc_error_handle error =
      grpc_call_stack_init(channel_stack, 1, free_call, call_stack, &args);
  ASSERT_TRUE(GRPC_ERROR_IS_NONE(error)) << grpc_error_std_string(error);
  EXPECT_EQ(call_stack->count, 1);
  call_elem = grpc_call_stack_element(call_stack, 0);
  EXPECT_EQ(call_elem->filter, channel_elem->filter);
  EXPECT_EQ(call_elem->channel_data, channel_elem->channel_data);
  call_data = static_cast<int*>(call_elem->call_data);
  EXPECT_EQ(*call_data, 0);
  EXPECT_EQ(*channel_data, 1);

  GRPC_CALL_STACK_UNREF(call_stack, "done");
  grpc_core::ExecCtx::Get()->Flush();
  EXPECT_EQ(*channel_data, 2);

  GRPC_CHANNEL_STACK_UNREF(channel_stack, "done");

  grpc_slice_unref_internal(path);
}

int main(int argc, char** argv) {
  grpc::testing::TestEnvironment env(&argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  grpc::testing::TestGrpcScope grpc_scope;
  return RUN_ALL_TESTS();
}
