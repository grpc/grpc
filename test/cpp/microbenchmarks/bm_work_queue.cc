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
#include <grpc/support/port_platform.h>

#include <cmath>
#include <deque>
#include <sstream>

// ensure assert() is enabled
#undef NDEBUG
#include <cassert>

#include <benchmark/benchmark.h>

#include <grpc/support/log.h>

#include "src/core/lib/event_engine/common_closures.h"
#include "src/core/lib/event_engine/work_queue.h"
#include "src/core/lib/gprpp/crash.h"
#include "test/core/util/test_config.h"

namespace {

using ::grpc_event_engine::experimental::AnyInvocableClosure;
using ::grpc_event_engine::experimental::EventEngine;
using ::grpc_event_engine::experimental::WorkQueue;

grpc_core::Mutex globalMu;
std::vector<WorkQueue*>* globalWorkQueueList;
std::vector<std::deque<EventEngine::Closure*>*>* globalDequeList;
std::vector<grpc_core::Mutex>* globalDequeMutexList;

void GlobalSetup(const benchmark::State& state) {
  // called for every test, resets all state
  globalWorkQueueList = new std::vector<WorkQueue*>();
  globalWorkQueueList->reserve(state.threads());
  globalDequeList = new std::vector<std::deque<EventEngine::Closure*>*>();
  globalDequeList->reserve(state.threads());
  globalDequeMutexList = new std::vector<grpc_core::Mutex>(
      std::vector<grpc_core::Mutex>(state.threads()));
}

void GlobalTeardown(const benchmark::State& /* state */) {
  // called for every test, resets all state
  delete globalWorkQueueList;
  delete globalDequeList;
  delete globalDequeMutexList;
}

void BM_WorkQueueIntptrPopFront(benchmark::State& state) {
  WorkQueue queue;
  grpc_event_engine::experimental::AnyInvocableClosure closure([] {});
  int element_count = state.range(0);
  for (auto _ : state) {
    int cnt = 0;
    for (int i = 0; i < element_count; i++) queue.Add(&closure);
    absl::optional<EventEngine::Closure*> popped;
    cnt = 0;
    do {
      popped = queue.PopFront();
      if (popped.has_value()) ++cnt;
    } while (cnt < element_count);
  }
  state.counters["Added"] = element_count * state.iterations();
  state.counters["Popped"] = state.counters["Added"];
  state.counters["Steal Rate"] =
      benchmark::Counter(state.counters["Popped"], benchmark::Counter::kIsRate);
}
BENCHMARK(BM_WorkQueueIntptrPopFront)
    ->Setup(GlobalSetup)
    ->Teardown(GlobalTeardown)
    ->Range(1, 512)
    ->UseRealTime()
    ->MeasureProcessCPUTime();

void BM_MultithreadedWorkQueuePopBack(benchmark::State& state) {
  if (state.thread_index() == 0) (*globalWorkQueueList)[0] = new WorkQueue();
  AnyInvocableClosure closure([] {});
  int element_count = state.range(0);
  for (auto _ : state) {
    int cnt = 0;
    auto* queue = (*globalWorkQueueList)[0];
    for (int i = 0; i < element_count; i++) queue->Add(&closure);
    absl::optional<EventEngine::Closure*> popped;
    cnt = 0;
    do {
      popped = queue->PopBack();
      if (popped.has_value()) ++cnt;
    } while (cnt < element_count);
  }
  state.counters["Added"] = element_count * state.iterations();
  state.counters["Popped"] = state.counters["Added"];
  state.counters["Steal Rate"] =
      benchmark::Counter(state.counters["Popped"], benchmark::Counter::kIsRate);
  if (state.thread_index() == 0) {
    delete (*globalWorkQueueList)[0];
  }
}
BENCHMARK(BM_MultithreadedWorkQueuePopBack)
    ->Setup(GlobalSetup)
    ->Teardown(GlobalTeardown)
    ->Range(1, 512)
    ->UseRealTime()
    ->MeasureProcessCPUTime()
    ->Threads(1)
    ->Threads(4)
    ->ThreadPerCpu();

void BM_WorkQueueClosureExecution(benchmark::State& state) {
  WorkQueue queue;
  int element_count = state.range(0);
  int run_count = 0;
  grpc_event_engine::experimental::AnyInvocableClosure closure(
      [&run_count] { ++run_count; });
  for (auto _ : state) {
    for (int i = 0; i < element_count; i++) queue.Add(&closure);
    do {
      queue.PopFront()->Run();
    } while (run_count < element_count);
    run_count = 0;
  }
  state.counters["Added"] = element_count * state.iterations();
  state.counters["Popped"] = state.counters["Added"];
  state.counters["Steal Rate"] =
      benchmark::Counter(state.counters["Popped"], benchmark::Counter::kIsRate);
}
BENCHMARK(BM_WorkQueueClosureExecution)
    ->Range(8, 128)
    ->UseRealTime()
    ->MeasureProcessCPUTime();

void BM_WorkQueueAnyInvocableExecution(benchmark::State& state) {
  WorkQueue queue;
  int element_count = state.range(0);
  int run_count = 0;
  for (auto _ : state) {
    for (int i = 0; i < element_count; i++) {
      queue.Add([&run_count] { ++run_count; });
    }
    do {
      queue.PopFront()->Run();
    } while (run_count < element_count);
    run_count = 0;
  }
  state.counters["Added"] = element_count * state.iterations();
  state.counters["Popped"] = state.counters["Added"];
  state.counters["Steal Rate"] =
      benchmark::Counter(state.counters["Popped"], benchmark::Counter::kIsRate);
}
BENCHMARK(BM_WorkQueueAnyInvocableExecution)
    ->Range(8, 128)
    ->UseRealTime()
    ->MeasureProcessCPUTime();

void BM_StdDequeLIFO(benchmark::State& state) {
  if (state.thread_index() == 0) {
    (*globalDequeList)[0] = new std::deque<EventEngine::Closure*>();
  }
  auto& mu = (*globalDequeMutexList)[0];
  int element_count = state.range(0);
  AnyInvocableClosure closure([] {});
  for (auto _ : state) {
    auto* queue = (*globalDequeList)[0];
    for (int i = 0; i < element_count; i++) {
      grpc_core::MutexLock lock(&mu);
      queue->emplace_back(&closure);
    }
    for (int i = 0; i < element_count; i++) {
      grpc_core::MutexLock lock(&mu);
      EventEngine::Closure* popped = queue->back();
      queue->pop_back();
      assert(popped != nullptr);
    }
  }
  state.counters["Added"] = element_count * state.iterations();
  state.counters["Popped"] = state.counters["Added"];
  state.counters["Steal Rate"] =
      benchmark::Counter(state.counters["Popped"], benchmark::Counter::kIsRate);
  if (state.thread_index() == 0) {
    delete (*globalDequeList)[0];
  }
}
BENCHMARK(BM_StdDequeLIFO)
    ->Setup(GlobalSetup)
    ->Teardown(GlobalTeardown)
    ->Range(1, 512)
    ->UseRealTime()
    ->MeasureProcessCPUTime()
    ->Threads(1)
    ->Threads(4)
    ->ThreadPerCpu();

void PerThreadArguments(benchmark::internal::Benchmark* b) {
  b->Setup(GlobalSetup)
      ->Teardown(GlobalTeardown)
      ->ArgsProduct({/*pop_attempts=*/{10, 50, 250},
                     /*pct_fill=*/{2, 10, 50}})
      ->UseRealTime()
      ->MeasureProcessCPUTime()
      ->Threads(10)
      ->ThreadPerCpu();
}

void BM_WorkQueuePerThread(benchmark::State& state) {
  WorkQueue local_queue;
  {
    grpc_core::MutexLock lock(&globalMu);
    (*globalWorkQueueList)[state.thread_index()] = &local_queue;
  }
  AnyInvocableClosure closure([] {});
  int element_count = state.range(0);
  float pct_fill = state.range(1) / 100.0;
  for (auto _ : state) {
    // sparsely populate a queue
    for (int i = 0; i < std::ceil(element_count * pct_fill); i++) {
      local_queue.Add(&closure);
    }
    // attempt to pop from all thread queues `element_count` times
    int pop_attempts = 0;
    auto iq = globalWorkQueueList->begin();
    while (pop_attempts++ < element_count) {
      // may not get a value if the queue being looked at from another thread
      (*iq)->PopBack();
      if (iq == globalWorkQueueList->end()) {
        iq = globalWorkQueueList->begin();
      } else {
        iq++;
      };
    }
  }
  state.counters["Added"] =
      std::ceil(element_count * pct_fill) * state.iterations();
  state.counters["Steal Attempts"] = element_count * state.iterations();
  state.counters["Steal Rate"] = benchmark::Counter(
      state.counters["Steal Attempts"], benchmark::Counter::kIsRate);
  if (state.thread_index() == 0) {
    for (auto* queue : *globalWorkQueueList) {
      assert(queue->Empty());
    }
  }
}
BENCHMARK(BM_WorkQueuePerThread)->Apply(PerThreadArguments);

void BM_StdDequePerThread(benchmark::State& state) {
  std::deque<EventEngine::Closure*> local_queue;
  (*globalDequeList)[state.thread_index()] = &local_queue;
  int element_count = state.range(0);
  float pct_fill = state.range(1) / 100.0;
  AnyInvocableClosure closure([] {});
  auto& local_mu = (*globalDequeMutexList)[state.thread_index()];
  for (auto _ : state) {
    // sparsely populate a queue
    for (int i = 0; i < std::ceil(element_count * pct_fill); i++) {
      grpc_core::MutexLock lock(&local_mu);
      local_queue.emplace_back(&closure);
    }
    int pop_attempts = 0;
    auto iq = globalDequeList->begin();
    auto mu = globalDequeMutexList->begin();
    while (pop_attempts++ < element_count) {
      {
        grpc_core::MutexLock lock(&*mu);
        if (!(*iq)->empty()) {
          assert((*iq)->back() != nullptr);
          (*iq)->pop_back();
        }
      }
      if (iq == globalDequeList->end()) {
        iq = globalDequeList->begin();
        mu = globalDequeMutexList->begin();
      } else {
        ++iq;
        ++mu;
      };
    }
  }
  state.counters["Added"] =
      std::ceil(element_count * pct_fill) * state.iterations();
  state.counters["Steal Attempts"] = element_count * state.iterations();
  state.counters["Steal Rate"] = benchmark::Counter(
      state.counters["Steal Attempts"], benchmark::Counter::kIsRate);
  if (state.thread_index() == 0) {
    for (auto* queue : *globalDequeList) {
      assert(queue->empty());
    }
  }
}
BENCHMARK(BM_StdDequePerThread)->Apply(PerThreadArguments);

}  // namespace

// Some distros have RunSpecifiedBenchmarks under the benchmark namespace,
// and others do not. This allows us to support both modes.
namespace benchmark {
void RunTheBenchmarksNamespaced() { RunSpecifiedBenchmarks(); }
}  // namespace benchmark

int main(int argc, char** argv) {
  grpc::testing::TestEnvironment env(&argc, argv);
  ::benchmark::Initialize(&argc, argv);
  benchmark::RunTheBenchmarksNamespaced();
  return 0;
}
