/*
 *
 * Copyright 2015, Google Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *     * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 * copyright notice, this list of conditions and the following disclaimer
 * in the documentation and/or other materials provided with the
 * distribution.
 *     * Neither the name of Google Inc. nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

#include "src/core/lib/channel/channel_stack.h"

#include <string.h>

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>

#include "src/core/lib/slice/slice_internal.h"
#include "test/core/util/test_config.h"

static grpc_error *channel_init_func(grpc_exec_ctx *exec_ctx,
                                     grpc_channel_element *elem,
                                     grpc_channel_element_args *args) {
  GPR_ASSERT(args->channel_args->num_args == 1);
  GPR_ASSERT(args->channel_args->args[0].type == GRPC_ARG_INTEGER);
  GPR_ASSERT(0 == strcmp(args->channel_args->args[0].key, "test_key"));
  GPR_ASSERT(args->channel_args->args[0].value.integer == 42);
  GPR_ASSERT(args->is_first);
  GPR_ASSERT(args->is_last);
  *(int *)(elem->channel_data) = 0;
  return GRPC_ERROR_NONE;
}

static grpc_error *call_init_func(grpc_exec_ctx *exec_ctx,
                                  grpc_call_element *elem,
                                  const grpc_call_element_args *args) {
  ++*(int *)(elem->channel_data);
  *(int *)(elem->call_data) = 0;
  return GRPC_ERROR_NONE;
}

static void channel_destroy_func(grpc_exec_ctx *exec_ctx,
                                 grpc_channel_element *elem) {}

static void call_destroy_func(grpc_exec_ctx *exec_ctx, grpc_call_element *elem,
                              const grpc_call_final_info *final_info,
                              grpc_closure *ignored) {
  ++*(int *)(elem->channel_data);
}

static void call_func(grpc_exec_ctx *exec_ctx, grpc_call_element *elem,
                      grpc_transport_stream_op_batch *op) {
  ++*(int *)(elem->call_data);
}

static void channel_func(grpc_exec_ctx *exec_ctx, grpc_channel_element *elem,
                         grpc_transport_op *op) {
  ++*(int *)(elem->channel_data);
}

static char *get_peer(grpc_exec_ctx *exec_ctx, grpc_call_element *elem) {
  return gpr_strdup("peer");
}

static void free_channel(grpc_exec_ctx *exec_ctx, void *arg,
                         grpc_error *error) {
  grpc_channel_stack_destroy(exec_ctx, arg);
  gpr_free(arg);
}

static void free_call(grpc_exec_ctx *exec_ctx, void *arg, grpc_error *error) {
  grpc_call_stack_destroy(exec_ctx, arg, NULL, NULL);
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
      get_peer,
      grpc_channel_next_get_info,
      "some_test_filter"};
  const grpc_channel_filter *filters = &filter;
  grpc_channel_stack *channel_stack;
  grpc_call_stack *call_stack;
  grpc_channel_element *channel_elem;
  grpc_call_element *call_elem;
  grpc_arg arg;
  grpc_channel_args chan_args;
  int *channel_data;
  int *call_data;
  grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
  grpc_slice path = grpc_slice_from_static_string("/service/method");

  arg.type = GRPC_ARG_INTEGER;
  arg.key = "test_key";
  arg.value.integer = 42;

  chan_args.num_args = 1;
  chan_args.args = &arg;

  channel_stack = gpr_malloc(grpc_channel_stack_size(&filters, 1));
  grpc_channel_stack_init(&exec_ctx, 1, free_channel, channel_stack, &filters,
                          1, &chan_args, NULL, "test", channel_stack);
  GPR_ASSERT(channel_stack->count == 1);
  channel_elem = grpc_channel_stack_element(channel_stack, 0);
  channel_data = (int *)channel_elem->channel_data;
  GPR_ASSERT(*channel_data == 0);

  call_stack = gpr_malloc(channel_stack->call_stack_size);
  const grpc_call_element_args args = {
      .call_stack = call_stack,
      .server_transport_data = NULL,
      .context = NULL,
      .path = path,
      .start_time = gpr_now(GPR_CLOCK_MONOTONIC),
      .deadline = gpr_inf_future(GPR_CLOCK_MONOTONIC),
      .arena = NULL};
  grpc_error *error = grpc_call_stack_init(&exec_ctx, channel_stack, 1,
                                           free_call, call_stack, &args);
  GPR_ASSERT(error == GRPC_ERROR_NONE);
  GPR_ASSERT(call_stack->count == 1);
  call_elem = grpc_call_stack_element(call_stack, 0);
  GPR_ASSERT(call_elem->filter == channel_elem->filter);
  GPR_ASSERT(call_elem->channel_data == channel_elem->channel_data);
  call_data = (int *)call_elem->call_data;
  GPR_ASSERT(*call_data == 0);
  GPR_ASSERT(*channel_data == 1);

  GRPC_CALL_STACK_UNREF(&exec_ctx, call_stack, "done");
  grpc_exec_ctx_flush(&exec_ctx);
  GPR_ASSERT(*channel_data == 2);

  GRPC_CHANNEL_STACK_UNREF(&exec_ctx, channel_stack, "done");

  grpc_slice_unref_internal(&exec_ctx, path);
  grpc_exec_ctx_finish(&exec_ctx);
}

int main(int argc, char **argv) {
  grpc_test_init(argc, argv);
  grpc_init();
  test_create_channel_stack();
  grpc_shutdown();
  return 0;
}
