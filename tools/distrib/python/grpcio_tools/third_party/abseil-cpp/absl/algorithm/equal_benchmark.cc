// Copyright 2017 The Abseil Authors.
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

#include <cstdint>
#include <cstring>

#include "absl/algorithm/algorithm.h"
#include "benchmark/benchmark.h"

namespace {

// The range of sequence sizes to benchmark.
constexpr int kMinBenchmarkSize = 1024;
constexpr int kMaxBenchmarkSize = 8 * 1024 * 1024;

// A user-defined type for use in equality benchmarks. Note that we expect
// std::memcmp to win for this type: libstdc++'s std::equal only defers to
// memcmp for integral types. This is because it is not straightforward to
// guarantee that std::memcmp would produce a result "as-if" compared by
// operator== for other types (example gotchas: NaN floats, structs with
// padding).
struct EightBits {
  explicit EightBits(int /* unused */) : data(0) {}
  bool operator==(const EightBits& rhs) const { return data == rhs.data; }
  uint8_t data;
};

template <typename T>
void BM_absl_equal_benchmark(benchmark::State& state) {
  std::vector<T> xs(state.range(0), T(0));
  std::vector<T> ys = xs;
  while (state.KeepRunning()) {
    const bool same = absl::equal(xs.begin(), xs.end(), ys.begin(), ys.end());
    benchmark::DoNotOptimize(same);
  }
}

template <typename T>
void BM_std_equal_benchmark(benchmark::State& state) {
  std::vector<T> xs(state.range(0), T(0));
  std::vector<T> ys = xs;
  while (state.KeepRunning()) {
    const bool same = std::equal(xs.begin(), xs.end(), ys.begin());
    benchmark::DoNotOptimize(same);
  }
}

template <typename T>
void BM_memcmp_benchmark(benchmark::State& state) {
  std::vector<T> xs(state.range(0), T(0));
  std::vector<T> ys = xs;
  while (state.KeepRunning()) {
    const bool same =
        std::memcmp(xs.data(), ys.data(), xs.size() * sizeof(T)) == 0;
    benchmark::DoNotOptimize(same);
  }
}

// The expectation is that the compiler should be able to elide the equality
// comparison altogether for sufficiently simple types.
template <typename T>
void BM_absl_equal_self_benchmark(benchmark::State& state) {
  std::vector<T> xs(state.range(0), T(0));
  while (state.KeepRunning()) {
    const bool same = absl::equal(xs.begin(), xs.end(), xs.begin(), xs.end());
    benchmark::DoNotOptimize(same);
  }
}

BENCHMARK_TEMPLATE(BM_absl_equal_benchmark, uint8_t)
    ->Range(kMinBenchmarkSize, kMaxBenchmarkSize);
BENCHMARK_TEMPLATE(BM_std_equal_benchmark, uint8_t)
    ->Range(kMinBenchmarkSize, kMaxBenchmarkSize);
BENCHMARK_TEMPLATE(BM_memcmp_benchmark, uint8_t)
    ->Range(kMinBenchmarkSize, kMaxBenchmarkSize);
BENCHMARK_TEMPLATE(BM_absl_equal_self_benchmark, uint8_t)
    ->Range(kMinBenchmarkSize, kMaxBenchmarkSize);

BENCHMARK_TEMPLATE(BM_absl_equal_benchmark, uint16_t)
    ->Range(kMinBenchmarkSize, kMaxBenchmarkSize);
BENCHMARK_TEMPLATE(BM_std_equal_benchmark, uint16_t)
    ->Range(kMinBenchmarkSize, kMaxBenchmarkSize);
BENCHMARK_TEMPLATE(BM_memcmp_benchmark, uint16_t)
    ->Range(kMinBenchmarkSize, kMaxBenchmarkSize);
BENCHMARK_TEMPLATE(BM_absl_equal_self_benchmark, uint16_t)
    ->Range(kMinBenchmarkSize, kMaxBenchmarkSize);

BENCHMARK_TEMPLATE(BM_absl_equal_benchmark, uint32_t)
    ->Range(kMinBenchmarkSize, kMaxBenchmarkSize);
BENCHMARK_TEMPLATE(BM_std_equal_benchmark, uint32_t)
    ->Range(kMinBenchmarkSize, kMaxBenchmarkSize);
BENCHMARK_TEMPLATE(BM_memcmp_benchmark, uint32_t)
    ->Range(kMinBenchmarkSize, kMaxBenchmarkSize);
BENCHMARK_TEMPLATE(BM_absl_equal_self_benchmark, uint32_t)
    ->Range(kMinBenchmarkSize, kMaxBenchmarkSize);

BENCHMARK_TEMPLATE(BM_absl_equal_benchmark, uint64_t)
    ->Range(kMinBenchmarkSize, kMaxBenchmarkSize);
BENCHMARK_TEMPLATE(BM_std_equal_benchmark, uint64_t)
    ->Range(kMinBenchmarkSize, kMaxBenchmarkSize);
BENCHMARK_TEMPLATE(BM_memcmp_benchmark, uint64_t)
    ->Range(kMinBenchmarkSize, kMaxBenchmarkSize);
BENCHMARK_TEMPLATE(BM_absl_equal_self_benchmark, uint64_t)
    ->Range(kMinBenchmarkSize, kMaxBenchmarkSize);

BENCHMARK_TEMPLATE(BM_absl_equal_benchmark, EightBits)
    ->Range(kMinBenchmarkSize, kMaxBenchmarkSize);
BENCHMARK_TEMPLATE(BM_std_equal_benchmark, EightBits)
    ->Range(kMinBenchmarkSize, kMaxBenchmarkSize);
BENCHMARK_TEMPLATE(BM_memcmp_benchmark, EightBits)
    ->Range(kMinBenchmarkSize, kMaxBenchmarkSize);
BENCHMARK_TEMPLATE(BM_absl_equal_self_benchmark, EightBits)
    ->Range(kMinBenchmarkSize, kMaxBenchmarkSize);

}  // namespace
