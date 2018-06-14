/*
 *
 * Copyright 2018 gRPC authors.
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

#include "src/cpp/client/create_channel_internal.h"
#include "src/proto/grpc/testing/echo.grpc.pb.h"
#include "test/cpp/microbenchmarks/filter_helpers.h"
#include "test/cpp/util/test_config.h"

auto& force_library_initialization = Library::get();

// Test a filter's call stack init in isolation. FilterBM, in conjunction with
// FilterFixture, specifies the filter under test (use the FilterBM<> template
// to specify this).
// Note that there is some other work being done within the benchmarking loop,
// so the result of this microbenchmark is a little bloated.
template <class FilterBM>
static void BM_CallStackInit(benchmark::State& state) {
  // Setup for benchmark
  FilterBM bm_setup;
  struct DataForFilterBM data;
  bm_setup.Setup(&data);

  // Run the benchmark
  grpc_call_final_info final_info;
  while (state.KeepRunning()) {
    GPR_TIMER_SCOPE("BenchmarkCycle", 0);
    GRPC_ERROR_UNREF(
        grpc_call_stack_init(channel_stack, 1, DoNothing, nullptr, &call_args));
    grpc_call_stack_destroy(call_stack, &final_info, nullptr);
    // Recreate arena every 64k iterations to avoid oom
    if (0 == (state.iterations() & 0xffff)) {
      gpr_arena_destroy(call_args.arena);
      call_args.arena = gpr_arena_create(bm_setup.kArenaSize);
    }
  }

  bm_setup.Destroy(&data, state);
}

typedef FilterFixture<nullptr, 0> NoFilter;
typedef FilterBM<NoFilter> NoFilterBM;
BENCHMARK_TEMPLATE(BM_CallStackInit, NoFilterBM);

typedef FilterFixture<&dummy_filter::dummy_filter, 0> DummyFilter;
typedef FilterBM<DummyFilter> DummyFilterBM;
BENCHMARK_TEMPLATE(BM_CallStackInit, DummyFilterBM);

typedef FilterFixture<&grpc_client_channel_filter, 0> ClientChannelFilter;
typedef FilterBM<ClientChannelFilter> ClientChannelFilterBM;
BENCHMARK_TEMPLATE(BM_CallStackInit, ClientChannelFilterBM);

typedef FilterFixture<&grpc_message_compress_filter, CHECKS_NOT_LAST> 
CompressFilter;
typedef FilterBM<CompressFilter> CompressFilterBM; 
BENCHMARK_TEMPLATE(BM_CallStackInit, CompressFilterBM); 

typedef
FilterFixture<&grpc_client_deadline_filter, CHECKS_NOT_LAST>
    ClientDeadlineFilter;
typedef FilterBM<ClientDeadlineFilter> ClientDeadlineFilterBM;
BENCHMARK_TEMPLATE(BM_CallStackInit, ClientDeadlineFilterBM);

typedef FilterFixture<&grpc_server_deadline_filter, CHECKS_NOT_LAST>
    ServerDeadlineFilter;
typedef FilterBM<ServerDeadlineFilter> ServerDeadlineFilterBM;
BENCHMARK_TEMPLATE(BM_CallStackInit, ServerDeadlineFilterBM);

typedef FilterFixture<&grpc_http_client_filter, CHECKS_NOT_LAST |
REQUIRES_TRANSPORT>
    HttpClientFilter;
typedef FilterBM<HttpClientFilter> HttpClientFilterBM;
BENCHMARK_TEMPLATE(BM_CallStackInit, HttpClientFilterBM);

typedef FilterFixture<&grpc_http_server_filter, CHECKS_NOT_LAST> HttpServerFilter;
typedef FilterBM<HttpServerFilter> HttpServerFilterBM;
BENCHMARK_TEMPLATE(BM_CallStackInit, HttpServerFilterBM);

typedef FilterFixture<&grpc_message_size_filter, CHECKS_NOT_LAST>
MessageSizeFilter; 
typedef FilterBM<MessageSizeFilter> MessageSizeFilterBM;
BENCHMARK_TEMPLATE(BM_CallStackInit, MessageSizeFilterBM);

typedef FilterFixture<&grpc_server_load_reporting_filter, CHECKS_NOT_LAST>
    ServerLoadReportingFilter;
typedef FilterBM<ServerLoadReportingFilter> ServerLoadReportingFilterBM;
BENCHMARK_TEMPLATE(BM_CallStackInit, ServerLoadReportingFilterBM);

// Test a filter's start_transport_stream_op_batch in isolation. FilterFixture
// specifies the filter under test (use the FilterFixture<> template to specify
// this).

// template <class FilterFixture>
// static void BM_StartTransportStreamOpBatch(benchmark::State& state) {
//   TrackCounters track_counters;
//   FilterFixture fixture;
//   std::ostringstream label;

//   std::vector<grpc_arg> args;
//   FakeClientChannelFactory fake_client_channel_factory;
//   args.push_back(grpc_client_channel_factory_create_channel_arg(
//       &fake_client_channel_factory));
//   args.push_back(StringArg(GRPC_ARG_SERVER_URI, "localhost"));

//   grpc_channel_args channel_args = {args.size(), &args[0]};

//   std::vector<const grpc_channel_filter*> filters;
//   if (fixture.filter != nullptr) {
//     filters.push_back(fixture.filter);
//     if (fixture.flags & CHECKS_NOT_LAST) {
//       filters.push_back(&dummy_filter::dummy_filter);
//     } else {
//       // Add another dummy filter so that all the benchmarked filters
//       // have a dummy filter on the stack. For consistency.
//       filters.insert(filters.begin(), &dummy_filter::dummy_filter);
//     }
//   }

//   size_t channel_size = grpc_channel_stack_size(filters.data(),
//   filters.size()); grpc_channel_stack* channel_stack =
//       static_cast<grpc_channel_stack*>(gpr_zalloc(channel_size));
//   GPR_ASSERT(GRPC_LOG_IF_ERROR(
//       "channel_stack_init",
//       grpc_channel_stack_init(1, FilterDestroy, channel_stack,
//       filters.data(),
//                               filters.size(), &channel_args,
//                               fixture.flags & REQUIRES_TRANSPORT
//                                   ? &dummy_transport::dummy_transport
//                                   : nullptr,
//                               "CHANNEL", channel_stack)));
//   grpc_core::ExecCtx::Get()->Flush();
//   grpc_call_stack* call_stack =
//       static_cast<grpc_call_stack*>(gpr_zalloc(channel_stack->call_stack_size));
//   grpc_millis deadline = GRPC_MILLIS_INF_FUTURE;
//   gpr_timespec start_time = gpr_now(GPR_CLOCK_MONOTONIC);
//   grpc_slice method = grpc_slice_from_static_string("/foo/bar");
//   grpc_call_final_info final_info;
//   grpc_call_element_args call_args;
//   call_args.call_stack = call_stack;
//   call_args.server_transport_data = nullptr;
//   call_args.context = nullptr;
//   call_args.path = method;
//   call_args.start_time = start_time;
//   call_args.deadline = deadline;
//   const int kArenaSize = 4096;
//   call_args.arena = gpr_arena_create(kArenaSize);

//   while (state.KeepRunning()) {
//     GPR_TIMER_SCOPE("BenchmarkCycle", 0);
//     memset(call_stack, 0, channel_stack->call_stack_size);
//     GRPC_ERROR_UNREF(
//         grpc_call_stack_init(channel_stack, 1, DoNothing, nullptr,
//         &call_args));

//     /* Create new payload */
//     grpc_transport_stream_op_batch_payload payload;
//     memset(&payload, 0, sizeof(grpc_transport_stream_op_batch_payload));
//     grpc_metadata_batch metadata_batch_send_init;
//     grpc_metadata_batch metadata_batch_recv_init;
//     grpc_metadata_batch metadata_batch_send_trailing;
//     grpc_metadata_batch metadata_batch_recv_trailing;
//     grpc_metadata_batch_init(&metadata_batch_send_init);
//     grpc_metadata_batch_init(&metadata_batch_recv_init);
//     grpc_metadata_batch_init(&metadata_batch_send_trailing);
//     grpc_metadata_batch_init(&metadata_batch_recv_trailing);
//     payload.send_initial_metadata.send_initial_metadata =
//         &metadata_batch_send_init;

//     payload.send_trailing_metadata.send_trailing_metadata =
//         &metadata_batch_send_trailing;
//     payload.recv_initial_metadata.recv_initial_metadata =
//         &metadata_batch_recv_init;
//     uint32_t recv_flags = 0;
//     payload.recv_initial_metadata.recv_flags = &recv_flags;

//     gpr_atm peer_address_atm;
//     payload.recv_initial_metadata.peer_string = &peer_address_atm;
//     std::string peer_address_string = "Unknown.";
//     gpr_atm_rel_store(payload.recv_initial_metadata.peer_string,
//                       (gpr_atm)gpr_strdup(peer_address_string.data()));

//     payload.recv_trailing_metadata.recv_trailing_metadata =
//         &metadata_batch_recv_trailing;

//     grpc_core::OrphanablePtr<grpc_core::ByteStream> op;
//     payload.recv_message.recv_message = &op;

//     grpc_transport_stream_stats stats;
//     memset(&stats, 0, sizeof(grpc_transport_stream_stats));
//     payload.collect_stats.collect_stats = &stats;

//     grpc_core::ManualConstructor<grpc_core::SliceBufferByteStream>
//         byte_stream_send;
//     grpc_slice_buffer sb2;
//     grpc_slice_buffer_init(&sb2);
//     byte_stream_send.Init(&sb2, 0);
//     payload.send_message.send_message.reset(byte_stream_send.get());

//     grpc_slice_buffer sb;
//     grpc_slice_buffer_init(&sb);
//     grpc_core::SliceBufferByteStream* sbs =
//         grpc_core::New<grpc_core::SliceBufferByteStream>(&sb, 0);
//     payload.recv_message.recv_message->reset(sbs);

//     /* Create new batch with all 6 ops */
//     grpc_transport_stream_op_batch batch;
//     memset(&batch, 0, sizeof(grpc_transport_stream_op_batch));
//     batch.payload = &payload;
//     batch.send_initial_metadata = true;
//     batch.send_trailing_metadata = true;
//     batch.send_message = true;
//     batch.recv_initial_metadata = true;
//     batch.recv_message = true;
//     batch.recv_trailing_metadata = true;
//     batch.collect_stats = true;

//     grpc_call_element* call_elem =
//     CALL_ELEMS_FROM_STACK(call_args.call_stack);

//     if (fixture.filter != nullptr) {
//       fixture.filter->start_transport_stream_op_batch(call_elem, &batch);
//     }

//     GRPC_CLOSURE_RUN(batch.on_complete, GRPC_ERROR_NONE);
//     GRPC_CLOSURE_RUN(
//         batch.payload->recv_initial_metadata.recv_initial_metadata_ready,
//         GRPC_ERROR_NONE);
//     GRPC_CLOSURE_RUN(batch.payload->recv_message.recv_message_ready,
//                      GRPC_ERROR_NONE);
//   }

//   grpc_call_stack_destroy(call_stack, &final_info, nullptr);
//   grpc_core::ExecCtx::Get()->Flush();

//   grpc_channel_stack_destroy(channel_stack);
//   gpr_arena_destroy(call_args.arena);

//   gpr_free(channel_stack);
//   gpr_free(call_stack);

//   state.SetLabel(label.str());
//   track_counters.Finish(state);
// }
// BENCHMARK_TEMPLATE(BM_StartTransportStreamOpBatch, NoFilter);
// BENCHMARK_TEMPLATE(BM_StartTransportStreamOpBatch, DummyFilter);
// BENCHMARK_TEMPLATE(BM_StartTransportStreamOpBatch, CompressFilter);
// BENCHMARK_TEMPLATE(BM_StartTransportStreamOpBatch, ClientDeadlineFilter);
// BENCHMARK_TEMPLATE(BM_StartTransportStreamOpBatch, ServerDeadlineFilter);
// BENCHMARK_TEMPLATE(BM_StartTransportStreamOpBatch, HttpClientFilter);
// BENCHMARK_TEMPLATE(BM_StartTransportStreamOpBatch, HttpServerFilter);
// BENCHMARK_TEMPLATE(BM_StartTransportStreamOpBatch, MessageSizeFilter);
// BENCHMARK_TEMPLATE(BM_StartTransportStreamOpBatch, LoadReportingFilter);

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
