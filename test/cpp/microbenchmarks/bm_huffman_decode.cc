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

#include "absl/strings/escaping.h"

#include "src/core/ext/transport/chttp2/transport/bin_encoder.h"
#include "src/core/ext/transport/chttp2/transport/decode_huff.h"
#include "src/core/lib/gprpp/no_destruct.h"
#include "src/core/lib/slice/slice.h"
#include "test/core/util/test_config.h"
#include "test/cpp/microbenchmarks/huffman_geometries/index.h"

std::vector<uint8_t> MakeInput(int min, int max) {
  std::vector<uint8_t> v;
  std::uniform_int_distribution<> distribution(min, max);
  static std::mt19937 rd(0);
  v.reserve(1024 * 1024);
  for (int i = 0; i < 1024 * 1024; i++) {
    v.push_back(distribution(rd));
  }
  grpc_core::Slice s = grpc_core::Slice::FromCopiedBuffer(v);
  grpc_core::Slice c(grpc_chttp2_huffman_compress(s.c_slice()));
  return std::vector<uint8_t>(c.begin(), c.end());
}

std::vector<uint8_t> MakeBase64() {
  auto src = MakeInput(0, 255);
  auto s = absl::Base64Escape(
      absl::string_view(reinterpret_cast<char*>(src.data()), src.size()));
  return std::vector<uint8_t>(s.begin(), s.end());
}

const std::vector<uint8_t>& AllChars() {
  static const auto* const data = new std::vector<uint8_t>(MakeInput(0, 255));
  return *data;
};
const std::vector<uint8_t>& AsciiChars() {
  static const auto* const data = new std::vector<uint8_t>(MakeInput(32, 126));
  return *data;
};
const std::vector<uint8_t>& AlphaChars() {
  static const auto* const data = new std::vector<uint8_t>(MakeInput('a', 'z'));
  return *data;
};
const std::vector<uint8_t>& Base64Chars() {
  static const auto* const data = new std::vector<uint8_t>(MakeBase64());
  return *data;
};

using CharSet = const std::vector<uint8_t>& (*)();

template <template <typename Sink> class Decoder>
static void BM_Decode(benchmark::State& state, CharSet chars_gen) {
  const std::vector<uint8_t>& chars = chars_gen();
  std::vector<uint8_t> output;
  auto add = [&output](uint8_t c) { output.push_back(c); };
  for (auto _ : state) {
    output.clear();
    Decoder<decltype(add)>(add, chars.data(), chars.data() + chars.size())
        .Run();
  }
}

#define DECL_BENCHMARK(cls, name)                     \
  static auto name = BM_Decode<cls>;                  \
  BENCHMARK_CAPTURE(name, all_chars, AllChars);       \
  BENCHMARK_CAPTURE(name, base64_chars, Base64Chars); \
  BENCHMARK_CAPTURE(name, ascii_chars, AsciiChars);   \
  BENCHMARK_CAPTURE(name, alpha_chars, AlphaChars)

DECL_HUFFMAN_VARIANTS();

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
