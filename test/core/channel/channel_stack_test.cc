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

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>

#include "src/core/lib/slice/slice_internal.h"
#include "test/core/util/test_config.h"

static grpc_error* channel_init_func(grpc_channel_element* elem,
                                     grpc_channel_element_args* args) {
  GPR_ASSERT(args->channel_args->num_args == 1);
  GPR_ASSERT(args->channel_args->args[0].type == GRPC_ARG_INTEGER);
  GPR_ASSERT(0 == strcmp(args->channel_args->args[0].key, "test_key"));
  GPR_ASSERT(args->channel_args->args[0].value.integer == 42);
  GPR_ASSERT(args->is_first);
  GPR_ASSERT(args->is_last);
  *static_cast<int*>(elem->channel_data) = 0;
  return GRPC_ERROR_NONE;
}

static grpc_error* call_init_func(grpc_call_element* elem,
                                  const grpc_call_element_args* args) {
  ++*static_cast<int*>(elem->channel_data);
  *static_cast<int*>(elem->call_data) = 0;
  return GRPC_ERROR_NONE;
}

static void channel_destroy_func(grpc_channel_element* elem) {}

static void call_destroy_func(grpc_call_element* elem,
                              const grpc_call_final_info* final_info,
                              grpc_closure* ignored) {
  ++*static_cast<int*>(elem->channel_data);
}

static void call_func(grpc_call_element* elem,
                      grpc_transport_stream_op_batch* op) {
  ++*static_cast<int*>(elem->call_data);
}

static void channel_func(grpc_channel_element* elem, grpc_transport_op* op) {
  ++*static_cast<int*>(elem->channel_data);
}

static void free_channel(void* arg, grpc_error* error) {
  grpc_channel_stack_destroy(static_cast<grpc_channel_stack*>(arg));
  gpr_free(arg);
}

static void free_call(void* arg, grpc_error* error) {
  grpc_call_stack_destroy(static_cast<grpc_call_stack*>(arg), nullptr, nullptr);
  gpr_free(arg);
}

static void test_create_channel_stack(void) {
  const grpc_channel_filter filter = {
      call_func,
      channel_func,
      sizeof(int),
      call_init_func,
      grpc_call_stack_ignore_set_pollset_or_pollset_set,
      call_destroy_func,
      sizeof(int),
      channel_init_func,
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
  grpc_channel_stack_init(1, free_channel, channel_stack, &filters, 1,
                          &chan_args, nullptr, "test", channel_stack);
  GPR_ASSERT(channel_stack->count == 1);
  channel_elem = grpc_channel_stack_element(channel_stack, 0);
  channel_data = static_cast<int*>(channel_elem->channel_data);
  GPR_ASSERT(*channel_data == 0);

  call_stack =
      static_cast<grpc_call_stack*>(gpr_malloc(channel_stack->call_stack_size));
  const grpc_call_element_args args = {
      call_stack,                   /* call_stack */
      nullptr,                      /* server_transport_data */
      nullptr,                      /* context */
      path,                         /* path */
      gpr_now(GPR_CLOCK_MONOTONIC), /* start_time */
      GRPC_MILLIS_INF_FUTURE,       /* deadline */
      nullptr,                      /* arena */
      nullptr                       /* call_combiner */
  };
  grpc_error* error =
      grpc_call_stack_init(channel_stack, 1, free_call, call_stack, &args);
  GPR_ASSERT(error == GRPC_ERROR_NONE);
  GPR_ASSERT(call_stack->count == 1);
  call_elem = grpc_call_stack_element(call_stack, 0);
  GPR_ASSERT(call_elem->filter == channel_elem->filter);
  GPR_ASSERT(call_elem->channel_data == channel_elem->channel_data);
  call_data = static_cast<int*>(call_elem->call_data);
  GPR_ASSERT(*call_data == 0);
  GPR_ASSERT(*channel_data == 1);

  GRPC_CALL_STACK_UNREF(call_stack, "done");
  grpc_core::ExecCtx::Get()->Flush();
  GPR_ASSERT(*channel_data == 2);

  GRPC_CHANNEL_STACK_UNREF(channel_stack, "done");

  grpc_slice_unref_internal(path);
}

int main(int argc, char** argv) {
  grpc_test_init(argc, argv);
  grpc_init();
  test_create_channel_stack();
  grpc_shutdown();
  return 0;
}
