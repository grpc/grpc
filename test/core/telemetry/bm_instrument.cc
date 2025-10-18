// Copyright 2025 gRPC authors.
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

#include <benchmark/benchmark.h>

#include <random>
#include <thread>

#include "src/core/telemetry/instrument.h"

namespace grpc_core {
namespace {

class LowContentionDomain : public InstrumentDomain<LowContentionDomain> {
 public:
  using Backend = LowContentionBackend;
  static constexpr absl::string_view kName = "low_contention";
  static constexpr auto kLabels = Labels();
  static inline const auto kCounter =
      RegisterCounter("low_contention", "Desc", "unit");
};

class HighContentionDomain : public InstrumentDomain<HighContentionDomain> {
 public:
  using Backend = HighContentionBackend;
  static constexpr absl::string_view kName = "high_contention";
  static constexpr auto kLabels = Labels();
  static inline const auto kCounter =
      RegisterCounter("high_contention", "Desc", "unit");
};

void BM_IncrementLowContentionInstrument(benchmark::State& state) {
  auto storage = LowContentionDomain::GetStorage(CreateCollectionScope({}, {}));
  for (auto _ : state) {
    storage->Increment(LowContentionDomain::kCounter);
  }
}
BENCHMARK(BM_IncrementLowContentionInstrument)->ThreadRange(1, 64);

void BM_IncrementHighContentionInstrument(benchmark::State& state) {
  auto storage =
      HighContentionDomain::GetStorage(CreateCollectionScope({}, {}));
  for (auto _ : state) {
    storage->Increment(HighContentionDomain::kCounter);
  }
}
BENCHMARK(BM_IncrementHighContentionInstrument)->ThreadRange(1, 64);

}  // namespace
}  // namespace grpc_core

// Some distros have RunSpecifiedBenchmarks under the benchmark namespace,
// and others do not. This allows us to support both modes.
namespace benchmark {
void RunTheBenchmarksNamespaced() { RunSpecifiedBenchmarks(); }
}  // namespace benchmark

int main(int argc, char** argv) {
  ::benchmark::Initialize(&argc, argv);
  benchmark::RunTheBenchmarksNamespaced();
  return 0;
}
