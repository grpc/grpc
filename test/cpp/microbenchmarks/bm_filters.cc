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
#include "src/core/lib/gprpp/manual_constructor.h"

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
    GRPC_ERROR_UNREF(grpc_call_stack_init(data.channel_stack, 1, DoNothing,
                                          nullptr, &data.call_args));
    grpc_call_stack_destroy(data.call_stack, &final_info, nullptr);
    // Recreate arena every 64k iterations to avoid oom
    if (0 == (state.iterations() & 0xffff)) {
      gpr_arena_destroy(data.call_args.arena);
      data.call_args.arena = gpr_arena_create(bm_setup.kArenaSize);
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

typedef FilterFixture<&grpc_client_deadline_filter, CHECKS_NOT_LAST>
    ClientDeadlineFilter;
typedef FilterBM<ClientDeadlineFilter> ClientDeadlineFilterBM;
BENCHMARK_TEMPLATE(BM_CallStackInit, ClientDeadlineFilterBM);

typedef FilterFixture<&grpc_server_deadline_filter, CHECKS_NOT_LAST>
    ServerDeadlineFilter;
typedef FilterBM<ServerDeadlineFilter> ServerDeadlineFilterBM;
BENCHMARK_TEMPLATE(BM_CallStackInit, ServerDeadlineFilterBM);

typedef FilterFixture<&grpc_http_client_filter,
                      CHECKS_NOT_LAST | REQUIRES_TRANSPORT>
    HttpClientFilter;
typedef FilterBM<HttpClientFilter> HttpClientFilterBM;
BENCHMARK_TEMPLATE(BM_CallStackInit, HttpClientFilterBM);

typedef FilterFixture<&grpc_http_server_filter, CHECKS_NOT_LAST>
    HttpServerFilter;
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

// Measure full filter functionality overhead, from initializing the call stack
// through running all filter callbacks.
// Note that all we do is send down all 6 ops through the filter stack; we do
// not test different combinations or subsets of ops. Thus, this test does not
// comprehensively test all the code paths of each individual filter because
// filters may take different code paths based on the combination and/or
// ordering of the ops.
template <class FilterBM>
static void BM_FullFilterFunctionality(benchmark::State& state) {
  // Setup for benchmark
  FilterBM bm_setup;
  struct DataForFilterBM data;
  bm_setup.Setup(&data);

  // Run the benchmark
  while (state.KeepRunning()) {
    GPR_TIMER_SCOPE("BenchmarkCycle", 0);
    // Because it's not valid to send more than one of any of the {send, recv}_
    // {initial, trailing}_metadata ops on a single call, we need to construct
    // a new call stack each time through the loop. It's also not valid to have
    // more than one of send_message or recv_message in flight on a single call
    // at the same time.
    memset(data.call_stack, 0, data.channel_stack->call_stack_size);
    GRPC_ERROR_UNREF(grpc_call_stack_init(data.channel_stack, 1, DoNothing,
                                          nullptr, &data.call_args));

    struct PayloadData payload;
    CreatePayloadForAllOps(&payload);

    grpc_transport_stream_op_batch batch;
    CreateBatchWithAllOps(&batch, &payload.payload);

    grpc_call_element* call_elem =
        CALL_ELEMS_FROM_STACK(data.call_args.call_stack);
    if (!data.filters.empty()) {
      bm_setup.fixture.filter->start_transport_stream_op_batch(call_elem,
                                                               &batch);
    }

    GRPC_CLOSURE_RUN(batch.on_complete, GRPC_ERROR_NONE);
    GRPC_CLOSURE_RUN(
        batch.payload->recv_initial_metadata.recv_initial_metadata_ready,
        GRPC_ERROR_NONE);
    GRPC_CLOSURE_RUN(batch.payload->recv_message.recv_message_ready,
                     GRPC_ERROR_NONE);
  }

  grpc_call_final_info final_info;
  grpc_call_stack_destroy(data.call_stack, &final_info, nullptr);

  bm_setup.Destroy(&data, state);
}

// We skip Client_Channel for this benchmark because it requires a lot more work
// than what has been done in order to microbenchmark it. Moreover, it may be
// the case that once we do this work, we may be measuring much more than just
// client_channel filter overhead.
BENCHMARK_TEMPLATE(BM_FullFilterFunctionality, NoFilterBM);
BENCHMARK_TEMPLATE(BM_FullFilterFunctionality, DummyFilterBM);
BENCHMARK_TEMPLATE(BM_FullFilterFunctionality, CompressFilterBM);
BENCHMARK_TEMPLATE(BM_FullFilterFunctionality, ClientDeadlineFilterBM);
BENCHMARK_TEMPLATE(BM_FullFilterFunctionality, ServerDeadlineFilterBM);
BENCHMARK_TEMPLATE(BM_FullFilterFunctionality, HttpClientFilterBM);
BENCHMARK_TEMPLATE(BM_FullFilterFunctionality, HttpServerFilterBM);
BENCHMARK_TEMPLATE(BM_FullFilterFunctionality, MessageSizeFilterBM);
BENCHMARK_TEMPLATE(BM_FullFilterFunctionality, ServerLoadReportingFilterBM);

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
