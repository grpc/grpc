// Copyright 2017 gRPC authors.
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

// Benchmark ChannelArgs comparison performance between grpc_channel_args and
// grpc_core::ChannelArgs

#include <benchmark/benchmark.h>
#include <grpcpp/support/channel_arguments.h>

#include <random>

#include "absl/container/btree_map.h"
#include "src/core/lib/channel/channel_args.h"

const char kKey[] = "a very long key";
const char kValue[] = "a very long value";

void BM_ChannelArgs(benchmark::State& state) {
  grpc_core::ChannelArgs arg1, arg2;
  arg1 = arg1.Set(kKey, kValue);
  arg2 = arg2.Set(kKey, kValue);
  for (auto s : state) {
    benchmark::DoNotOptimize(arg1 < arg2);
  }
}
BENCHMARK(BM_ChannelArgs);

void BM_grpc_channel_args(benchmark::State& state) {
  grpc_channel_args arg1, arg2;
  grpc::ChannelArguments xargs;
  xargs.SetString(kKey, kValue);
  xargs.SetChannelArgs(&arg1);
  xargs.SetChannelArgs(&arg2);
  for (auto s : state) {
    benchmark::DoNotOptimize(grpc_channel_args_compare(&arg1, &arg2));
  }
}
BENCHMARK(BM_grpc_channel_args);

void BM_ChannelArgsAsKeyIntoMap(benchmark::State& state) {
  std::map<grpc_core::ChannelArgs, int> m;
  std::vector<grpc_core::ChannelArgs> v;
  for (int i = 0; i < 10000; i++) {
    const auto& a = grpc_core::ChannelArgs().Set(kKey, i);
    m[a] = i;
    v.push_back(a);
  }
  std::shuffle(v.begin(), v.end(), std::mt19937(std::random_device()()));
  size_t n = 0;
  for (auto s : state) {
    benchmark::DoNotOptimize(m.find(v[n++ % v.size()]));
  }
}
BENCHMARK(BM_ChannelArgsAsKeyIntoMap);

void BM_ChannelArgsAsKeyIntoBTree(benchmark::State& state) {
  absl::btree_map<grpc_core::ChannelArgs, int> m;
  std::vector<grpc_core::ChannelArgs> v;
  for (int i = 0; i < 10000; i++) {
    const auto& a = grpc_core::ChannelArgs().Set(kKey, i);
    m[a] = i;
    v.push_back(a);
  }
  std::shuffle(v.begin(), v.end(), std::mt19937(std::random_device()()));
  size_t n = 0;
  for (auto s : state) {
    benchmark::DoNotOptimize(m.find(v[n++ % v.size()]));
  }
}
BENCHMARK(BM_ChannelArgsAsKeyIntoBTree);

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
