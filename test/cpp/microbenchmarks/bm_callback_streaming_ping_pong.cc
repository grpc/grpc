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
#include "test/cpp/microbenchmarks/callback_streaming_ping_pong.h"
#include "test/cpp/util/test_config.h"

namespace grpc {
namespace testing {

//******************************************************************************
// CONFIGURATIONS
//

// Replace "benchmark::internal::Benchmark" with "::testing::Benchmark" to use
// internal microbenchmarking tooling
static void StreamingPingPongMsgSizeArgs(benchmark::internal::Benchmark* b) {
  // base case: 0 byte ping-pong msgs
  b->Args({0, 1});
  b->Args({0, 2});

  for (int msg_size = 1; msg_size <= 128 * 1024 * 1024; msg_size *= 8) {
    b->Args({msg_size, 1});
    b->Args({msg_size, 2});
  }
}

// Replace "benchmark::internal::Benchmark" with "::testing::Benchmark" to use
// internal microbenchmarking tooling
static void StreamingPingPongMsgsNumberArgs(benchmark::internal::Benchmark* b) {
  for (int msg_number = 1; msg_number <= 256 * 1024; msg_number *= 8) {
    b->Args({0, msg_number});
    b->Args({1024, msg_number});
  }
}

// Streaming with different message size
BENCHMARK_TEMPLATE(BM_CallbackBidiStreaming, InProcess, NoOpMutator,
                   NoOpMutator)
    ->Apply(StreamingPingPongMsgSizeArgs);
BENCHMARK_TEMPLATE(BM_CallbackBidiStreaming, MinInProcess, NoOpMutator,
                   NoOpMutator)
    ->Apply(StreamingPingPongMsgSizeArgs);

// Streaming with different message number
BENCHMARK_TEMPLATE(BM_CallbackBidiStreaming, InProcess, NoOpMutator,
                   NoOpMutator)
    ->Apply(StreamingPingPongMsgsNumberArgs);
BENCHMARK_TEMPLATE(BM_CallbackBidiStreaming, MinInProcess, NoOpMutator,
                   NoOpMutator)
    ->Apply(StreamingPingPongMsgsNumberArgs);

// Client context with different metadata
BENCHMARK_TEMPLATE(BM_CallbackBidiStreaming, InProcess,
                   Client_AddMetadata<RandomBinaryMetadata<10>, 1>, NoOpMutator)
    ->Args({0, 1});
BENCHMARK_TEMPLATE(BM_CallbackBidiStreaming, InProcess,
                   Client_AddMetadata<RandomBinaryMetadata<31>, 1>, NoOpMutator)
    ->Args({0, 1});
BENCHMARK_TEMPLATE(BM_CallbackBidiStreaming, InProcess,
                   Client_AddMetadata<RandomBinaryMetadata<100>, 1>,
                   NoOpMutator)
    ->Args({0, 1});
BENCHMARK_TEMPLATE(BM_CallbackBidiStreaming, InProcess,
                   Client_AddMetadata<RandomBinaryMetadata<10>, 2>, NoOpMutator)
    ->Args({0, 1});
BENCHMARK_TEMPLATE(BM_CallbackBidiStreaming, InProcess,
                   Client_AddMetadata<RandomBinaryMetadata<31>, 2>, NoOpMutator)
    ->Args({0, 1});
BENCHMARK_TEMPLATE(BM_CallbackBidiStreaming, InProcess,
                   Client_AddMetadata<RandomBinaryMetadata<100>, 2>,
                   NoOpMutator)
    ->Args({0, 1});
BENCHMARK_TEMPLATE(BM_CallbackBidiStreaming, InProcess,
                   Client_AddMetadata<RandomAsciiMetadata<10>, 1>, NoOpMutator)
    ->Args({0, 1});
BENCHMARK_TEMPLATE(BM_CallbackBidiStreaming, InProcess,
                   Client_AddMetadata<RandomAsciiMetadata<31>, 1>, NoOpMutator)
    ->Args({0, 1});
BENCHMARK_TEMPLATE(BM_CallbackBidiStreaming, InProcess,
                   Client_AddMetadata<RandomAsciiMetadata<100>, 1>, NoOpMutator)
    ->Args({0, 1});

// Server context with different metadata
BENCHMARK_TEMPLATE(BM_CallbackBidiStreaming, InProcess, NoOpMutator,
                   Server_AddInitialMetadata<RandomBinaryMetadata<10>, 1>)
    ->Args({0, 1});
BENCHMARK_TEMPLATE(BM_CallbackBidiStreaming, InProcess, NoOpMutator,
                   Server_AddInitialMetadata<RandomBinaryMetadata<31>, 1>)
    ->Args({0, 1});
BENCHMARK_TEMPLATE(BM_CallbackBidiStreaming, InProcess, NoOpMutator,
                   Server_AddInitialMetadata<RandomBinaryMetadata<100>, 1>)
    ->Args({0, 1});
BENCHMARK_TEMPLATE(BM_CallbackBidiStreaming, InProcess, NoOpMutator,
                   Server_AddInitialMetadata<RandomAsciiMetadata<10>, 1>)
    ->Args({0, 1});
BENCHMARK_TEMPLATE(BM_CallbackBidiStreaming, InProcess, NoOpMutator,
                   Server_AddInitialMetadata<RandomAsciiMetadata<31>, 1>)
    ->Args({0, 1});
BENCHMARK_TEMPLATE(BM_CallbackBidiStreaming, InProcess, NoOpMutator,
                   Server_AddInitialMetadata<RandomAsciiMetadata<100>, 1>)
    ->Args({0, 1});
BENCHMARK_TEMPLATE(BM_CallbackBidiStreaming, InProcess, NoOpMutator,
                   Server_AddInitialMetadata<RandomAsciiMetadata<10>, 100>)
    ->Args({0, 1});

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
