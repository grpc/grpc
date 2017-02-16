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

/* This benchmark exists to ensure that the benchmark integration is
 * working */

#include <grpc++/support/channel_arguments.h>
#include <grpc/grpc.h>
#include <grpc/support/alloc.h>

extern "C" {
#include "src/core/ext/client_channel/client_channel.h"
#include "src/core/lib/channel/channel_stack.h"
}

#include "third_party/benchmark/include/benchmark/benchmark.h"

static struct Init {
  Init() { grpc_init(); }
  ~Init() { grpc_shutdown(); }
} g_init;

static void BM_InsecureChannelWithDefaults(benchmark::State& state) {
  grpc_channel* channel =
      grpc_insecure_channel_create("localhost:12345", NULL, NULL);
  grpc_completion_queue* cq = grpc_completion_queue_create(NULL);
  grpc_slice method = grpc_slice_from_static_string("/foo/bar");
  gpr_timespec deadline = gpr_inf_future(GPR_CLOCK_MONOTONIC);
  while (state.KeepRunning()) {
    grpc_call_destroy(grpc_channel_create_call(channel, NULL,
                                               GRPC_PROPAGATE_DEFAULTS, cq,
                                               method, NULL, deadline, NULL));
  }
  grpc_channel_destroy(channel);
}
BENCHMARK(BM_InsecureChannelWithDefaults);

static void FilterDestroy(grpc_exec_ctx* exec_ctx, void* arg,
                          grpc_error* error) {
  gpr_free(arg);
}

static void DoNothing(grpc_exec_ctx* exec_ctx, void* arg, grpc_error* error) {}

class FakeClientChannelFactory : public grpc_client_channel_factory {
 public:
  FakeClientChannelFactory() { vtable = &vtable_; }

 private:
  static void NoRef(grpc_client_channel_factory* factory) {}
  static void NoUnref(grpc_exec_ctx* exec_ctx,
                      grpc_client_channel_factory* factory) {}
  static grpc_subchannel* CreateSubchannel(grpc_exec_ctx* exec_ctx,
                                           grpc_client_channel_factory* factory,
                                           const grpc_subchannel_args* args) {
    return nullptr;
  }
  static grpc_channel* CreateClientChannel(grpc_exec_ctx* exec_ctx,
                                           grpc_client_channel_factory* factory,
                                           const char* target,
                                           grpc_client_channel_type type,
                                           const grpc_channel_args* args) {
    return nullptr;
  }

  static const grpc_client_channel_factory_vtable vtable_;
};

const grpc_client_channel_factory_vtable FakeClientChannelFactory::vtable_ = {
    NoRef, NoUnref, CreateSubchannel, CreateClientChannel};

static grpc_arg StringArg(const char* key, const char* value) {
  grpc_arg a;
  a.type = GRPC_ARG_STRING;
  a.key = const_cast<char*>(key);
  a.value.string = const_cast<char*>(value);
  return a;
}

template <const grpc_channel_filter* kFilter>
static void BM_FilterInitDestroy(benchmark::State& state) {
  std::vector<grpc_arg> args;
  FakeClientChannelFactory fake_client_channel_factory;
  args.push_back(grpc_client_channel_factory_create_channel_arg(
      &fake_client_channel_factory));
  args.push_back(StringArg(GRPC_ARG_SERVER_URI, "localhost"));

  grpc_channel_args channel_args = {args.size(), &args[0]};

  const grpc_channel_filter* filters[] = {kFilter};
  grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
  size_t channel_size =
      grpc_channel_stack_size(filters, GPR_ARRAY_SIZE(filters));
  grpc_channel_stack* channel_stack =
      static_cast<grpc_channel_stack*>(gpr_malloc(channel_size));
  GPR_ASSERT(GRPC_LOG_IF_ERROR(
      "call_stack_init",
      grpc_channel_stack_init(&exec_ctx, 1, FilterDestroy, channel_stack,
                              filters, GPR_ARRAY_SIZE(filters), &channel_args,
                              NULL, "CHANNEL", channel_stack)));
  grpc_exec_ctx_flush(&exec_ctx);
  grpc_call_stack* call_stack =
      static_cast<grpc_call_stack*>(gpr_malloc(channel_stack->call_stack_size));
  gpr_timespec deadline = gpr_inf_future(GPR_CLOCK_MONOTONIC);
  gpr_timespec start_time = gpr_now(GPR_CLOCK_MONOTONIC);
  grpc_slice method = grpc_slice_from_static_string("/foo/bar");
  grpc_call_final_info final_info;
  while (state.KeepRunning()) {
    GRPC_ERROR_UNREF(grpc_call_stack_init(&exec_ctx, channel_stack, 1,
                                          DoNothing, NULL, NULL, NULL, method,
                                          start_time, deadline, call_stack));
    grpc_call_stack_destroy(&exec_ctx, call_stack, &final_info, NULL);
    grpc_exec_ctx_flush(&exec_ctx);
  }
  GRPC_CHANNEL_STACK_UNREF(&exec_ctx, channel_stack, "done");
  grpc_exec_ctx_finish(&exec_ctx);
}

BENCHMARK_TEMPLATE(BM_FilterInitDestroy, &grpc_client_channel_filter);

BENCHMARK_MAIN();
