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
#include <cmath>
#include <memory>

#include <benchmark/benchmark.h>

#include <grpc/event_engine/event_engine.h>
#include <grpcpp/impl/grpc_library.h>

#include "src/core/lib/event_engine/common_closures.h"
#include "src/core/lib/event_engine/default_event_engine.h"
#include "src/core/lib/event_engine/promise.h"
#include "test/core/util/test_config.h"
#include "test/cpp/microbenchmarks/helpers.h"
#include "test/cpp/util/test_config.h"

namespace {

using ::grpc_event_engine::experimental::AnyInvocableClosure;
using ::grpc_event_engine::experimental::EventEngine;
using ::grpc_event_engine::experimental::GetDefaultEventEngine;
using ::grpc_event_engine::experimental::Promise;
using ::grpc_event_engine::experimental::ResetDefaultEventEngine;

void BM_EventEngine_RunLambda(benchmark::State& state) {
  int cb_count = state.range(0);
  std::atomic_int cnt{0};
  Promise<bool> p{false};
  auto cb = [&cnt, &p, cb_count]() {
    if (++cnt == cb_count) p.Set(true);
  };
  auto engine = GetDefaultEventEngine();
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
  ResetDefaultEventEngine();
  state.SetItemsProcessed(cb_count * state.iterations());
}
BENCHMARK(BM_EventEngine_RunLambda)
    ->Range(100, 4096)
    ->MeasureProcessCPUTime()
    ->UseRealTime();

void BM_EventEngine_RunClosure(benchmark::State& state) {
  int cb_count = state.range(0);
  Promise<bool> p{false};
  std::atomic_int cnt{0};
  AnyInvocableClosure closure([&cnt, &p, cb_count]() {
    if (++cnt == cb_count) {
      p.Set(true);
    }
  });
  auto engine = GetDefaultEventEngine();
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
  ResetDefaultEventEngine();
  state.SetItemsProcessed(cb_count * state.iterations());
}
BENCHMARK(BM_EventEngine_RunClosure)
    ->Range(100, 4096)
    ->MeasureProcessCPUTime()
    ->UseRealTime();

void FanOutCallback(EventEngine* engine, std::atomic_int& cnt, int fanout,
                    int depth, int limit, Promise<bool>& promise) {
  if (++cnt == limit) {
    promise.Set(true);
    return;
  }
  if (depth == 0) return;
  for (int i = 0; i < fanout; i++) {
    engine->Run([engine, &cnt, fanout, depth, limit, &promise]() {
      FanOutCallback(engine, cnt, fanout, depth - 1, limit, promise);
    });
  }
}

void BM_EventEngine_Lambda_FanOut(benchmark::State& state) {
  int depth = state.range(0);
  int fanout = state.range(1);
  // sum of geometric series
  int limit = (1 - std::pow(fanout, depth + 1)) / (1 - fanout);
  if (depth == 1 || fanout == 1) limit = std::max(depth, limit);
  auto engine = GetDefaultEventEngine();
  Promise<bool> promise{false};
  std::atomic_int cnt{0};
  for (auto _ : state) {
    FanOutCallback(engine, cnt, fanout, depth, limit, promise);
    GPR_ASSERT(promise.Get());
    // cleanup
    state.PauseTiming();
    cnt.store(0);
    promise.Reset();
    state.ResumeTiming();
  }
  state.SetItemsProcessed(limit * state.iterations());
}
// TODO(hork): enable the 3 commented-out tests when the engine is fast enough.
BENCHMARK(BM_EventEngine_Lambda_FanOut)
    // ->Args({10000, 1})  // chain of callbacks scheduling callbacks
    ->Args({1000, 1})  // chain of callbacks scheduling callbacks
    ->Args({100, 1})   // chain of callbacks scheduling callbacks
    // ->Args({1, 10000})  // flat scheduling of callbacks
    ->Args({1, 1000})  // flat scheduling of callbacks
    ->Args({1, 100})   // flat scheduling of callbacks
    ->Args({2, 100})   // depth 2, fans out 10,101 callbacks
    ->Args({4, 10})    // depth 4, fans out to 11,110 callbacks
    // ->Args({5, 6})      // depth 5, fans out to 9,330 callbacks
    ->MeasureProcessCPUTime()
    ->UseRealTime();

namespace {
std::atomic_int gCnt{0};
Promise<bool> gDone{false};
}  // namespace

void ClosureFanOutCallback(EventEngine::Closure* child_closure,
                           EventEngine* engine, int fanout, int limit) {
  int local_cnt = gCnt.fetch_add(1, std::memory_order_acq_rel);
  if (local_cnt + 1 == limit) {
    gDone.Set(true);
    return;
  }
  if (child_closure == nullptr) return;
  for (int i = 0; i < fanout; i++) {
    engine->Run(child_closure);
  }
}

void BM_EventEngine_Closure_FanOut(benchmark::State& state) {
  int depth = state.range(0);
  int fanout = state.range(1);
  // sum of geometric series
  int limit = (1 - std::pow(fanout, depth + 1)) / (1 - fanout) - 1;
  if (depth == 1 || fanout == 1) limit = std::max(depth, limit);
  auto engine = GetDefaultEventEngine();
  std::vector<EventEngine::Closure*> closures;
  closures.reserve(depth + 1);
  closures[0] = nullptr;
  // prepare a unique closure for each depth
  for (int i = 0; i < depth; i++) {
    closures[i + 1] =
        new AnyInvocableClosure([i, &closures, &engine, fanout, limit]() {
          ClosureFanOutCallback(closures[i], engine, fanout, limit);
        });
  }
  for (auto _ : state) {
    GPR_ASSERT(gCnt.load() == 0);
    for (int i = 0; i < fanout; i++) {
      engine->Run(closures[depth]);
    }
    GPR_ASSERT(gDone.Get());
    // cleanup
    state.PauseTiming();
    gCnt.store(0);
    gDone.Reset();
    state.ResumeTiming();
  }
  state.SetItemsProcessed(limit * state.iterations());
  for (auto i : closures) delete i;
}
// TODO(hork): enable the 3 commented-out tests when the engine is fast enough.
BENCHMARK(BM_EventEngine_Closure_FanOut)
    // ->Args({10000, 1})  // chain of callbacks scheduling callbacks
    ->Args({1000, 1})  // chain of callbacks scheduling callbacks
    ->Args({100, 1})   // chain of callbacks scheduling callbacks
    // ->Args({1, 10000})  // flat scheduling of callbacks
    ->Args({1, 1000})  // flat scheduling of callbacks
    ->Args({1, 100})   // flat scheduling of callbacks
    ->Args({2, 100})   // depth 2, fans out 10,101 callbacks
    ->Args({4, 10})    // depth 4, fans out to 11,110 callbacks
    // ->Args({5, 6})      // depth 5, fans out to 9,330 callbacks
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
