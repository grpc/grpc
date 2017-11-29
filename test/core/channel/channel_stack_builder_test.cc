/*
 *
 * Copyright 2017 gRPC authors.
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

#include "src/core/lib/channel/channel_stack_builder.h"

#include <limits.h>
#include <string.h>

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>

#include "src/core/lib/slice/slice_internal.h"
#include "src/core/lib/surface/channel_init.h"
#include "test/core/util/test_config.h"

static grpc_error* channel_init_func(grpc_exec_ctx* exec_ctx,
                                     grpc_channel_element* elem,
                                     grpc_channel_element_args* args) {
  return GRPC_ERROR_NONE;
}

static grpc_error* call_init_func(grpc_exec_ctx* exec_ctx,
                                  grpc_call_element* elem,
                                  const grpc_call_element_args* args) {
  return GRPC_ERROR_NONE;
}

static void channel_destroy_func(grpc_exec_ctx* exec_ctx,
                                 grpc_channel_element* elem) {}

static void call_destroy_func(grpc_exec_ctx* exec_ctx, grpc_call_element* elem,
                              const grpc_call_final_info* final_info,
                              grpc_closure* ignored) {}

static void call_func(grpc_exec_ctx* exec_ctx, grpc_call_element* elem,
                      grpc_transport_stream_op_batch* op) {}

static void channel_func(grpc_exec_ctx* exec_ctx, grpc_channel_element* elem,
                         grpc_transport_op* op) {
  if (op->disconnect_with_error != GRPC_ERROR_NONE) {
    GRPC_ERROR_UNREF(op->disconnect_with_error);
  }
  GRPC_CLOSURE_SCHED(exec_ctx, op->on_consumed, GRPC_ERROR_NONE);
}

bool g_replacement_fn_called = false;
bool g_original_fn_called = false;
void set_arg_once_fn(grpc_channel_stack* channel_stack,
                     grpc_channel_element* elem, void* arg) {
  bool* called = static_cast<bool*>(arg);
  // Make sure this function is only called once per arg.
  GPR_ASSERT(*called == false);
  *called = true;
}

static void test_channel_stack_builder_filter_replace(void) {
  grpc_channel* channel =
      grpc_insecure_channel_create("target name isn't used", nullptr, nullptr);
  GPR_ASSERT(channel != nullptr);
  // Make sure the high priority filter has been created.
  GPR_ASSERT(g_replacement_fn_called);
  // ... and that the low priority one hasn't.
  GPR_ASSERT(!g_original_fn_called);
  grpc_channel_destroy(channel);
}

const grpc_channel_filter replacement_filter = {
    call_func,
    channel_func,
    0,
    call_init_func,
    grpc_call_stack_ignore_set_pollset_or_pollset_set,
    call_destroy_func,
    0,
    channel_init_func,
    channel_destroy_func,
    grpc_channel_next_get_info,
    "filter_name"};

const grpc_channel_filter original_filter = {
    call_func,
    channel_func,
    0,
    call_init_func,
    grpc_call_stack_ignore_set_pollset_or_pollset_set,
    call_destroy_func,
    0,
    channel_init_func,
    channel_destroy_func,
    grpc_channel_next_get_info,
    "filter_name"};

static bool add_replacement_filter(grpc_exec_ctx* exec_ctx,
                                   grpc_channel_stack_builder* builder,
                                   void* arg) {
  const grpc_channel_filter* filter =
      static_cast<const grpc_channel_filter*>(arg);
  // Get rid of any other version of the filter, as determined by having the
  // same name.
  GPR_ASSERT(grpc_channel_stack_builder_remove_filter(builder, filter->name));
  return grpc_channel_stack_builder_prepend_filter(
      builder, filter, set_arg_once_fn, &g_replacement_fn_called);
}

static bool add_original_filter(grpc_exec_ctx* exec_ctx,
                                grpc_channel_stack_builder* builder,
                                void* arg) {
  return grpc_channel_stack_builder_prepend_filter(
      builder, (const grpc_channel_filter*)arg, set_arg_once_fn,
      &g_original_fn_called);
}

static void init_plugin(void) {
  grpc_channel_init_register_stage(GRPC_CLIENT_CHANNEL, INT_MAX,
                                   add_original_filter,
                                   (void*)&original_filter);
  grpc_channel_init_register_stage(GRPC_CLIENT_CHANNEL, INT_MAX,
                                   add_replacement_filter,
                                   (void*)&replacement_filter);
}

static void destroy_plugin(void) {}

int main(int argc, char** argv) {
  grpc_test_init(argc, argv);
  grpc_register_plugin(init_plugin, destroy_plugin);
  grpc_init();
  test_channel_stack_builder_filter_replace();
  grpc_shutdown();
  return 0;
}
