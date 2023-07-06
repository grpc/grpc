// Copyright 2018 The Abseil Authors.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      https://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "absl/strings/str_cat.h"

#include <cstdint>
#include <string>

#include "benchmark/benchmark.h"
#include "absl/strings/substitute.h"

namespace {

const char kStringOne[] = "Once Upon A Time, ";
const char kStringTwo[] = "There was a string benchmark";

// We want to include negative numbers in the benchmark, so this function
// is used to count 0, 1, -1, 2, -2, 3, -3, ...
inline int IncrementAlternatingSign(int i) {
  return i > 0 ? -i : 1 - i;
}

void BM_Sum_By_StrCat(benchmark::State& state) {
  int i = 0;
  char foo[100];
  for (auto _ : state) {
    // NOLINTNEXTLINE(runtime/printf)
    strcpy(foo, absl::StrCat(kStringOne, i, kStringTwo, i * 65536ULL).c_str());
    int sum = 0;
    for (char* f = &foo[0]; *f != 0; ++f) {
      sum += *f;
    }
    benchmark::DoNotOptimize(sum);
    i = IncrementAlternatingSign(i);
  }
}
BENCHMARK(BM_Sum_By_StrCat);

void BM_StrCat_By_snprintf(benchmark::State& state) {
  int i = 0;
  char on_stack[1000];
  for (auto _ : state) {
    snprintf(on_stack, sizeof(on_stack), "%s %s:%d", kStringOne, kStringTwo, i);
    i = IncrementAlternatingSign(i);
  }
}
BENCHMARK(BM_StrCat_By_snprintf);

void BM_StrCat_By_Strings(benchmark::State& state) {
  int i = 0;
  for (auto _ : state) {
    std::string result =
        std::string(kStringOne) + " " + kStringTwo + ":" + absl::StrCat(i);
    benchmark::DoNotOptimize(result);
    i = IncrementAlternatingSign(i);
  }
}
BENCHMARK(BM_StrCat_By_Strings);

void BM_StrCat_By_StringOpPlus(benchmark::State& state) {
  int i = 0;
  for (auto _ : state) {
    std::string result = kStringOne;
    result += " ";
    result += kStringTwo;
    result += ":";
    result += absl::StrCat(i);
    benchmark::DoNotOptimize(result);
    i = IncrementAlternatingSign(i);
  }
}
BENCHMARK(BM_StrCat_By_StringOpPlus);

void BM_StrCat_By_StrCat(benchmark::State& state) {
  int i = 0;
  for (auto _ : state) {
    std::string result = absl::StrCat(kStringOne, " ", kStringTwo, ":", i);
    benchmark::DoNotOptimize(result);
    i = IncrementAlternatingSign(i);
  }
}
BENCHMARK(BM_StrCat_By_StrCat);

void BM_HexCat_By_StrCat(benchmark::State& state) {
  int i = 0;
  for (auto _ : state) {
    std::string result =
        absl::StrCat(kStringOne, " ", absl::Hex(int64_t{i} + 0x10000000));
    benchmark::DoNotOptimize(result);
    i = IncrementAlternatingSign(i);
  }
}
BENCHMARK(BM_HexCat_By_StrCat);

void BM_HexCat_By_Substitute(benchmark::State& state) {
  int i = 0;
  for (auto _ : state) {
    std::string result = absl::Substitute(
        "$0 $1", kStringOne, reinterpret_cast<void*>(int64_t{i} + 0x10000000));
    benchmark::DoNotOptimize(result);
    i = IncrementAlternatingSign(i);
  }
}
BENCHMARK(BM_HexCat_By_Substitute);

void BM_FloatToString_By_StrCat(benchmark::State& state) {
  int i = 0;
  float foo = 0.0f;
  for (auto _ : state) {
    std::string result = absl::StrCat(foo += 1.001f, " != ", int64_t{i});
    benchmark::DoNotOptimize(result);
    i = IncrementAlternatingSign(i);
  }
}
BENCHMARK(BM_FloatToString_By_StrCat);

void BM_DoubleToString_By_SixDigits(benchmark::State& state) {
  int i = 0;
  double foo = 0.0;
  for (auto _ : state) {
    std::string result =
        absl::StrCat(absl::SixDigits(foo += 1.001), " != ", int64_t{i});
    benchmark::DoNotOptimize(result);
    i = IncrementAlternatingSign(i);
  }
}
BENCHMARK(BM_DoubleToString_By_SixDigits);

template <typename... Chunks>
void BM_StrAppendImpl(benchmark::State& state, size_t total_bytes,
                      Chunks... chunks) {
  for (auto s : state) {
    std::string result;
    while (result.size() < total_bytes) {
      absl::StrAppend(&result, chunks...);
      benchmark::DoNotOptimize(result);
    }
  }
}

void BM_StrAppend(benchmark::State& state) {
  const int total_bytes = state.range(0);
  const int chunks_at_a_time = state.range(1);
  const absl::string_view kChunk = "0123456789";

  switch (chunks_at_a_time) {
    case 1:
      return BM_StrAppendImpl(state, total_bytes, kChunk);
    case 2:
      return BM_StrAppendImpl(state, total_bytes, kChunk, kChunk);
    case 4:
      return BM_StrAppendImpl(state, total_bytes, kChunk, kChunk, kChunk,
                              kChunk);
    case 8:
      return BM_StrAppendImpl(state, total_bytes, kChunk, kChunk, kChunk,
                              kChunk, kChunk, kChunk, kChunk, kChunk);
    default:
      std::abort();
  }
}

template <typename B>
void StrAppendConfig(B* benchmark) {
  for (int bytes : {10, 100, 1000, 10000}) {
    for (int chunks : {1, 2, 4, 8}) {
      // Only add the ones that divide properly. Otherwise we are over counting.
      if (bytes % (10 * chunks) == 0) {
        benchmark->Args({bytes, chunks});
      }
    }
  }
}

BENCHMARK(BM_StrAppend)->Apply(StrAppendConfig);

}  // namespace
