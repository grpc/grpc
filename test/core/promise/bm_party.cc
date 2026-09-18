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
#include "src/core/lib/resource_quota/arena.h"

namespace grpc_core {
namespace {

void BM_PartyCreate(benchmark::State& state) {
  auto arena = SimpleArenaAllocator()->MakeArena();
  arena->SetContext(
      grpc_event_engine::experimental::GetDefaultEventEngine().get());
  for (auto _ : state) {
    Party::Make(arena);
  }
}
BENCHMARK(BM_PartyCreate);

void BM_AddParticipant(benchmark::State& state) {
  auto arena = SimpleArenaAllocator()->MakeArena();
  arena->SetContext(
      grpc_event_engine::experimental::GetDefaultEventEngine().get());
  auto party = Party::Make(arena);
  for (auto _ : state) {
    party->Spawn("participant", []() { return Success{}; }, [](StatusFlag) {});
  }
}
BENCHMARK(BM_AddParticipant);

void BM_WakeupParticipant(benchmark::State& state) {
  auto arena = SimpleArenaAllocator()->MakeArena();
  arena->SetContext(
      grpc_event_engine::experimental::GetDefaultEventEngine().get());
  auto party = Party::Make(arena);
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
      [party](StatusFlag) {});
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
