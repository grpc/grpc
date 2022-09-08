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
  state.SetItemsProcessed(element_count * state.iterations());
}
BENCHMARK(BM_WorkQueueIntptrPopFront)
    ->Range(1, 4096)
    ->UseRealTime()
    ->MeasureProcessCPUTime();

WorkQueue<intptr_t>* globalQueue;
std::atomic_int globalProcessedCount{0};

static void BM_MultithreadedWorkQueuePopBack(benchmark::State& state) {
  if (state.thread_index() == 0) {
    // Setup
    globalQueue = new WorkQueue<intptr_t>();
    globalProcessedCount = 0;
  }
  int element_count = state.range(0);
  for (auto _ : state) {
    int cnt = 0;
    for (int i = 0; i < element_count; i++) globalQueue->Add(1);
    absl::optional<intptr_t> popped;
    cnt = 0;
    do {
      popped = globalQueue->PopBack();
      if (popped.has_value()) ++cnt;
    } while (cnt < element_count);
  }
  globalProcessedCount += element_count * state.iterations();
  if (state.thread_index() == 0) {
    // Teardown
    assert(globalQueue->Empty());
    delete globalQueue;
    state.SetItemsProcessed(globalProcessedCount.load());
  }
}
BENCHMARK(BM_MultithreadedWorkQueuePopBack)
    ->Range(1, 4096)
    ->UseRealTime()
    ->MeasureProcessCPUTime()
    ->Threads(1)
    ->Threads(4)
    ->ThreadPerCpu();

grpc_core::Mutex globalDequeMutex;
std::deque<intptr_t>* globalDeque ABSL_GUARDED_BY(globalDequeMutex);

static void BM_StdDequeLIFO(benchmark::State& state) {
  if (state.thread_index() == 0) {
    // Setup
    globalProcessedCount = 0;
    grpc_core::MutexLock lock(&globalDequeMutex);
    globalDeque = new std::deque<intptr_t>();
  }
  int element_count = state.range(0);
  for (auto _ : state) {
    for (int i = 0; i < element_count; i++) {
      grpc_core::MutexLock lock(&globalDequeMutex);
      globalDeque->emplace_back(1);
    }
    for (int i = 0; i < element_count; i++) {
      grpc_core::MutexLock lock(&globalDequeMutex);
      intptr_t popped = globalDeque->back();
      globalDeque->pop_back();
      assert(popped == 1);
    }
  }
  globalProcessedCount += element_count * state.iterations();
  if (state.thread_index() == 0) {
    // Teardown
    grpc_core::MutexLock lock(&globalDequeMutex);
    assert(globalDeque->empty());
    delete globalDeque;
    state.SetItemsProcessed(globalProcessedCount.load());
  }
}
BENCHMARK(BM_StdDequeLIFO)
    ->Range(1, 4096)
    ->UseRealTime()
    ->MeasureProcessCPUTime()
    ->Threads(1)
    ->Threads(4)
    ->ThreadPerCpu();
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
