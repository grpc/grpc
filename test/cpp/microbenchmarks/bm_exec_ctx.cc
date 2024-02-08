// Copyright 2022 The gRPC Authors
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

#include <atomic>
#include <memory>

#include <benchmark/benchmark.h>

#include <grpcpp/impl/grpc_library.h>

#include "src/core/lib/gprpp/notification.h"
#include "src/core/lib/iomgr/exec_ctx.h"
#include "test/core/util/test_config.h"
#include "test/cpp/microbenchmarks/helpers.h"
#include "test/cpp/util/test_config.h"

namespace {
void NoOpCb(void* /* arg */, grpc_error_handle /* error */) {}

void BM_ExecCtx_Run(benchmark::State& state) {
  int cb_count = state.range(0);
  grpc_closure cb;
  GRPC_CLOSURE_INIT(&cb, NoOpCb, nullptr, nullptr);
  grpc_core::ExecCtx exec_ctx;
  for (auto _ : state) {
    for (int i = 0; i < cb_count; i++) {
      exec_ctx.Run(DEBUG_LOCATION, &cb, absl::OkStatus());
      exec_ctx.Flush();
    }
  }
  state.SetItemsProcessed(cb_count * state.iterations());
}
BENCHMARK(BM_ExecCtx_Run)
    ->Range(100, 10000)
    ->MeasureProcessCPUTime()
    ->UseRealTime();

struct CountingCbData {
  std::atomic_int cnt{0};
  grpc_core::Notification* signal;
  int limit;
};

void CountingCb(void* arg, grpc_error_handle) {
  auto* data = static_cast<CountingCbData*>(arg);
  if (++(data->cnt) == data->limit) data->signal->Notify();
}

void BM_ExecCtx_RunCounted(benchmark::State& state) {
  // A more fair comparison with EventEngine::Run, which must wait for all
  // executions to finish
  int cb_count = state.range(0);
  CountingCbData data;
  data.limit = cb_count;
  data.signal = new grpc_core::Notification();
  grpc_closure cb;
  GRPC_CLOSURE_INIT(&cb, CountingCb, &data, nullptr);
  grpc_core::ExecCtx exec_ctx;
  for (auto _ : state) {
    for (int i = 0; i < cb_count; i++) {
      exec_ctx.Run(DEBUG_LOCATION, &cb, absl::OkStatus());
      exec_ctx.Flush();
    }
    data.signal->WaitForNotification();
    state.PauseTiming();
    delete data.signal;
    data.signal = new grpc_core::Notification();
    data.cnt = 0;
    state.ResumeTiming();
  }
  delete data.signal;
  state.SetItemsProcessed(cb_count * state.iterations());
}
BENCHMARK(BM_ExecCtx_RunCounted)
    ->Range(100, 10000)
    ->MeasureProcessCPUTime()
    ->UseRealTime();
}  // namespace

// Some distros have RunSpecifiedBenchmarks under the benchmark namespace,
// and others do not. This allows us to support both modes.
namespace benchmark {
void RunTheBenchmarksNamespaced() { RunSpecifiedBenchmarks(); }
}  // namespace benchmark

int main(int argc, char** argv) {
  grpc::testing::TestEnvironment env(&argc, argv);
  LibraryInitializer libInit;
  benchmark::Initialize(&argc, argv);
  grpc::testing::InitTest(&argc, &argv, false);

  benchmark::RunTheBenchmarksNamespaced();
  return 0;
}
