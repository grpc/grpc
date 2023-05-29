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

const std::vector<uint8_t> MakeInput(int min, int max) {
  std::vector<uint8_t> v;
  std::uniform_int_distribution<> distribution(min, max);
  static std::mt19937 rd(0);
  for (int i = 0; i < 1024 * 1024; i++) {
    v.push_back(distribution(rd));
  }
  grpc_core::Slice s = grpc_core::Slice::FromCopiedBuffer(v);
  grpc_core::Slice c(grpc_chttp2_huffman_compress(s.c_slice()));
  return std::vector<uint8_t>(c.begin(), c.end());
}

const std::vector<uint8_t>* AllChars() {
  static const grpc_core::NoDestruct<std::vector<uint8_t>> v(MakeInput(0, 255));
  return v.get();
}

const std::vector<uint8_t>* AsciiChars() {
  static const grpc_core::NoDestruct<std::vector<uint8_t>> v(
      MakeInput(32, 126));
  return v.get();
}

const std::vector<uint8_t>* AlphaChars() {
  static const grpc_core::NoDestruct<std::vector<uint8_t>> v(
      MakeInput('a', 'z'));
  return v.get();
}

static void BM_Decode(benchmark::State& state,
                      const std::vector<uint8_t>* input) {
  std::vector<uint8_t> output;
  auto add = [&output](uint8_t c) { output.push_back(c); };
  for (auto _ : state) {
    output.clear();
    grpc_core::HuffDecoder<decltype(add)>(add, input->data(),
                                          input->data() + input->size())
        .Run();
  }
}
BENCHMARK_CAPTURE(BM_Decode, all_chars, AllChars());
BENCHMARK_CAPTURE(BM_Decode, ascii_chars, AsciiChars());
BENCHMARK_CAPTURE(BM_Decode, alpha_chars, AlphaChars());

// Some distros have RunSpecifiedBenchmarks under the benchmark namespace,
// and others do not. This allows us to support both modes.
namespace benchmark {
void RunTheBenchmarksNamespaced() { RunSpecifiedBenchmarks(); }
}  // namespace benchmark

int main(int argc, char** argv) {
  grpc::testing::TestEnvironment env(&argc, argv);
  benchmark::Initialize(&argc, argv);
  benchmark::RunTheBenchmarksNamespaced();
  return 0;
}
