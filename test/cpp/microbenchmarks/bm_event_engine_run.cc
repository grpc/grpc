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

#include <benchmark/benchmark.h>
#include <grpc/event_engine/event_engine.h>
#include <grpcpp/impl/grpc_library.h>

#include <atomic>
#include <cmath>
#include <memory>
#include <vector>

#include "absl/debugging/leak_check.h"
#include "absl/functional/any_invocable.h"
#include "absl/log/check.h"
#include "absl/strings/str_format.h"
#include "src/core/lib/event_engine/common_closures.h"
#include "src/core/lib/event_engine/default_event_engine.h"
#include "src/core/util/crash.h"
#include "src/core/util/notification.h"
#include "test/core/test_util/test_config.h"
#include "test/cpp/microbenchmarks/helpers.h"
#include "test/cpp/util/test_config.h"

namespace {

using ::grpc_event_engine::experimental::AnyInvocableClosure;
using ::grpc_event_engine::experimental::EventEngine;
using ::grpc_event_engine::experimental::GetDefaultEventEngine;

struct FanoutParameters {
  int depth;
  int fanout;
  int limit;
};

void BM_EventEngine_RunSmallLambda(benchmark::State& state) {
  auto engine = GetDefaultEventEngine();
  const int cb_count = state.range(0);
  std::atomic_int count{0};
  for (auto _ : state) {
    state.PauseTiming();
    grpc_core::Notification signal;
    auto cb = [&signal, &count, cb_count]() {
      if (++count == cb_count) signal.Notify();
    };
    state.ResumeTiming();
    for (int i = 0; i < cb_count; i++) {
      engine->Run(cb);
    }
    signal.WaitForNotification();
    count.store(0);
  }
  state.SetItemsProcessed(cb_count * state.iterations());
}
BENCHMARK(BM_EventEngine_RunSmallLambda)
    ->Range(100, 4096)
    ->MeasureProcessCPUTime()
    ->UseRealTime();

void BM_EventEngine_RunLargeLambda(benchmark::State& state) {
  int cb_count = state.range(0);
  // large lambdas require an extra allocation
  std::string extra = "12345678";
  auto engine = GetDefaultEventEngine();
  std::atomic_int count{0};
  for (auto _ : state) {
    state.PauseTiming();
    grpc_core::Notification signal;
    auto cb = [&signal, &count, cb_count, extra]() {
      (void)extra;
      if (++count == cb_count) signal.Notify();
    };
    state.ResumeTiming();
    for (int i = 0; i < cb_count; i++) {
      engine->Run(cb);
    }
    signal.WaitForNotification();
    count.store(0);
  }
  state.SetItemsProcessed(cb_count * state.iterations());
}
BENCHMARK(BM_EventEngine_RunLargeLambda)
    ->Range(100, 4096)
    ->MeasureProcessCPUTime()
    ->UseRealTime();

void BM_EventEngine_RunClosure(benchmark::State& state) {
  int cb_count = state.range(0);
  grpc_core::Notification* signal = new grpc_core::Notification();
  std::atomic_int count{0};
  // Ignore leaks from this closure. For simplicity, this closure is not deleted
  // because the closure may still be executing after the EventEngine is
  // destroyed. This is because the default posix EventEngine's thread pool may
  // get destroyed separately from the EventEngine.
  AnyInvocableClosure* closure = absl::IgnoreLeak(
      new AnyInvocableClosure([signal_holder = &signal, cb_count, &count]() {
        if (++count == cb_count) {
          (*signal_holder)->Notify();
        }
      }));
  auto engine = GetDefaultEventEngine();
  for (auto _ : state) {
    for (int i = 0; i < cb_count; i++) {
      engine->Run(closure);
    }
    signal->WaitForNotification();
    state.PauseTiming();
    delete signal;
    signal = new grpc_core::Notification();
    count.store(0);
    state.ResumeTiming();
  }
  delete signal;
  state.SetItemsProcessed(cb_count * state.iterations());
}
BENCHMARK(BM_EventEngine_RunClosure)
    ->Range(100, 4096)
    ->MeasureProcessCPUTime()
    ->UseRealTime();

void FanoutTestArguments(benchmark::internal::Benchmark* b) {
  // TODO(hork): enable when the engines are fast enough to run these:
  // ->Args({10000, 1})  // chain of callbacks scheduling callbacks
  // ->Args({1, 10000})  // flat scheduling of callbacks
  // ->Args({5, 6})      // depth 5, fans out to 9,330 callbacks
  //  ->Args({2, 100})   // depth 2, fans out 10,101 callbacks
  //  ->Args({4, 10})    // depth 4, fans out to 11,110 callbacks
  b->Args({1000, 1})     // chain of callbacks scheduling callbacks
      ->Args({100, 1})   // chain of callbacks scheduling callbacks
      ->Args({1, 1000})  // flat scheduling of callbacks
      ->Args({1, 100})   // flat scheduling of callbacks
      ->Args({2, 70})    // depth 2, fans out 4971
      ->Args({4, 8})     // depth 4, fans out 4681
      ->UseRealTime()
      ->MeasureProcessCPUTime();
}

FanoutParameters GetFanoutParameters(benchmark::State& state) {
  FanoutParameters params;
  params.depth = state.range(0);
  params.fanout = state.range(1);
  if (params.depth == 1 || params.fanout == 1) {
    params.limit = std::max(params.depth, params.fanout) + 1;
  } else {
    // sum of geometric series
    params.limit =
        (1 - std::pow(params.fanout, params.depth + 1)) / (1 - params.fanout);
  }
  // sanity checking
  CHECK(params.limit >= params.fanout * params.depth);
  return params;
}

// EventEngine callback for Lambda FanOut tests
//
// Note that params are copied each time for 2 reasons: 1) callbacks will
// inevitably continue to shut down after the end of the test, so a reference
// parameter will become invalid and crash some callbacks, and 2) in my RBE
// tests, copies are slightly faster than a shared_ptr<FanoutParams>
// alternative.
void FanOutCallback(std::shared_ptr<EventEngine> engine,
                    const FanoutParameters params,
                    grpc_core::Notification& signal, std::atomic_int& count,
                    int processing_layer) {
  int local_cnt = count.fetch_add(1, std::memory_order_acq_rel) + 1;
  if (local_cnt == params.limit) {
    signal.Notify();
    return;
  }
  DCHECK_LT(local_cnt, params.limit);
  if (params.depth == processing_layer) return;
  for (int i = 0; i < params.fanout; i++) {
    engine->Run([engine, params, processing_layer, &count, &signal]() {
      FanOutCallback(engine, params, signal, count, processing_layer + 1);
    });
  }
}

void BM_EventEngine_Lambda_FanOut(benchmark::State& state) {
  auto params = GetFanoutParameters(state);
  auto engine = GetDefaultEventEngine();
  for (auto _ : state) {
    std::atomic_int count{0};
    grpc_core::Notification signal;
    FanOutCallback(engine, params, signal, count, /*processing_layer=*/0);
    do {
      signal.WaitForNotification();
    } while (count.load() != params.limit);
  }
  state.SetItemsProcessed(params.limit * state.iterations());
}
BENCHMARK(BM_EventEngine_Lambda_FanOut)->Apply(FanoutTestArguments);

void ClosureFanOutCallback(EventEngine::Closure* child_closure,
                           std::shared_ptr<EventEngine> engine,
                           grpc_core::Notification** signal_holder,
                           std::atomic_int& count,
                           const FanoutParameters params) {
  int local_cnt = count.fetch_add(1, std::memory_order_acq_rel) + 1;
  if (local_cnt == params.limit) {
    (*signal_holder)->Notify();
    return;
  }
  if (local_cnt > params.limit) {
    grpc_core::Crash(absl::StrFormat("Ran too many closures: %d/%d", local_cnt,
                                     params.limit));
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
  grpc_core::Notification* signal = new grpc_core::Notification();
  std::atomic_int count{0};
  // prepare a unique closure for each depth
  for (int i = 0; i <= params.depth; i++) {
    // call the previous closure (e.g., closures[2] calls closures[1] during
    // fanout)
    closures.push_back(new AnyInvocableClosure(
        [i, engine, &closures, params, signal_holder = &signal, &count]() {
          ClosureFanOutCallback(closures[i], engine, signal_holder, count,
                                params);
        }));
  }
  for (auto _ : state) {
    DCHECK_EQ(count.load(std::memory_order_relaxed), 0);
    engine->Run(closures[params.depth + 1]);
    do {
      signal->WaitForNotification();
    } while (count.load() != params.limit);
    // cleanup
    state.PauseTiming();
    delete signal;
    signal = new grpc_core::Notification();
    count.store(0);
    state.ResumeTiming();
  }
  delete signal;
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
