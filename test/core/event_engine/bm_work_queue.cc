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

#include "src/core/lib/event_engine/workqueue.h"
#include "test/core/util/test_config.h"

namespace {

using ::grpc_event_engine::experimental::WorkQueue;

grpc_core::Mutex globalMu;
std::vector<WorkQueue<intptr_t>*>* globalWorkQueueList;
std::vector<std::deque<intptr_t>*>* globalDequeList;
std::vector<grpc_core::Mutex>* globalDequeMutexList;

static void GlobalSetup(const benchmark::State& state) {
  // called for every test, resets all state
  globalWorkQueueList = new std::vector<WorkQueue<intptr_t>*>();
  globalWorkQueueList->reserve(state.threads());
  globalDequeList = new std::vector<std::deque<intptr_t>*>();
  globalDequeList->reserve(state.threads());
  globalDequeMutexList = new std::vector<grpc_core::Mutex>(
      std::vector<grpc_core::Mutex>(state.threads()));
}

static void GlobalTeardown(const benchmark::State& state) {
  // called for every test, resets all state
  delete globalWorkQueueList;
  delete globalDequeList;
  delete globalDequeMutexList;
}

static void BM_WorkQueueIntptrPopFront(benchmark::State& state) {
  WorkQueue<intptr_t> queue;
  int element_count = state.range(0);
  for (auto _ : state) {
    int cnt = 0;
    for (int i = 0; i < element_count; i++) queue.Add(1);
    absl::optional<intptr_t> popped;
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

static void BM_MultithreadedWorkQueuePopBack(benchmark::State& state) {
  if (state.thread_index() == 0)
    (*globalWorkQueueList)[0] = new WorkQueue<intptr_t>();
  int element_count = state.range(0);
  for (auto _ : state) {
    int cnt = 0;
    auto* queue = (*globalWorkQueueList)[0];
    for (int i = 0; i < element_count; i++) queue->Add(1);
    absl::optional<intptr_t> popped;
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

static void BM_StdDequeLIFO(benchmark::State& state) {
  if (state.thread_index() == 0)
    (*globalDequeList)[0] = new std::deque<intptr_t>();
  auto& mu = (*globalDequeMutexList)[0];
  int element_count = state.range(0);
  for (auto _ : state) {
    auto* queue = (*globalDequeList)[0];
    for (int i = 0; i < element_count; i++) {
      grpc_core::MutexLock lock(&mu);
      queue->emplace_back(1);
    }
    for (int i = 0; i < element_count; i++) {
      grpc_core::MutexLock lock(&mu);
      intptr_t popped = queue->back();
      queue->pop_back();
      assert(popped == 1);
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

static void BM_WorkQueuePerThread(benchmark::State& state) {
  WorkQueue<intptr_t> local_queue;
  {
    grpc_core::MutexLock lock(&globalMu);
    (*globalWorkQueueList)[state.thread_index()] = &local_queue;
  }
  int element_count = state.range(0);
  float pct_fill = state.range(1) / 100.0;
  for (auto _ : state) {
    // sparsely populate a queue
    for (int i = 0; i < std::ceil(element_count * pct_fill); i++) {
      local_queue.Add(1);
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

static void BM_StdDequePerThread(benchmark::State& state) {
  std::deque<intptr_t> local_queue;
  (*globalDequeList)[state.thread_index()] = &local_queue;
  int element_count = state.range(0);
  float pct_fill = state.range(1) / 100.0;
  auto& local_mu = (*globalDequeMutexList)[state.thread_index()];
  for (auto _ : state) {
    // sparsely populate a queue
    for (int i = 0; i < std::ceil(element_count * pct_fill); i++) {
      grpc_core::MutexLock lock(&local_mu);
      local_queue.emplace_back(1);
    }
    int pop_attempts = 0;
    auto iq = globalDequeList->begin();
    auto mu = globalDequeMutexList->begin();
    while (pop_attempts++ < element_count) {
      {
        grpc_core::MutexLock lock(&*mu);
        if (!(*iq)->empty()) {
          assert((*iq)->back() == 1);
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
