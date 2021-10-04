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
#include "test/cpp/microbenchmarks/fullstack_unary_ping_pong.h"
#include "test/cpp/util/test_config.h"

namespace grpc {
namespace testing {

/*******************************************************************************
 * CONFIGURATIONS
 */

// Add args to benchmark, but allow filtering.
static void AddBenchmarkArgsList(
    benchmark::internal::Benchmark* b,
    const std::vector<std::vector<int64_t>>& args_list) {
  // SKIPS SOME SCENARIOS!!!
  for (int i = 0; i < args_list.size(); i += 7) {
    b->Args(args_list[i]);
  }
}

// Replace "benchmark::internal::Benchmark" with "::testing::Benchmark" to use
// internal microbenchmarking tooling
static void SweepSizesArgs(benchmark::internal::Benchmark* b) {
  std::vector<std::vector<int64_t>> args_list;

  args_list.push_back({0, 0});
  for (int i = 1; i <= 128 * 1024 * 1024; i *= 8) {
    args_list.push_back({i, 0});
    args_list.push_back({0, i});
    args_list.push_back({i, i});
  }

  AddBenchmarkArgsList(b, args_list);
}

BENCHMARK_TEMPLATE(BM_UnaryPingPong, TCP, NoOpMutator, NoOpMutator)
    ->Apply(SweepSizesArgs);
BENCHMARK_TEMPLATE(BM_UnaryPingPong, MinTCP, NoOpMutator, NoOpMutator)
    ->Apply(SweepSizesArgs);
BENCHMARK_TEMPLATE(BM_UnaryPingPong, UDS, NoOpMutator, NoOpMutator)
    ->Args({0, 0});
BENCHMARK_TEMPLATE(BM_UnaryPingPong, MinUDS, NoOpMutator, NoOpMutator)
    ->Args({0, 0});
BENCHMARK_TEMPLATE(BM_UnaryPingPong, InProcess, NoOpMutator, NoOpMutator)
    ->Apply(SweepSizesArgs);
BENCHMARK_TEMPLATE(BM_UnaryPingPong, MinInProcess, NoOpMutator, NoOpMutator)
    ->Apply(SweepSizesArgs);
BENCHMARK_TEMPLATE(BM_UnaryPingPong, SockPair, NoOpMutator, NoOpMutator)
    ->Args({0, 0});
BENCHMARK_TEMPLATE(BM_UnaryPingPong, MinSockPair, NoOpMutator, NoOpMutator)
    ->Args({0, 0});
BENCHMARK_TEMPLATE(BM_UnaryPingPong, InProcessCHTTP2, NoOpMutator, NoOpMutator)
    ->Apply(SweepSizesArgs);
BENCHMARK_TEMPLATE(BM_UnaryPingPong, MinInProcessCHTTP2, NoOpMutator,
                   NoOpMutator)
    ->Apply(SweepSizesArgs);
BENCHMARK_TEMPLATE(BM_UnaryPingPong, InProcessCHTTP2,
                   Client_AddMetadata<RandomBinaryMetadata<10>, 1>, NoOpMutator)
    ->Args({0, 0});
BENCHMARK_TEMPLATE(BM_UnaryPingPong, InProcessCHTTP2,
                   Client_AddMetadata<RandomBinaryMetadata<31>, 1>, NoOpMutator)
    ->Args({0, 0});
BENCHMARK_TEMPLATE(BM_UnaryPingPong, InProcessCHTTP2,
                   Client_AddMetadata<RandomBinaryMetadata<100>, 1>,
                   NoOpMutator)
    ->Args({0, 0});
BENCHMARK_TEMPLATE(BM_UnaryPingPong, InProcessCHTTP2,
                   Client_AddMetadata<RandomBinaryMetadata<10>, 2>, NoOpMutator)
    ->Args({0, 0});
BENCHMARK_TEMPLATE(BM_UnaryPingPong, InProcessCHTTP2,
                   Client_AddMetadata<RandomBinaryMetadata<31>, 2>, NoOpMutator)
    ->Args({0, 0});
BENCHMARK_TEMPLATE(BM_UnaryPingPong, InProcessCHTTP2,
                   Client_AddMetadata<RandomBinaryMetadata<100>, 2>,
                   NoOpMutator)
    ->Args({0, 0});
BENCHMARK_TEMPLATE(BM_UnaryPingPong, InProcessCHTTP2, NoOpMutator,
                   Server_AddInitialMetadata<RandomBinaryMetadata<10>, 1>)
    ->Args({0, 0});
BENCHMARK_TEMPLATE(BM_UnaryPingPong, InProcessCHTTP2, NoOpMutator,
                   Server_AddInitialMetadata<RandomBinaryMetadata<31>, 1>)
    ->Args({0, 0});
BENCHMARK_TEMPLATE(BM_UnaryPingPong, InProcessCHTTP2, NoOpMutator,
                   Server_AddInitialMetadata<RandomBinaryMetadata<100>, 1>)
    ->Args({0, 0});
BENCHMARK_TEMPLATE(BM_UnaryPingPong, InProcessCHTTP2,
                   Client_AddMetadata<RandomAsciiMetadata<10>, 1>, NoOpMutator)
    ->Args({0, 0});
BENCHMARK_TEMPLATE(BM_UnaryPingPong, InProcessCHTTP2,
                   Client_AddMetadata<RandomAsciiMetadata<31>, 1>, NoOpMutator)
    ->Args({0, 0});
BENCHMARK_TEMPLATE(BM_UnaryPingPong, InProcessCHTTP2,
                   Client_AddMetadata<RandomAsciiMetadata<100>, 1>, NoOpMutator)
    ->Args({0, 0});
BENCHMARK_TEMPLATE(BM_UnaryPingPong, InProcessCHTTP2, NoOpMutator,
                   Server_AddInitialMetadata<RandomAsciiMetadata<10>, 1>)
    ->Args({0, 0});
BENCHMARK_TEMPLATE(BM_UnaryPingPong, InProcessCHTTP2, NoOpMutator,
                   Server_AddInitialMetadata<RandomAsciiMetadata<31>, 1>)
    ->Args({0, 0});
BENCHMARK_TEMPLATE(BM_UnaryPingPong, InProcessCHTTP2, NoOpMutator,
                   Server_AddInitialMetadata<RandomAsciiMetadata<100>, 1>)
    ->Args({0, 0});
BENCHMARK_TEMPLATE(BM_UnaryPingPong, InProcessCHTTP2, NoOpMutator,
                   Server_AddInitialMetadata<RandomAsciiMetadata<10>, 100>)
    ->Args({0, 0});
BENCHMARK_TEMPLATE(BM_UnaryPingPong, InProcess,
                   Client_AddMetadata<RandomBinaryMetadata<10>, 1>, NoOpMutator)
    ->Args({0, 0});
BENCHMARK_TEMPLATE(BM_UnaryPingPong, InProcess,
                   Client_AddMetadata<RandomBinaryMetadata<31>, 1>, NoOpMutator)
    ->Args({0, 0});
BENCHMARK_TEMPLATE(BM_UnaryPingPong, InProcess,
                   Client_AddMetadata<RandomBinaryMetadata<100>, 1>,
                   NoOpMutator)
    ->Args({0, 0});
BENCHMARK_TEMPLATE(BM_UnaryPingPong, InProcess,
                   Client_AddMetadata<RandomBinaryMetadata<10>, 2>, NoOpMutator)
    ->Args({0, 0});
BENCHMARK_TEMPLATE(BM_UnaryPingPong, InProcess,
                   Client_AddMetadata<RandomBinaryMetadata<31>, 2>, NoOpMutator)
    ->Args({0, 0});
BENCHMARK_TEMPLATE(BM_UnaryPingPong, InProcess,
                   Client_AddMetadata<RandomBinaryMetadata<100>, 2>,
                   NoOpMutator)
    ->Args({0, 0});
BENCHMARK_TEMPLATE(BM_UnaryPingPong, InProcess, NoOpMutator,
                   Server_AddInitialMetadata<RandomBinaryMetadata<10>, 1>)
    ->Args({0, 0});
BENCHMARK_TEMPLATE(BM_UnaryPingPong, InProcess, NoOpMutator,
                   Server_AddInitialMetadata<RandomBinaryMetadata<31>, 1>)
    ->Args({0, 0});
BENCHMARK_TEMPLATE(BM_UnaryPingPong, InProcess, NoOpMutator,
                   Server_AddInitialMetadata<RandomBinaryMetadata<100>, 1>)
    ->Args({0, 0});
BENCHMARK_TEMPLATE(BM_UnaryPingPong, InProcess,
                   Client_AddMetadata<RandomAsciiMetadata<10>, 1>, NoOpMutator)
    ->Args({0, 0});
BENCHMARK_TEMPLATE(BM_UnaryPingPong, InProcess,
                   Client_AddMetadata<RandomAsciiMetadata<31>, 1>, NoOpMutator)
    ->Args({0, 0});
BENCHMARK_TEMPLATE(BM_UnaryPingPong, InProcess,
                   Client_AddMetadata<RandomAsciiMetadata<100>, 1>, NoOpMutator)
    ->Args({0, 0});
BENCHMARK_TEMPLATE(BM_UnaryPingPong, InProcess, NoOpMutator,
                   Server_AddInitialMetadata<RandomAsciiMetadata<10>, 1>)
    ->Args({0, 0});
BENCHMARK_TEMPLATE(BM_UnaryPingPong, InProcess, NoOpMutator,
                   Server_AddInitialMetadata<RandomAsciiMetadata<31>, 1>)
    ->Args({0, 0});
BENCHMARK_TEMPLATE(BM_UnaryPingPong, InProcess, NoOpMutator,
                   Server_AddInitialMetadata<RandomAsciiMetadata<100>, 1>)
    ->Args({0, 0});
BENCHMARK_TEMPLATE(BM_UnaryPingPong, InProcess, NoOpMutator,
                   Server_AddInitialMetadata<RandomAsciiMetadata<10>, 100>)
    ->Args({0, 0});

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
