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

/* This benchmark exists to ensure that the benchmark integration is
 * working */

#include <benchmark/benchmark.h>
#include <string.h>
#include <sstream>

#include <grpc/grpc.h>
#include <grpc/support/alloc.h>
#include <grpc/support/string_util.h>
#include <grpcpp/channel.h>
#include <grpcpp/support/channel_arguments.h>

#include "src/core/ext/filters/client_channel/client_channel.h"
#include "src/core/ext/filters/deadline/deadline_filter.h"
#include "src/core/ext/filters/http/client/http_client_filter.h"
#include "src/core/ext/filters/http/message_compress/message_compress_filter.h"
#include "src/core/ext/filters/http/server/http_server_filter.h"
#include "src/core/ext/filters/load_reporting/server_load_reporting_filter.h"
#include "src/core/ext/filters/message_size/message_size_filter.h"
#include "src/core/lib/channel/channel_stack.h"
#include "src/core/lib/channel/connected_channel.h"
#include "src/core/lib/iomgr/call_combiner.h"
#include "src/core/lib/profiling/timers.h"
#include "src/core/lib/surface/channel.h"
#include "src/core/lib/transport/transport_impl.h"

#include "src/cpp/client/create_channel_internal.h"
#include "src/proto/grpc/testing/echo.grpc.pb.h"
#include "test/cpp/microbenchmarks/helpers.h"
#include "test/cpp/util/test_config.h"

auto& force_library_initialization = Library::get();

void BM_Zalloc(benchmark::State& state) {
  // speed of light for call creation is zalloc, so benchmark a few interesting
  // sizes
  TrackCounters track_counters;
  size_t sz = state.range(0);
  while (state.KeepRunning()) {
    gpr_free(gpr_zalloc(sz));
  }
  track_counters.Finish(state);
}
BENCHMARK(BM_Zalloc)
    ->Arg(64)
    ->Arg(128)
    ->Arg(256)
    ->Arg(512)
    ->Arg(1024)
    ->Arg(1536)
    ->Arg(2048)
    ->Arg(3072)
    ->Arg(4096)
    ->Arg(5120)
    ->Arg(6144)
    ->Arg(7168);

////////////////////////////////////////////////////////////////////////////////
// Benchmarks creating full stacks

class BaseChannelFixture {
 public:
  BaseChannelFixture(grpc_channel* channel) : channel_(channel) {}
  ~BaseChannelFixture() { grpc_channel_destroy(channel_); }

  grpc_channel* channel() const { return channel_; }

 private:
  grpc_channel* const channel_;
};

class InsecureChannel : public BaseChannelFixture {
 public:
  InsecureChannel()
      : BaseChannelFixture(
            grpc_insecure_channel_create("localhost:1234", nullptr, nullptr)) {}
};

class LameChannel : public BaseChannelFixture {
 public:
  LameChannel()
      : BaseChannelFixture(grpc_lame_client_channel_create(
            "localhost:1234", GRPC_STATUS_UNAUTHENTICATED, "blah")) {}
};

template <class Fixture>
static void BM_CallCreateDestroy(benchmark::State& state) {
  TrackCounters track_counters;
  Fixture fixture;
  grpc_completion_queue* cq = grpc_completion_queue_create_for_next(nullptr);
  gpr_timespec deadline = gpr_inf_future(GPR_CLOCK_MONOTONIC);
  void* method_hdl = grpc_channel_register_call(fixture.channel(), "/foo/bar",
                                                nullptr, nullptr);
  while (state.KeepRunning()) {
    grpc_call_unref(grpc_channel_create_registered_call(
        fixture.channel(), nullptr, GRPC_PROPAGATE_DEFAULTS, cq, method_hdl,
        deadline, nullptr));
  }
  grpc_completion_queue_destroy(cq);
  track_counters.Finish(state);
}

BENCHMARK_TEMPLATE(BM_CallCreateDestroy, InsecureChannel);
BENCHMARK_TEMPLATE(BM_CallCreateDestroy, LameChannel);

////////////////////////////////////////////////////////////////////////////////
// Benchmarks isolating individual filters

static void* tag(int i) {
  return reinterpret_cast<void*>(static_cast<intptr_t>(i));
}

static void BM_LameChannelCallCreateCpp(benchmark::State& state) {
  TrackCounters track_counters;
  auto stub =
      grpc::testing::EchoTestService::NewStub(grpc::CreateChannelInternal(
          "", grpc_lame_client_channel_create(
                  "localhost:1234", GRPC_STATUS_UNAUTHENTICATED, "blah")));
  grpc::CompletionQueue cq;
  grpc::testing::EchoRequest send_request;
  grpc::testing::EchoResponse recv_response;
  grpc::Status recv_status;
  while (state.KeepRunning()) {
    GPR_TIMER_SCOPE("BenchmarkCycle", 0);
    grpc::ClientContext cli_ctx;
    auto reader = stub->AsyncEcho(&cli_ctx, send_request, &cq);
    reader->Finish(&recv_response, &recv_status, tag(0));
    void* t;
    bool ok;
    GPR_ASSERT(cq.Next(&t, &ok));
    GPR_ASSERT(ok);
  }
  track_counters.Finish(state);
}
BENCHMARK(BM_LameChannelCallCreateCpp);

static void do_nothing(void* ignored) {}

static void BM_LameChannelCallCreateCore(benchmark::State& state) {
  TrackCounters track_counters;

  grpc_channel* channel;
  grpc_completion_queue* cq;
  grpc_metadata_array initial_metadata_recv;
  grpc_metadata_array trailing_metadata_recv;
  grpc_byte_buffer* response_payload_recv = nullptr;
  grpc_status_code status;
  grpc_slice details;
  grpc::testing::EchoRequest send_request;
  grpc_slice send_request_slice =
      grpc_slice_new(&send_request, sizeof(send_request), do_nothing);

  channel = grpc_lame_client_channel_create(
      "localhost:1234", GRPC_STATUS_UNAUTHENTICATED, "blah");
  cq = grpc_completion_queue_create_for_next(nullptr);
  void* rc = grpc_channel_register_call(
      channel, "/grpc.testing.EchoTestService/Echo", nullptr, nullptr);
  while (state.KeepRunning()) {
    GPR_TIMER_SCOPE("BenchmarkCycle", 0);
    grpc_call* call = grpc_channel_create_registered_call(
        channel, nullptr, GRPC_PROPAGATE_DEFAULTS, cq, rc,
        gpr_inf_future(GPR_CLOCK_REALTIME), nullptr);
    grpc_metadata_array_init(&initial_metadata_recv);
    grpc_metadata_array_init(&trailing_metadata_recv);
    grpc_byte_buffer* request_payload_send =
        grpc_raw_byte_buffer_create(&send_request_slice, 1);

    // Fill in call ops
    grpc_op ops[6];
    memset(ops, 0, sizeof(ops));
    grpc_op* op = ops;
    op->op = GRPC_OP_SEND_INITIAL_METADATA;
    op->data.send_initial_metadata.count = 0;
    op++;
    op->op = GRPC_OP_SEND_MESSAGE;
    op->data.send_message.send_message = request_payload_send;
    op++;
    op->op = GRPC_OP_SEND_CLOSE_FROM_CLIENT;
    op++;
    op->op = GRPC_OP_RECV_INITIAL_METADATA;
    op->data.recv_initial_metadata.recv_initial_metadata =
        &initial_metadata_recv;
    op++;
    op->op = GRPC_OP_RECV_MESSAGE;
    op->data.recv_message.recv_message = &response_payload_recv;
    op++;
    op->op = GRPC_OP_RECV_STATUS_ON_CLIENT;
    op->data.recv_status_on_client.trailing_metadata = &trailing_metadata_recv;
    op->data.recv_status_on_client.status = &status;
    op->data.recv_status_on_client.status_details = &details;
    op++;

    GPR_ASSERT(GRPC_CALL_OK == grpc_call_start_batch(call, ops,
                                                     (size_t)(op - ops),
                                                     (void*)1, nullptr));
    grpc_event ev = grpc_completion_queue_next(
        cq, gpr_inf_future(GPR_CLOCK_REALTIME), nullptr);
    GPR_ASSERT(ev.type != GRPC_QUEUE_SHUTDOWN);
    GPR_ASSERT(ev.success != 0);
    grpc_call_unref(call);
    grpc_byte_buffer_destroy(request_payload_send);
    grpc_byte_buffer_destroy(response_payload_recv);
    grpc_metadata_array_destroy(&initial_metadata_recv);
    grpc_metadata_array_destroy(&trailing_metadata_recv);
  }
  grpc_channel_destroy(channel);
  grpc_completion_queue_destroy(cq);
  grpc_slice_unref(send_request_slice);
  track_counters.Finish(state);
}
BENCHMARK(BM_LameChannelCallCreateCore);

static void BM_LameChannelCallCreateCoreSeparateBatch(benchmark::State& state) {
  TrackCounters track_counters;

  grpc_channel* channel;
  grpc_completion_queue* cq;
  grpc_metadata_array initial_metadata_recv;
  grpc_metadata_array trailing_metadata_recv;
  grpc_byte_buffer* response_payload_recv = nullptr;
  grpc_status_code status;
  grpc_slice details;
  grpc::testing::EchoRequest send_request;
  grpc_slice send_request_slice =
      grpc_slice_new(&send_request, sizeof(send_request), do_nothing);

  channel = grpc_lame_client_channel_create(
      "localhost:1234", GRPC_STATUS_UNAUTHENTICATED, "blah");
  cq = grpc_completion_queue_create_for_next(nullptr);
  void* rc = grpc_channel_register_call(
      channel, "/grpc.testing.EchoTestService/Echo", nullptr, nullptr);
  while (state.KeepRunning()) {
    GPR_TIMER_SCOPE("BenchmarkCycle", 0);
    grpc_call* call = grpc_channel_create_registered_call(
        channel, nullptr, GRPC_PROPAGATE_DEFAULTS, cq, rc,
        gpr_inf_future(GPR_CLOCK_REALTIME), nullptr);
    grpc_metadata_array_init(&initial_metadata_recv);
    grpc_metadata_array_init(&trailing_metadata_recv);
    grpc_byte_buffer* request_payload_send =
        grpc_raw_byte_buffer_create(&send_request_slice, 1);

    // Fill in call ops
    grpc_op ops[3];
    memset(ops, 0, sizeof(ops));
    grpc_op* op = ops;
    op->op = GRPC_OP_SEND_INITIAL_METADATA;
    op->data.send_initial_metadata.count = 0;
    op++;
    op->op = GRPC_OP_SEND_MESSAGE;
    op->data.send_message.send_message = request_payload_send;
    op++;
    op->op = GRPC_OP_SEND_CLOSE_FROM_CLIENT;
    op++;
    GPR_ASSERT(GRPC_CALL_OK == grpc_call_start_batch(call, ops,
                                                     (size_t)(op - ops),
                                                     (void*)nullptr, nullptr));
    memset(ops, 0, sizeof(ops));
    op = ops;
    op->op = GRPC_OP_RECV_INITIAL_METADATA;
    op->data.recv_initial_metadata.recv_initial_metadata =
        &initial_metadata_recv;
    op++;
    op->op = GRPC_OP_RECV_MESSAGE;
    op->data.recv_message.recv_message = &response_payload_recv;
    op++;
    op->op = GRPC_OP_RECV_STATUS_ON_CLIENT;
    op->data.recv_status_on_client.trailing_metadata = &trailing_metadata_recv;
    op->data.recv_status_on_client.status = &status;
    op->data.recv_status_on_client.status_details = &details;
    op++;

    GPR_ASSERT(GRPC_CALL_OK == grpc_call_start_batch(call, ops,
                                                     (size_t)(op - ops),
                                                     (void*)1, nullptr));
    grpc_event ev = grpc_completion_queue_next(
        cq, gpr_inf_future(GPR_CLOCK_REALTIME), nullptr);
    GPR_ASSERT(ev.type != GRPC_QUEUE_SHUTDOWN);
    GPR_ASSERT(ev.success == 0);
    ev = grpc_completion_queue_next(cq, gpr_inf_future(GPR_CLOCK_REALTIME),
                                    nullptr);
    GPR_ASSERT(ev.type != GRPC_QUEUE_SHUTDOWN);
    GPR_ASSERT(ev.success != 0);
    grpc_call_unref(call);
    grpc_byte_buffer_destroy(request_payload_send);
    grpc_byte_buffer_destroy(response_payload_recv);
    grpc_metadata_array_destroy(&initial_metadata_recv);
    grpc_metadata_array_destroy(&trailing_metadata_recv);
  }
  grpc_channel_destroy(channel);
  grpc_completion_queue_destroy(cq);
  grpc_slice_unref(send_request_slice);
  track_counters.Finish(state);
}
BENCHMARK(BM_LameChannelCallCreateCoreSeparateBatch);

////////////////////////////////////////////////////////////////////////////////
// Benchmarks isolating grpc_call

namespace isolated_call_filter {

typedef struct {
  grpc_call_combiner* call_combiner;
} call_data;

static void StartTransportStreamOp(grpc_call_element* elem,
                                   grpc_transport_stream_op_batch* op) {
  call_data* calld = static_cast<call_data*>(elem->call_data);
  if (op->recv_initial_metadata) {
    GRPC_CALL_COMBINER_START(
        calld->call_combiner,
        op->payload->recv_initial_metadata.recv_initial_metadata_ready,
        GRPC_ERROR_NONE, "recv_initial_metadata");
  }
  if (op->recv_message) {
    GRPC_CALL_COMBINER_START(calld->call_combiner,
                             op->payload->recv_message.recv_message_ready,
                             GRPC_ERROR_NONE, "recv_message");
  }
  GRPC_CLOSURE_SCHED(op->on_complete, GRPC_ERROR_NONE);
}

static void StartTransportOp(grpc_channel_element* elem,
                             grpc_transport_op* op) {
  if (op->disconnect_with_error != GRPC_ERROR_NONE) {
    GRPC_ERROR_UNREF(op->disconnect_with_error);
  }
  GRPC_CLOSURE_SCHED(op->on_consumed, GRPC_ERROR_NONE);
}

static grpc_error* InitCallElem(grpc_call_element* elem,
                                const grpc_call_element_args* args) {
  call_data* calld = static_cast<call_data*>(elem->call_data);
  calld->call_combiner = args->call_combiner;
  return GRPC_ERROR_NONE;
}

static void SetPollsetOrPollsetSet(grpc_call_element* elem,
                                   grpc_polling_entity* pollent) {}

static void DestroyCallElem(grpc_call_element* elem,
                            const grpc_call_final_info* final_info,
                            grpc_closure* then_sched_closure) {
  GRPC_CLOSURE_SCHED(then_sched_closure, GRPC_ERROR_NONE);
}

grpc_error* InitChannelElem(grpc_channel_element* elem,
                            grpc_channel_element_args* args) {
  return GRPC_ERROR_NONE;
}

void DestroyChannelElem(grpc_channel_element* elem) {}

void GetChannelInfo(grpc_channel_element* elem,
                    const grpc_channel_info* channel_info) {}

static const grpc_channel_filter isolated_call_filter = {
    StartTransportStreamOp,
    StartTransportOp,
    sizeof(call_data),
    InitCallElem,
    SetPollsetOrPollsetSet,
    DestroyCallElem,
    0,
    InitChannelElem,
    DestroyChannelElem,
    GetChannelInfo,
    "isolated_call_filter"};
}  // namespace isolated_call_filter

class IsolatedCallFixture : public TrackCounters {
 public:
  IsolatedCallFixture() {
    grpc_channel_stack_builder* builder = grpc_channel_stack_builder_create();
    grpc_channel_stack_builder_set_name(builder, "dummy");
    grpc_channel_stack_builder_set_target(builder, "dummy_target");
    GPR_ASSERT(grpc_channel_stack_builder_append_filter(
        builder, &isolated_call_filter::isolated_call_filter, nullptr,
        nullptr));
    {
      grpc_core::ExecCtx exec_ctx;
      channel_ = grpc_channel_create_with_builder(builder, GRPC_CLIENT_CHANNEL);
    }
    cq_ = grpc_completion_queue_create_for_next(nullptr);
  }

  void Finish(benchmark::State& state) {
    grpc_completion_queue_destroy(cq_);
    grpc_channel_destroy(channel_);
    TrackCounters::Finish(state);
  }

  grpc_channel* channel() const { return channel_; }
  grpc_completion_queue* cq() const { return cq_; }

 private:
  grpc_completion_queue* cq_;
  grpc_channel* channel_;
};

static void BM_IsolatedCall_NoOp(benchmark::State& state) {
  IsolatedCallFixture fixture;
  gpr_timespec deadline = gpr_inf_future(GPR_CLOCK_MONOTONIC);
  void* method_hdl = grpc_channel_register_call(fixture.channel(), "/foo/bar",
                                                nullptr, nullptr);
  while (state.KeepRunning()) {
    GPR_TIMER_SCOPE("BenchmarkCycle", 0);
    grpc_call_unref(grpc_channel_create_registered_call(
        fixture.channel(), nullptr, GRPC_PROPAGATE_DEFAULTS, fixture.cq(),
        method_hdl, deadline, nullptr));
  }
  fixture.Finish(state);
}
BENCHMARK(BM_IsolatedCall_NoOp);

static void BM_IsolatedCall_Unary(benchmark::State& state) {
  IsolatedCallFixture fixture;
  gpr_timespec deadline = gpr_inf_future(GPR_CLOCK_MONOTONIC);
  void* method_hdl = grpc_channel_register_call(fixture.channel(), "/foo/bar",
                                                nullptr, nullptr);
  grpc_slice slice = grpc_slice_from_static_string("hello world");
  grpc_byte_buffer* send_message = grpc_raw_byte_buffer_create(&slice, 1);
  grpc_byte_buffer* recv_message = nullptr;
  grpc_status_code status_code;
  grpc_slice status_details = grpc_empty_slice();
  grpc_metadata_array recv_initial_metadata;
  grpc_metadata_array_init(&recv_initial_metadata);
  grpc_metadata_array recv_trailing_metadata;
  grpc_metadata_array_init(&recv_trailing_metadata);
  grpc_op ops[6];
  memset(ops, 0, sizeof(ops));
  ops[0].op = GRPC_OP_SEND_INITIAL_METADATA;
  ops[1].op = GRPC_OP_SEND_MESSAGE;
  ops[1].data.send_message.send_message = send_message;
  ops[2].op = GRPC_OP_SEND_CLOSE_FROM_CLIENT;
  ops[3].op = GRPC_OP_RECV_INITIAL_METADATA;
  ops[3].data.recv_initial_metadata.recv_initial_metadata =
      &recv_initial_metadata;
  ops[4].op = GRPC_OP_RECV_MESSAGE;
  ops[4].data.recv_message.recv_message = &recv_message;
  ops[5].op = GRPC_OP_RECV_STATUS_ON_CLIENT;
  ops[5].data.recv_status_on_client.status = &status_code;
  ops[5].data.recv_status_on_client.status_details = &status_details;
  ops[5].data.recv_status_on_client.trailing_metadata = &recv_trailing_metadata;
  while (state.KeepRunning()) {
    GPR_TIMER_SCOPE("BenchmarkCycle", 0);
    grpc_call* call = grpc_channel_create_registered_call(
        fixture.channel(), nullptr, GRPC_PROPAGATE_DEFAULTS, fixture.cq(),
        method_hdl, deadline, nullptr);
    grpc_call_start_batch(call, ops, 6, tag(1), nullptr);
    grpc_completion_queue_next(fixture.cq(),
                               gpr_inf_future(GPR_CLOCK_MONOTONIC), nullptr);
    grpc_call_unref(call);
  }
  fixture.Finish(state);
  grpc_metadata_array_destroy(&recv_initial_metadata);
  grpc_metadata_array_destroy(&recv_trailing_metadata);
  grpc_byte_buffer_destroy(send_message);
}
BENCHMARK(BM_IsolatedCall_Unary);

static void BM_IsolatedCall_StreamingSend(benchmark::State& state) {
  IsolatedCallFixture fixture;
  gpr_timespec deadline = gpr_inf_future(GPR_CLOCK_MONOTONIC);
  void* method_hdl = grpc_channel_register_call(fixture.channel(), "/foo/bar",
                                                nullptr, nullptr);
  grpc_slice slice = grpc_slice_from_static_string("hello world");
  grpc_byte_buffer* send_message = grpc_raw_byte_buffer_create(&slice, 1);
  grpc_metadata_array recv_initial_metadata;
  grpc_metadata_array_init(&recv_initial_metadata);
  grpc_metadata_array recv_trailing_metadata;
  grpc_metadata_array_init(&recv_trailing_metadata);
  grpc_op ops[2];
  memset(ops, 0, sizeof(ops));
  ops[0].op = GRPC_OP_SEND_INITIAL_METADATA;
  ops[1].op = GRPC_OP_RECV_INITIAL_METADATA;
  ops[1].data.recv_initial_metadata.recv_initial_metadata =
      &recv_initial_metadata;
  grpc_call* call = grpc_channel_create_registered_call(
      fixture.channel(), nullptr, GRPC_PROPAGATE_DEFAULTS, fixture.cq(),
      method_hdl, deadline, nullptr);
  grpc_call_start_batch(call, ops, 2, tag(1), nullptr);
  grpc_completion_queue_next(fixture.cq(), gpr_inf_future(GPR_CLOCK_MONOTONIC),
                             nullptr);
  memset(ops, 0, sizeof(ops));
  ops[0].op = GRPC_OP_SEND_MESSAGE;
  ops[0].data.send_message.send_message = send_message;
  while (state.KeepRunning()) {
    GPR_TIMER_SCOPE("BenchmarkCycle", 0);
    grpc_call_start_batch(call, ops, 1, tag(2), nullptr);
    grpc_completion_queue_next(fixture.cq(),
                               gpr_inf_future(GPR_CLOCK_MONOTONIC), nullptr);
  }
  grpc_call_unref(call);
  fixture.Finish(state);
  grpc_metadata_array_destroy(&recv_initial_metadata);
  grpc_metadata_array_destroy(&recv_trailing_metadata);
  grpc_byte_buffer_destroy(send_message);
}
BENCHMARK(BM_IsolatedCall_StreamingSend);

// Some distros have RunSpecifiedBenchmarks under the benchmark namespace,
// and others do not. This allows us to support both modes.
namespace benchmark {
void RunTheBenchmarksNamespaced() { RunSpecifiedBenchmarks(); }
}  // namespace benchmark

int main(int argc, char** argv) {
  ::benchmark::Initialize(&argc, argv);
  ::grpc::testing::InitTest(&argc, &argv, false);
  benchmark::RunTheBenchmarksNamespaced();
  return 0;
}
