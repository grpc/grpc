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

#include <grpc/event_engine/event_engine.h>
#include <grpcpp/impl/grpc_library.h>
#include <grpcpp/support/byte_buffer.h>

#include "src/core/lib/event_engine/common_closures.h"
#include "src/core/lib/event_engine/default_event_engine.h"
#include "src/core/lib/event_engine/promise.h"
#include "test/core/util/test_config.h"
#include "test/cpp/microbenchmarks/helpers.h"
#include "test/cpp/util/test_config.h"

namespace {

void BM_EventEngine_RunLambda(benchmark::State& state) {
  int cb_count = state.range(0);
  std::atomic_int cnt{0};
  grpc_event_engine::experimental::Promise<bool> p{false};
  auto cb = [&cnt, &p, cb_count]() {
    if (++cnt == cb_count) p.Set(true);
  };
  auto engine = grpc_event_engine::experimental::GetDefaultEventEngine();
  for (auto _ : state) {
    for (int i = 0; i < cb_count; i++) {
      engine->Run(cb);
    }
    GPR_ASSERT(p.Get());
    state.PauseTiming();
    p.Reset();
    cnt.store(0);
    state.ResumeTiming();
  }
  grpc_event_engine::experimental::ResetDefaultEventEngine();
  state.SetItemsProcessed(cb_count * state.iterations());
}
BENCHMARK(BM_EventEngine_RunLambda)->Range(100, 10 * 1000);
;

void BM_EventEngine_RunClosure(benchmark::State& state) {
  int cb_count = state.range(0);
  grpc_event_engine::experimental::Promise<bool> p{false};
  std::atomic_int cnt{0};
  grpc_event_engine::experimental::AnyInvocableClosure closure(
      [&cnt, &p, cb_count]() {
        if (++cnt == cb_count) {
          p.Set(true);
        }
      });
  auto engine = grpc_event_engine::experimental::GetDefaultEventEngine();
  for (auto _ : state) {
    for (int i = 0; i < cb_count; i++) {
      engine->Run(&closure);
    }
    GPR_ASSERT(p.Get());
    state.PauseTiming();
    p.Reset();
    cnt.store(0);
    state.ResumeTiming();
  }
  grpc_event_engine::experimental::ResetDefaultEventEngine();
  state.SetItemsProcessed(cb_count * state.iterations());
}
BENCHMARK(BM_EventEngine_RunClosure)->Range(100, 10 * 1000);
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
