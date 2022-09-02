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

#include <sstream>

#include <benchmark/benchmark.h>

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
BENCHMARK(BM_WorkQueueIntptrPopFront)->Range(1, 4096);

static void BM_WorkQueueIntptrPopBack(benchmark::State& state) {
  WorkQueue<intptr_t> queue;
  int element_count = state.range(0);
  for (auto _ : state) {
    int cnt = 0;
    for (int i = 0; i < element_count; i++) queue.Add(1);
    absl::optional<intptr_t> popped;
    cnt = 0;
    do {
      popped = queue.PopBack();
      if (popped.has_value()) ++cnt;
    } while (cnt < element_count);
  }
  state.SetItemsProcessed(element_count * state.iterations());
}
BENCHMARK(BM_WorkQueueIntptrPopBack)->Range(1, 4096);
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
