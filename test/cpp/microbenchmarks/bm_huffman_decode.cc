// Copyright 2022 gRPC authors.
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

#include <cstdint>
#include <random>

#include <benchmark/benchmark.h>

#include "src/core/ext/transport/chttp2/transport/bin_encoder.h"
#include "src/core/ext/transport/chttp2/transport/decode_huff.h"
#include "src/core/lib/gprpp/no_destruct.h"
#include "src/core/lib/slice/slice.h"
#include "test/core/util/test_config.h"

const std::vector<uint8_t>* Input() {
  static const grpc_core::NoDestruct<std::vector<uint8_t>> v([]() {
    std::vector<uint8_t> v;
    std::mt19937 rd(0);
    std::uniform_int_distribution<> dist_ty(0, 100);
    std::uniform_int_distribution<> dist_byte(0, 255);
    std::uniform_int_distribution<> dist_normal(32, 126);
    for (int i = 0; i < 1024 * 1024; i++) {
      if (dist_ty(rd) == 1) {
        v.push_back(dist_byte(rd));
      } else {
        v.push_back(dist_normal(rd));
      }
    }
    grpc_core::Slice s = grpc_core::Slice::FromCopiedBuffer(v);
    grpc_core::Slice c(grpc_chttp2_huffman_compress(s.c_slice()));
    return std::vector<uint8_t>(c.begin(), c.end());
  }());
  return v.get();
}

static void BM_Decode(benchmark::State& state) {
  std::vector<uint8_t> output;
  auto add = [&output](uint8_t c) { output.push_back(c); };
  for (auto _ : state) {
    output.clear();
    grpc_core::HuffDecoder<decltype(add)>(add, Input()->data(),
                                          Input()->data() + Input()->size())
        .Run();
  }
}
BENCHMARK(BM_Decode);

// Some distros have RunSpecifiedBenchmarks under the benchmark namespace,
// and others do not. This allows us to support both modes.
namespace benchmark {
void RunTheBenchmarksNamespaced() { RunSpecifiedBenchmarks(); }
}  // namespace benchmark

int main(int argc, char** argv) {
  grpc::testing::TestEnvironment env(&argc, argv);
  benchmark::Initialize(&argc, argv);
  Input();  // Force initialization of input data.
  benchmark::RunTheBenchmarksNamespaced();
  return 0;
}
