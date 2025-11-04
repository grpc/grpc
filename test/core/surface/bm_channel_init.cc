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
#include <grpc/support/string_util.h>

#include "src/core/lib/channel/channel_stack.h"
#include "src/core/lib/surface/channel_init.h"

namespace grpc_core {
namespace {

const grpc_channel_filter* FilterIdx(size_t idx) {
  static auto* names = new std::vector<const char*>();
  static auto* filters = new std::vector<const grpc_channel_filter*>;
  static auto* name_factories =
      new std::vector<std::unique_ptr<UniqueTypeName::Factory>>();
  while (filters->size() <= idx) {
    const char* name =
        gpr_strdup(absl::StrCat("filter", filters->size()).c_str());
    names->emplace_back(name);
    name_factories->emplace_back(
        std::make_unique<UniqueTypeName::Factory>(name));
    auto unique_type_name = name_factories->back()->Create();
    filters->push_back(new grpc_channel_filter{
        nullptr, nullptr, 0, nullptr, nullptr, nullptr, 0, nullptr, nullptr,
        nullptr, nullptr, unique_type_name});
  }
  return (*filters)[idx];
}

struct OrderedTopToBottom {
  void RegisterNodes(ChannelInit::Builder& b, size_t nodes) {
    b.RegisterFilter(GRPC_CLIENT_CHANNEL, FilterIdx(0));
    for (size_t i = 1; i < nodes; ++i) {
      b.RegisterFilter(GRPC_CLIENT_CHANNEL, FilterIdx(i))
          .After({FilterIdx(i - 1)->name});
    }
  }
};

struct OrderedBottomToTop {
  void RegisterNodes(ChannelInit::Builder& b, size_t nodes) {
    for (size_t i = 0; i < nodes - 1; ++i) {
      b.RegisterFilter(GRPC_CLIENT_CHANNEL, FilterIdx(i))
          .After({FilterIdx(i + 1)->name});
    }
    b.RegisterFilter(GRPC_CLIENT_CHANNEL, FilterIdx(nodes - 1));
  }
};

struct Unordered {
  void RegisterNodes(ChannelInit::Builder& b, size_t nodes) {
    for (size_t i = 0; i < nodes; ++i) {
      b.RegisterFilter(GRPC_CLIENT_CHANNEL, FilterIdx(i));
    }
  }
};

struct AllBeforeFirst {
  void RegisterNodes(ChannelInit::Builder& b, size_t nodes) {
    b.RegisterFilter(GRPC_CLIENT_CHANNEL, FilterIdx(0));
    for (size_t i = 1; i < nodes; ++i) {
      b.RegisterFilter(GRPC_CLIENT_CHANNEL, FilterIdx(i))
          .Before({FilterIdx(0)->name});
    }
  }
};

struct AllAfterFirst {
  void RegisterNodes(ChannelInit::Builder& b, size_t nodes) {
    b.RegisterFilter(GRPC_CLIENT_CHANNEL, FilterIdx(0));
    for (size_t i = 1; i < nodes; ++i) {
      b.RegisterFilter(GRPC_CLIENT_CHANNEL, FilterIdx(i))
          .After({FilterIdx(0)->name});
    }
  }
};

template <class Shape>
void BM_ChannelInitBuilder(benchmark::State& state) {
  Shape shape;
  for (auto _ : state) {
    ChannelInit::Builder b;
    shape.RegisterNodes(b, state.range(0));
    b.Build();
  }
}

BENCHMARK(BM_ChannelInitBuilder<OrderedTopToBottom>)
    ->RangeMultiplier(4)
    ->Range(1, 256);
BENCHMARK(BM_ChannelInitBuilder<OrderedBottomToTop>)
    ->RangeMultiplier(4)
    ->Range(1, 256);
BENCHMARK(BM_ChannelInitBuilder<Unordered>)->RangeMultiplier(4)->Range(1, 256);
BENCHMARK(BM_ChannelInitBuilder<AllBeforeFirst>)
    ->RangeMultiplier(4)
    ->Range(1, 256);
BENCHMARK(BM_ChannelInitBuilder<AllAfterFirst>)
    ->RangeMultiplier(4)
    ->Range(1, 256);

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
