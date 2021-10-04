/*
 *
 * Copyright 2016 gRPC authors.
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

/* Benchmark gRPC end2end in various configurations */

#include "test/core/util/test_config.h"
#include "test/cpp/microbenchmarks/fullstack_streaming_ping_pong.h"
#include "test/cpp/util/test_config.h"

namespace grpc {
namespace testing {

// Add args to benchmark, but allow filtering.
static void AddBenchmarkArgsList(
    benchmark::internal::Benchmark* b,
    const std::vector<std::vector<int64_t>>& args_list) {
  // SKIPS SOME SCENARIOS!!!
  for (int i = 0; i < args_list.size(); i += 7) {
    b->Args(args_list[i]);
  }
}

/*******************************************************************************
 * CONFIGURATIONS
 */

// Generate Args for StreamingPingPong benchmarks. Currently generates args for
// only "small streams" (i.e streams with 0, 1 or 2 messages)
static void StreamingPingPongArgs(benchmark::internal::Benchmark* b) {
  std::vector<std::vector<int64_t>> args_list;

  args_list.push_back(
      {0, 0});  // spl case: 0 ping-pong msgs (msg_size doesn't matter here)

  args_list.push_back({0, 1});
  args_list.push_back({0, 2});

  for (int msg_size = 1; msg_size <= 128 * 1024 * 1024; msg_size *= 8) {
    args_list.push_back({msg_size, 1});
    args_list.push_back({msg_size, 2});
  }

  AddBenchmarkArgsList(b, args_list);
}

static void StreamingPingPongMsgsArgs(benchmark::internal::Benchmark* b) {
  std::vector<std::vector<int64_t>> args_list;

  for (int msg_size = 0; msg_size <= 128 * 1024 * 1024;
       msg_size == 0 ? msg_size++ : msg_size *= 8) {
    args_list.push_back({msg_size});
  }
  AddBenchmarkArgsList(b, args_list);
}

BENCHMARK_TEMPLATE(BM_StreamingPingPong, InProcessCHTTP2, NoOpMutator,
                   NoOpMutator)
    ->Apply(StreamingPingPongArgs);
BENCHMARK_TEMPLATE(BM_StreamingPingPong, TCP, NoOpMutator, NoOpMutator)
    ->Apply(StreamingPingPongArgs);
BENCHMARK_TEMPLATE(BM_StreamingPingPong, InProcess, NoOpMutator, NoOpMutator)
    ->Apply(StreamingPingPongArgs);

BENCHMARK_TEMPLATE(BM_StreamingPingPongMsgs, InProcessCHTTP2, NoOpMutator,
                   NoOpMutator)
    ->Apply(StreamingPingPongMsgsArgs);
BENCHMARK_TEMPLATE(BM_StreamingPingPongMsgs, TCP, NoOpMutator, NoOpMutator)
    ->Apply(StreamingPingPongMsgsArgs);
BENCHMARK_TEMPLATE(BM_StreamingPingPongMsgs, InProcess, NoOpMutator,
                   NoOpMutator)
    ->Apply(StreamingPingPongMsgsArgs);

BENCHMARK_TEMPLATE(BM_StreamingPingPong, MinInProcessCHTTP2, NoOpMutator,
                   NoOpMutator)
    ->Apply(StreamingPingPongArgs);
BENCHMARK_TEMPLATE(BM_StreamingPingPong, MinTCP, NoOpMutator, NoOpMutator)
    ->Apply(StreamingPingPongArgs);
BENCHMARK_TEMPLATE(BM_StreamingPingPong, MinInProcess, NoOpMutator, NoOpMutator)
    ->Apply(StreamingPingPongArgs);

BENCHMARK_TEMPLATE(BM_StreamingPingPongMsgs, MinInProcessCHTTP2, NoOpMutator,
                   NoOpMutator)
    ->Apply(StreamingPingPongMsgsArgs);
BENCHMARK_TEMPLATE(BM_StreamingPingPongMsgs, MinTCP, NoOpMutator, NoOpMutator)
    ->Apply(StreamingPingPongMsgsArgs);
BENCHMARK_TEMPLATE(BM_StreamingPingPongMsgs, MinInProcess, NoOpMutator,
                   NoOpMutator)
    ->Apply(StreamingPingPongMsgsArgs);

// Generate Args for StreamingPingPongWithCoalescingApi benchmarks. Currently
// generates args for only "small streams" (i.e streams with 0, 1 or 2 messages)
static void StreamingPingPongWithCoalescingApiArgs(
    benchmark::internal::Benchmark* b) {
  std::vector<std::vector<int64_t>> args_list;
  int msg_size = 0;

  args_list.push_back(
      {0, 0, 0});  // spl case: 0 ping-pong msgs (msg_size doesn't matter here)
  args_list.push_back(
      {0, 0, 1});  // spl case: 0 ping-pong msgs (msg_size doesn't matter here)

  for (msg_size = 0; msg_size <= 128 * 1024 * 1024;
       msg_size == 0 ? msg_size++ : msg_size *= 8) {
    args_list.push_back({msg_size, 1, 0});
    args_list.push_back({msg_size, 2, 0});
    args_list.push_back({msg_size, 1, 1});
    args_list.push_back({msg_size, 2, 1});
  }

  AddBenchmarkArgsList(b, args_list);
}

BENCHMARK_TEMPLATE(BM_StreamingPingPongWithCoalescingApi, InProcessCHTTP2,
                   NoOpMutator, NoOpMutator)
    ->Apply(StreamingPingPongWithCoalescingApiArgs);
BENCHMARK_TEMPLATE(BM_StreamingPingPongWithCoalescingApi, MinInProcessCHTTP2,
                   NoOpMutator, NoOpMutator)
    ->Apply(StreamingPingPongWithCoalescingApiArgs);
BENCHMARK_TEMPLATE(BM_StreamingPingPongWithCoalescingApi, InProcess,
                   NoOpMutator, NoOpMutator)
    ->Apply(StreamingPingPongWithCoalescingApiArgs);
BENCHMARK_TEMPLATE(BM_StreamingPingPongWithCoalescingApi, MinInProcess,
                   NoOpMutator, NoOpMutator)
    ->Apply(StreamingPingPongWithCoalescingApiArgs);

}  // namespace testing
}  // namespace grpc

// Some distros have RunSpecifiedBenchmarks under the benchmark namespace,
// and others do not. This allows us to support both modes.
namespace benchmark {
void RunTheBenchmarksNamespaced() { RunSpecifiedBenchmarks(); }
}  // namespace benchmark

int main(int argc, char** argv) {
  grpc::testing::TestEnvironment env(argc, argv);
  LibraryInitializer libInit;
  ::benchmark::Initialize(&argc, argv);
  ::grpc::testing::InitTest(&argc, &argv, false);
  benchmark::RunTheBenchmarksNamespaced();
  return 0;
}
