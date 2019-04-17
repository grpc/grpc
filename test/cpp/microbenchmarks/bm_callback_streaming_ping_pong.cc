#include "test/cpp/microbenchmarks/callback_streaming_ping_pong.h"
#include "test/cpp/util/test_config.h"

namespace grpc {
namespace testing {

// force library initialization
auto& force_library_initialization = Library::get();

/*******************************************************************************
 * CONFIGURATIONS
 */

// Replace "benchmark::internal::Benchmark" with "::testing::Benchmark" to use
// internal microbenchmarking tooling
static void StreamingPingPongArgs(benchmark::internal::Benchmark* b) {
  int msg_size = 0;

  b->Args({0, 0});  // spl case: 0 ping-pong msgs (msg_size doesn't matter here)

  for (msg_size = 0; msg_size <= 128 * 1024 * 1024;
       msg_size == 0 ? msg_size++ : msg_size *= 8) {
    b->Args({msg_size, 1});
    b->Args({msg_size, 2});
  }
}

BENCHMARK_TEMPLATE(BM_StreamingPingPong, InProcess, NoOpMutator, NoOpMutator)
    ->Apply(StreamingPingPongArgs);
BENCHMARK_TEMPLATE(BM_StreamingPingPong, InProcess,
                   Client_AddMetadata<RandomBinaryMetadata<10>, 1>, NoOpMutator)
    ->Args({0, 0});
BENCHMARK_TEMPLATE(BM_StreamingPingPong, InProcess,
                   Client_AddMetadata<RandomBinaryMetadata<31>, 1>, NoOpMutator)
    ->Args({0, 0});
BENCHMARK_TEMPLATE(BM_StreamingPingPong, InProcess,
                   Client_AddMetadata<RandomBinaryMetadata<100>, 1>,
                   NoOpMutator)
    ->Args({0, 0});
BENCHMARK_TEMPLATE(BM_StreamingPingPong, InProcess,
                   Client_AddMetadata<RandomBinaryMetadata<10>, 2>, NoOpMutator)
    ->Args({0, 0});
BENCHMARK_TEMPLATE(BM_StreamingPingPong, InProcess,
                   Client_AddMetadata<RandomBinaryMetadata<31>, 2>, NoOpMutator)
    ->Args({0, 0});
BENCHMARK_TEMPLATE(BM_StreamingPingPong, InProcess,
                   Client_AddMetadata<RandomBinaryMetadata<100>, 2>,
                   NoOpMutator)
    ->Args({0, 0});
BENCHMARK_TEMPLATE(BM_StreamingPingPong, InProcess, NoOpMutator,
                   Server_AddInitialMetadata<RandomBinaryMetadata<10>, 1>)
    ->Args({0, 0});
BENCHMARK_TEMPLATE(BM_StreamingPingPong, InProcess, NoOpMutator,
                   Server_AddInitialMetadata<RandomBinaryMetadata<31>, 1>)
    ->Args({0, 0});
BENCHMARK_TEMPLATE(BM_StreamingPingPong, InProcess, NoOpMutator,
                   Server_AddInitialMetadata<RandomBinaryMetadata<100>, 1>)
    ->Args({0, 0});
BENCHMARK_TEMPLATE(BM_StreamingPingPong, InProcess,
                   Client_AddMetadata<RandomAsciiMetadata<10>, 1>, NoOpMutator)
    ->Args({0, 0});
BENCHMARK_TEMPLATE(BM_StreamingPingPong, InProcess,
                   Client_AddMetadata<RandomAsciiMetadata<31>, 1>, NoOpMutator)
    ->Args({0, 0});
BENCHMARK_TEMPLATE(BM_StreamingPingPong, InProcess,
                   Client_AddMetadata<RandomAsciiMetadata<100>, 1>, NoOpMutator)
    ->Args({0, 0});
BENCHMARK_TEMPLATE(BM_StreamingPingPong, InProcess, NoOpMutator,
                   Server_AddInitialMetadata<RandomAsciiMetadata<10>, 1>)
    ->Args({0, 0});
BENCHMARK_TEMPLATE(BM_StreamingPingPong, InProcess, NoOpMutator,
                   Server_AddInitialMetadata<RandomAsciiMetadata<31>, 1>)
    ->Args({0, 0});
BENCHMARK_TEMPLATE(BM_StreamingPingPong, InProcess, NoOpMutator,
                   Server_AddInitialMetadata<RandomAsciiMetadata<100>, 1>)
    ->Args({0, 0});
BENCHMARK_TEMPLATE(BM_StreamingPingPong, InProcess, NoOpMutator,
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
  ::benchmark::Initialize(&argc, argv);
  ::grpc::testing::InitTest(&argc, &argv, false);
  benchmark::RunTheBenchmarksNamespaced();
  return 0;
}
