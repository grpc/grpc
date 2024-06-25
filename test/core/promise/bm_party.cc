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

#include <benchmark/benchmark.h>

#include <grpc/grpc.h>

#include "src/core/lib/event_engine/default_event_engine.h"
#include "src/core/lib/promise/party.h"

namespace grpc_core {
namespace {

class TestParty final : public Party {
 public:
  TestParty() : Party(1) {}
  ~TestParty() override {}
  std::string DebugTag() const override { return "TestParty"; }

  using Party::IncrementRefCount;
  using Party::Unref;

  bool RunParty() override {
    promise_detail::Context<grpc_event_engine::experimental::EventEngine>
        ee_ctx(ee_.get());
    return Party::RunParty();
  }

  void PartyOver() override {
    {
      promise_detail::Context<grpc_event_engine::experimental::EventEngine>
          ee_ctx(ee_.get());
      CancelRemainingParticipants();
    }
    delete this;
  }

 private:
  grpc_event_engine::experimental::EventEngine* event_engine() const final {
    return ee_.get();
  }

  std::shared_ptr<grpc_event_engine::experimental::EventEngine> ee_ =
      grpc_event_engine::experimental::GetDefaultEventEngine();
};

void BM_PartyCreate(benchmark::State& state) {
  for (auto _ : state) {
    auto* party = new TestParty();
    party->Unref();
  }
}
BENCHMARK(BM_PartyCreate);

void BM_AddParticipant(benchmark::State& state) {
  auto* party = new TestParty();
  for (auto _ : state) {
    party->Spawn(
        "participant", []() { return Success{}; }, [](StatusFlag) {});
  }
  party->Unref();
}
BENCHMARK(BM_AddParticipant);

void BM_WakeupParticipant(benchmark::State& state) {
  auto* party = new TestParty();
  party->Spawn(
      "driver",
      [&state, w = IntraActivityWaiter()]() mutable -> Poll<StatusFlag> {
        w.pending();
        if (state.KeepRunning()) {
          w.Wake();
          return Pending{};
        }
        return Success{};
      },
      [party](StatusFlag) { party->Unref(); });
}
BENCHMARK(BM_WakeupParticipant);

}  // namespace
}  // namespace grpc_core

// Some distros have RunSpecifiedBenchmarks under the benchmark namespace,
// and others do not. This allows us to support both modes.
namespace benchmark {
void RunTheBenchmarksNamespaced() { RunSpecifiedBenchmarks(); }
}  // namespace benchmark

int main(int argc, char** argv) {
  ::benchmark::Initialize(&argc, argv);
  grpc_init();
  {
    auto ee = grpc_event_engine::experimental::GetDefaultEventEngine();
    benchmark::RunTheBenchmarksNamespaced();
  }
  grpc_shutdown();
  return 0;
}
