/*
 *
 * Copyright 2017 gRPC authors.
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

/* Benchmark channel */

#include <benchmark/benchmark.h>
#include <grpc/grpc.h>
#include "test/core/util/test_config.h"
#include "test/cpp/microbenchmarks/helpers.h"
#include "test/cpp/util/test_config.h"

class ChannelDestroyerFixture {
 public:
  ChannelDestroyerFixture() {}
  virtual ~ChannelDestroyerFixture() {
    if (channel_) {
      grpc_channel_destroy(channel_);
    }
  }
  virtual void Init() = 0;

 protected:
  grpc_channel* channel_ = nullptr;
};

class InsecureChannelFixture : public ChannelDestroyerFixture {
 public:
  InsecureChannelFixture() {}
  void Init() override {
    channel_ = grpc_insecure_channel_create("localhost:1234", nullptr, nullptr);
  }
};

class LameChannelFixture : public ChannelDestroyerFixture {
 public:
  LameChannelFixture() {}
  void Init() override {
    channel_ = grpc_lame_client_channel_create(
        "localhost:1234", GRPC_STATUS_UNAUTHENTICATED, "blah");
  }
};

template <class Fixture>
static void BM_InsecureChannelCreateDestroy(benchmark::State& state) {
  // In order to test if channel creation time is affected by the number of
  // already existing channels, we create some initial channels here.
  Fixture initial_channels[512];
  for (int i = 0; i < state.range(0); i++) {
    initial_channels[i].Init();
  }
  for (auto _ : state) {
    Fixture channel;
    channel.Init();
  }
}
BENCHMARK_TEMPLATE(BM_InsecureChannelCreateDestroy, InsecureChannelFixture)
    ->Range(0, 512);
;
BENCHMARK_TEMPLATE(BM_InsecureChannelCreateDestroy, LameChannelFixture)
    ->Range(0, 512);
;

// Some distros have RunSpecifiedBenchmarks under the benchmark namespace,
// and others do not. This allows us to support both modes.
namespace benchmark {
void RunTheBenchmarksNamespaced() { RunSpecifiedBenchmarks(); }
}  // namespace benchmark

int main(int argc, char** argv) {
  grpc::testing::TestEnvironment env(argc, argv);
  LibraryInitializer libInit;
  ::benchmark::Initialize(&argc, argv);
  ::grpc::testing::InitTest(&argc, &argv, false);
  benchmark::RunTheBenchmarksNamespaced();
  return 0;
}
