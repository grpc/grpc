//
//
// Copyright 2019 gRPC authors.
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
//
//

#include "test/core/util/test_config.h"
#include "test/cpp/microbenchmarks/callback_unary_ping_pong.h"
#include "test/cpp/util/test_config.h"

namespace grpc {
namespace testing {

//******************************************************************************
// CONFIGURATIONS
//

// Replace "benchmark::internal::Benchmark" with "::testing::Benchmark" to use
// internal microbenchmarking tooling
static void SweepSizesArgs(benchmark::internal::Benchmark* b) {
  b->Args({0, 0});
  for (int i = 1; i <= 128 * 1024 * 1024; i *= 8) {
    // First argument is the message size of request
    // Second argument is the message size of response
    b->Args({i, 0});
    b->Args({0, i});
    b->Args({i, i});
  }
}

// Unary ping pong with different message size of request and response
BENCHMARK_TEMPLATE(BM_CallbackUnaryPingPong, InProcess, NoOpMutator,
                   NoOpMutator)
    ->Apply(SweepSizesArgs);
BENCHMARK_TEMPLATE(BM_CallbackUnaryPingPong, MinInProcess, NoOpMutator,
                   NoOpMutator)
    ->Apply(SweepSizesArgs);

// Client context with different metadata
BENCHMARK_TEMPLATE(BM_CallbackUnaryPingPong, InProcess,
                   Client_AddMetadata<RandomBinaryMetadata<10>, 1>, NoOpMutator)
    ->Args({0, 0});
BENCHMARK_TEMPLATE(BM_CallbackUnaryPingPong, InProcess,
                   Client_AddMetadata<RandomBinaryMetadata<31>, 1>, NoOpMutator)
    ->Args({0, 0});
BENCHMARK_TEMPLATE(BM_CallbackUnaryPingPong, InProcess,
                   Client_AddMetadata<RandomBinaryMetadata<100>, 1>,
                   NoOpMutator)
    ->Args({0, 0});
BENCHMARK_TEMPLATE(BM_CallbackUnaryPingPong, InProcess,
                   Client_AddMetadata<RandomBinaryMetadata<10>, 2>, NoOpMutator)
    ->Args({0, 0});
BENCHMARK_TEMPLATE(BM_CallbackUnaryPingPong, InProcess,
                   Client_AddMetadata<RandomBinaryMetadata<31>, 2>, NoOpMutator)
    ->Args({0, 0});
BENCHMARK_TEMPLATE(BM_CallbackUnaryPingPong, InProcess,
                   Client_AddMetadata<RandomBinaryMetadata<100>, 2>,
                   NoOpMutator)
    ->Args({0, 0});
BENCHMARK_TEMPLATE(BM_CallbackUnaryPingPong, InProcess,
                   Client_AddMetadata<RandomAsciiMetadata<10>, 1>, NoOpMutator)
    ->Args({0, 0});
BENCHMARK_TEMPLATE(BM_CallbackUnaryPingPong, InProcess,
                   Client_AddMetadata<RandomAsciiMetadata<31>, 1>, NoOpMutator)
    ->Args({0, 0});
BENCHMARK_TEMPLATE(BM_CallbackUnaryPingPong, InProcess,
                   Client_AddMetadata<RandomAsciiMetadata<100>, 1>, NoOpMutator)
    ->Args({0, 0});

// Server context with different metadata
BENCHMARK_TEMPLATE(BM_CallbackUnaryPingPong, InProcess, NoOpMutator,
                   Server_AddInitialMetadata<RandomBinaryMetadata<10>, 1>)
    ->Args({0, 0});
BENCHMARK_TEMPLATE(BM_CallbackUnaryPingPong, InProcess, NoOpMutator,
                   Server_AddInitialMetadata<RandomBinaryMetadata<31>, 1>)
    ->Args({0, 0});
BENCHMARK_TEMPLATE(BM_CallbackUnaryPingPong, InProcess, NoOpMutator,
                   Server_AddInitialMetadata<RandomBinaryMetadata<100>, 1>)
    ->Args({0, 0});
BENCHMARK_TEMPLATE(BM_CallbackUnaryPingPong, InProcess, NoOpMutator,
                   Server_AddInitialMetadata<RandomAsciiMetadata<10>, 1>)
    ->Args({0, 0});
BENCHMARK_TEMPLATE(BM_CallbackUnaryPingPong, InProcess, NoOpMutator,
                   Server_AddInitialMetadata<RandomAsciiMetadata<31>, 1>)
    ->Args({0, 0});
BENCHMARK_TEMPLATE(BM_CallbackUnaryPingPong, InProcess, NoOpMutator,
                   Server_AddInitialMetadata<RandomAsciiMetadata<100>, 1>)
    ->Args({0, 0});
BENCHMARK_TEMPLATE(BM_CallbackUnaryPingPong, InProcess, NoOpMutator,
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
  grpc::testing::TestEnvironment env(&argc, argv);
  LibraryInitializer libInit;
  ::benchmark::Initialize(&argc, argv);
  grpc::testing::InitTest(&argc, &argv, false);
  benchmark::RunTheBenchmarksNamespaced();
  return 0;
}
