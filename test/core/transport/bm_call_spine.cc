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
#include "src/core/lib/resource_quota/resource_quota.h"
#include "test/core/transport/call_spine_benchmarks.h"

namespace grpc_core {

class CallSpineFixture {
 public:
  BenchmarkCall MakeCall() {
    auto arena = arena_allocator_->MakeArena();
    arena->SetContext<grpc_event_engine::experimental::EventEngine>(
        event_engine_.get());
    auto p = MakeCallPair(Arena::MakePooledForOverwrite<ClientMetadata>(),
                          std::move(arena));
    return {std::move(p.initiator), p.handler.StartCall()};
  }

  ServerMetadataHandle MakeServerInitialMetadata() {
    return Arena::MakePooledForOverwrite<ServerMetadata>();
  }

  MessageHandle MakePayload() { return Arena::MakePooled<Message>(); }

  ServerMetadataHandle MakeServerTrailingMetadata() {
    return Arena::MakePooledForOverwrite<ServerMetadata>();
  }

 private:
  std::shared_ptr<grpc_event_engine::experimental::EventEngine> event_engine_ =
      grpc_event_engine::experimental::GetDefaultEventEngine();
  RefCountedPtr<CallArenaAllocator> arena_allocator_ =
      MakeRefCounted<CallArenaAllocator>(
          ResourceQuota::Default()->memory_quota()->CreateMemoryAllocator(
              "test-allocator"),
          1024);
};
GRPC_CALL_SPINE_BENCHMARK(CallSpineFixture);

class ForwardCallFixture {
 public:
  BenchmarkCall MakeCall() {
    auto arena1 = arena_allocator_->MakeArena();
    auto arena2 = arena_allocator_->MakeArena();
    arena1->SetContext<grpc_event_engine::experimental::EventEngine>(
        event_engine_.get());
    arena2->SetContext<grpc_event_engine::experimental::EventEngine>(
        event_engine_.get());
    auto p1 = MakeCallPair(Arena::MakePooledForOverwrite<ClientMetadata>(),
                           std::move(arena1));
    auto p2 = MakeCallPair(Arena::MakePooledForOverwrite<ClientMetadata>(),
                           std::move(arena2));
    p1.handler.SpawnInfallible("initial_metadata", [&]() {
      auto p1_handler = p1.handler.StartCall();
      return Map(
          p1_handler.PullClientInitialMetadata(),
          [p1_handler, &p2](ValueOrFailure<ClientMetadataHandle> md) mutable {
            CHECK(md.ok());
            ForwardCall(std::move(p1_handler), std::move(p2.initiator));
          });
    });
    std::optional<CallHandler> p2_handler;
    p2.handler.SpawnInfallible("start",
                               [&]() { p2_handler = p2.handler.StartCall(); });
    return {std::move(p1.initiator), std::move(*p2_handler)};
  }

  ServerMetadataHandle MakeServerInitialMetadata() {
    return Arena::MakePooledForOverwrite<ServerMetadata>();
  }

  MessageHandle MakePayload() { return Arena::MakePooled<Message>(); }

  ServerMetadataHandle MakeServerTrailingMetadata() {
    return Arena::MakePooledForOverwrite<ServerMetadata>();
  }

 private:
  std::shared_ptr<grpc_event_engine::experimental::EventEngine> event_engine_ =
      grpc_event_engine::experimental::GetDefaultEventEngine();
  RefCountedPtr<CallArenaAllocator> arena_allocator_ =
      MakeRefCounted<CallArenaAllocator>(
          ResourceQuota::Default()->memory_quota()->CreateMemoryAllocator(
              "test-allocator"),
          1024);
};
GRPC_CALL_SPINE_BENCHMARK(ForwardCallFixture);

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
