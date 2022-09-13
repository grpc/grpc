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

std::atomic_int gCnt{0};
Promise<bool> gDone{false};

struct FanoutParameters {
  int depth;
  int fanout;
  int limit;
};

void BM_EventEngine_RunSmallLambda(benchmark::State& state) {
  int cb_count = state.range(0);
  Promise<bool> p{false};
  auto cb = [&p, cb_count]() {
    if (++gCnt == cb_count) p.Set(true);
  };
  auto engine = GetDefaultEventEngine();
  for (auto _ : state) {
    for (int i = 0; i < cb_count; i++) {
      engine->Run(cb);
    }
    p.Get();
    state.PauseTiming();
    p.Reset();
    gCnt.store(0);
    state.ResumeTiming();
  }
  ResetDefaultEventEngine();
  state.SetItemsProcessed(cb_count * state.iterations());
}
BENCHMARK(BM_EventEngine_RunSmallLambda)
    ->Range(100, 4096)
    ->MeasureProcessCPUTime()
    ->UseRealTime();

void BM_EventEngine_RunLargeLambda(benchmark::State& state) {
  int cb_count = state.range(0);
  Promise<bool> p{false};
  // large lambdas require an extra allocation
  std::string extra = "12345678";
  auto cb = [&p, cb_count, extra]() {
    (void)extra;
    if (++gCnt == cb_count) p.Set(true);
  };
  auto engine = GetDefaultEventEngine();
  for (auto _ : state) {
    for (int i = 0; i < cb_count; i++) {
      engine->Run(cb);
    }
    p.Get();
    state.PauseTiming();
    p.Reset();
    gCnt.store(0);
    state.ResumeTiming();
  }
  ResetDefaultEventEngine();
  state.SetItemsProcessed(cb_count * state.iterations());
}
BENCHMARK(BM_EventEngine_RunLargeLambda)
    ->Range(100, 4096)
    ->MeasureProcessCPUTime()
    ->UseRealTime();

void BM_EventEngine_RunClosure(benchmark::State& state) {
  int cb_count = state.range(0);
  Promise<bool> p{false};
  AnyInvocableClosure closure([&p, cb_count]() {
    if (++gCnt == cb_count) {
      p.Set(true);
    }
  });
  auto engine = GetDefaultEventEngine();
  for (auto _ : state) {
    for (int i = 0; i < cb_count; i++) {
      engine->Run(&closure);
    }
    p.Get();
    state.PauseTiming();
    p.Reset();
    gCnt.store(0);
    state.ResumeTiming();
  }
  ResetDefaultEventEngine();
  state.SetItemsProcessed(cb_count * state.iterations());
}
BENCHMARK(BM_EventEngine_RunClosure)
    ->Range(100, 4096)
    ->MeasureProcessCPUTime()
    ->UseRealTime();

void FanoutTestArguments(benchmark::internal::Benchmark* b) {
  // TODO(hork): enable when the engines are fast enough to run more than 1
  // iteration:
  // ->Args({10000, 1})  // chain of callbacks scheduling callbacks
  // ->Args({1, 10000})  // flat scheduling of callbacks
  // ->Args({5, 6})      // depth 5, fans out to 9,330 callbacks
  b->Args({1000, 1})     // chain of callbacks scheduling callbacks
      ->Args({100, 1})   // chain of callbacks scheduling callbacks
      ->Args({1, 1000})  // flat scheduling of callbacks
      ->Args({1, 100})   // flat scheduling of callbacks
      ->Args({2, 100})   // depth 2, fans out 10,101 callbacks
      ->Args({4, 10})    // depth 4, fans out to 11,110 callbacks
      ->UseRealTime()
      ->MeasureProcessCPUTime();
}

FanoutParameters GetFanoutParameters(benchmark::State& state) {
  FanoutParameters params;
  params.depth = state.range(0);
  params.fanout = state.range(1);
  // sum of geometric series
  params.limit =
      (1 - std::pow(params.fanout, params.depth + 1)) / (1 - params.fanout);
  if (params.depth == 1 || params.fanout == 1) {
    params.limit = std::max(params.depth, params.limit);
  }
  // sanity checking
  GPR_ASSERT(params.limit >= params.fanout * params.depth);
  return params;
}

void FanOutCallback(EventEngine* engine, const FanoutParameters& params,
                    int processing_layer) {
  int local_cnt = gCnt.fetch_add(1, std::memory_order_acq_rel);
  if (local_cnt + 1 == params.limit) {
    gDone.Set(true);
    return;
  }
  if (params.depth == processing_layer) return;
  for (int i = 0; i < params.fanout; i++) {
    engine->Run([engine, &params, processing_layer]() {
      FanOutCallback(engine, params, processing_layer + 1);
    });
  }
}

void BM_EventEngine_Lambda_FanOut(benchmark::State& state) {
  auto params = GetFanoutParameters(state);
  auto engine = GetDefaultEventEngine();
  for (auto _ : state) {
    FanOutCallback(engine, params, /*processing_layer=*/0);
    gDone.Get();
    // cleanup
    state.PauseTiming();
    gCnt.store(0);
    gDone.Reset();
    state.ResumeTiming();
  }
  state.SetItemsProcessed(params.limit * state.iterations());
}
BENCHMARK(BM_EventEngine_Lambda_FanOut)->Apply(FanoutTestArguments);

void ClosureFanOutCallback(EventEngine::Closure* child_closure,
                           EventEngine* engine,
                           const FanoutParameters& params) {
  int local_cnt = gCnt.fetch_add(1, std::memory_order_acq_rel);
  if (local_cnt + 1 == params.limit) {
    gDone.Set(true);
    return;
  }
  if (child_closure == nullptr) return;
  for (int i = 0; i < params.fanout; i++) {
    engine->Run(child_closure);
  }
}

void BM_EventEngine_Closure_FanOut(benchmark::State& state) {
  auto params = GetFanoutParameters(state);
  auto engine = GetDefaultEventEngine();
  std::vector<EventEngine::Closure*> closures;
  closures.reserve(params.depth + 2);
  closures.push_back(nullptr);
  // prepare a unique closure for each depth
  for (int i = 0; i <= params.depth; i++) {
    // call the previous closure (e.g., closures[2] calls closures[1] during
    // fanout)
    closures.push_back(
        new AnyInvocableClosure([i, engine, &closures, &params]() {
          ClosureFanOutCallback(closures[i], engine, params);
        }));
  }
  for (auto _ : state) {
    GPR_DEBUG_ASSERT(gCnt.load(std::memory_order_relaxed) == 0);
    engine->Run(closures[params.depth + 1]);
    gDone.Get();
    // cleanup
    state.PauseTiming();
    gCnt.store(0);
    gDone.Reset();
    state.ResumeTiming();
  }
  state.SetItemsProcessed(params.limit * state.iterations());
  for (auto i : closures) delete i;
}
BENCHMARK(BM_EventEngine_Closure_FanOut)->Apply(FanoutTestArguments);

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
