/*
 *
 * Copyright 2015 gRPC authors.
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

/* This benchmark exists to show that byte-buffer copy is size-independent */

#include <memory>

#include <benchmark/benchmark.h>
#include <grpcpp/impl/grpc_library.h>
#include <grpcpp/support/byte_buffer.h>
#include "test/cpp/microbenchmarks/helpers.h"
#include "test/cpp/util/test_config.h"

namespace grpc {
namespace testing {

auto& force_library_initialization = Library::get();

static void BM_ByteBuffer_Copy(benchmark::State& state) {
  int num_slices = state.range(0);
  size_t slice_size = state.range(1);
  std::vector<grpc::Slice> slices;
  while (num_slices > 0) {
    num_slices--;
    std::unique_ptr<char[]> buf(new char[slice_size]);
    memset(buf.get(), 0, slice_size);
    slices.emplace_back(buf.get(), slice_size);
  }
  grpc::ByteBuffer bb(slices.data(), num_slices);
  while (state.KeepRunning()) {
    grpc::ByteBuffer cc(bb);
  }
}
BENCHMARK(BM_ByteBuffer_Copy)->Ranges({{1, 64}, {1, 1024 * 1024}});

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
