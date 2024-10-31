//
//
// Copyright 2024 gRPC authors.
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
//
//

// Benchmark gRPC end2end in various configurations for chaotic good
// TODO(ctiller): fold back into bm_fullstack_unary_ping_pong.cc once chaotic
// good can run without custom experiment configuration.

#include "src/cpp/ext/chaotic_good.h"
#include "test/core/test_util/test_config.h"
#include "test/cpp/microbenchmarks/fullstack_unary_ping_pong.h"
#include "test/cpp/util/test_config.h"

namespace grpc {
namespace testing {

class ChaoticGoodFixture : public BaseFixture {
 public:
  explicit ChaoticGoodFixture(
      Service* service,
      const FixtureConfiguration& config = FixtureConfiguration()) {
    auto address = MakeAddress(&port_);
    ServerBuilder b;
    if (!address.empty()) {
      b.AddListeningPort(address, ChaoticGoodInsecureServerCredentials());
    }
    cq_ = b.AddCompletionQueue(true);
    b.RegisterService(service);
    config.ApplyCommonServerBuilderConfig(&b);
    server_ = b.BuildAndStart();
    ChannelArguments args;
    config.ApplyCommonChannelArguments(&args);
    if (!address.empty()) {
      channel_ = grpc::CreateCustomChannel(
          address, ChaoticGoodInsecureChannelCredentials(), args);
    } else {
      channel_ = server_->InProcessChannel(args);
    }
  }

  ~ChaoticGoodFixture() override {
    server_->Shutdown(grpc_timeout_milliseconds_to_deadline(0));
    cq_->Shutdown();
    void* tag;
    bool ok;
    while (cq_->Next(&tag, &ok)) {
    }
    grpc_recycle_unused_port(port_);
  }

  ServerCompletionQueue* cq() { return cq_.get(); }
  std::shared_ptr<Channel> channel() { return channel_; }

 private:
  static std::string MakeAddress(int* port) {
    *port = grpc_pick_unused_port_or_die();
    std::stringstream addr;
    addr << "localhost:" << *port;
    return addr.str();
  }

  std::unique_ptr<Server> server_;
  std::unique_ptr<ServerCompletionQueue> cq_;
  std::shared_ptr<Channel> channel_;
  int port_;
};

//******************************************************************************
// CONFIGURATIONS
//

// Replace "benchmark::internal::Benchmark" with "::testing::Benchmark" to use
// internal microbenchmarking tooling
static void SweepSizesArgs(benchmark::internal::Benchmark* b) {
  b->Args({0, 0});
  for (int i = 1; i <= 128 * 1024 * 1024; i *= 8) {
    b->Args({i, 0});
    b->Args({0, i});
    b->Args({i, i});
  }
}

BENCHMARK_TEMPLATE(BM_UnaryPingPong, ChaoticGoodFixture, NoOpMutator,
                   NoOpMutator)
    ->Apply(SweepSizesArgs);

}  // namespace testing
}  // namespace grpc

// Some distros have RunSpecifiedBenchmarks under the benchmark namespace,
// and others do not. This allows us to support both modes.
namespace benchmark {
void RunTheBenchmarksNamespaced() { RunSpecifiedBenchmarks(); }
}  // namespace benchmark

int main(int argc, char** argv) {
  grpc_core::ForceEnableExperiment("event_engine_client", true);
  grpc_core::ForceEnableExperiment("event_engine_listener", true);
  grpc_core::ForceEnableExperiment("promise_based_client_call", true);
  grpc_core::ForceEnableExperiment("chaotic_good", true);
  grpc::testing::TestEnvironment env(&argc, argv);
  LibraryInitializer libInit;
  ::benchmark::Initialize(&argc, argv);
  grpc::testing::InitTest(&argc, &argv, false);
  benchmark::RunTheBenchmarksNamespaced();
  return 0;
}
