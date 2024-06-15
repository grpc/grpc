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

#ifndef BM_CALL_SPINE_H
#define BM_CALL_SPINE_H

#include "benchmark/benchmark.h"

#include "src/core/lib/promise/all_ok.h"
#include "src/core/lib/promise/map.h"
#include "src/core/lib/transport/call_spine.h"

namespace grpc_core {

struct BenchmarkCall {
  CallInitiator initiator;
  CallHandler handler;
};

template <typename Fixture>
void BM_UnaryWithSpawnPerEnd(benchmark::State& state) {
  Fixture fixture;
  for (auto _ : state) {
    BenchmarkCall call = fixture.MakeCall();
    bool handler_done = false;
    bool initiator_done = false;
    call.handler.SpawnInfallible("handler", [&]() {
      call.handler.PushServerInitialMetadata(
          fixture.MakeServerInitialMetadata());
      return Map(AllOk<StatusFlag>(
                     Map(call.handler.PullClientInitialMetadata(),
                         [](ValueOrFailure<ClientMetadataHandle> md) {
                           return md.status();
                         }),
                     Map(call.handler.PullMessage(),
                         [](ValueOrFailure<absl::optional<MessageHandle>> msg) {
                           return msg.status();
                         }),
                     call.handler.PushMessage(fixture.MakePayload())),
                 [&](StatusFlag status) {
                   CHECK(status.ok());
                   call.handler.PushServerTrailingMetadata(
                       fixture.MakeServerTrailingMetadata());
                   handler_done = true;
                   return Empty{};
                 });
    });
    call.initiator.SpawnInfallible("initiator", [&]() {
      return Map(AllOk<StatusFlag>(
                     Map(call.initiator.PushMessage(fixture.MakePayload()),
                         [](StatusFlag) { return Success{}; }),
                     Map(call.initiator.PullServerInitialMetadata(),
                         [](absl::optional<ServerMetadataHandle> md) {
                           return Success{};
                         }),
                     Map(call.initiator.PullMessage(),
                         [](ValueOrFailure<absl::optional<MessageHandle>> msg) {
                           return msg.status();
                         }),
                     Map(call.initiator.PullServerTrailingMetadata(),
                         [](ServerMetadataHandle) { return Success(); })),
                 [&](StatusFlag result) {
                   CHECK(result.ok());
                   initiator_done = true;
                   return Empty{};
                 });
    });
    CHECK(handler_done);
    CHECK(initiator_done);
  }
}

template <typename Fixture>
void BM_UnaryWithSpawnPerOp(benchmark::State& state) {
  Fixture fixture;
  for (auto _ : state) {
    BenchmarkCall call = fixture.MakeCall();
    bool handler_done = false;
    bool initiator_done = false;
    {
      Party::BulkSpawner handler_spawner(call.handler.party());
      Party::BulkSpawner initiator_spawner(call.initiator.party());
      handler_spawner.Spawn(
          "HANDLER:PushServerInitialMetadata",
          [&]() {
            call.handler.PushServerInitialMetadata(
                fixture.MakeServerInitialMetadata());
            return Empty{};
          },
          [](Empty) {});
      handler_spawner.Spawn(
          "HANDLER:PullClientInitialMetadata",
          [&]() { return call.handler.PullClientInitialMetadata(); },
          [](ValueOrFailure<ClientMetadataHandle> md) { CHECK(md.ok()); });
      handler_spawner.Spawn(
          "HANDLER:PullMessage", [&]() { return call.handler.PullMessage(); },
          [](ValueOrFailure<absl::optional<MessageHandle>> msg) {
            CHECK(msg.ok());
          });
      handler_spawner.Spawn(
          "HANDLER:PushMessage",
          [&]() { return call.handler.PushMessage(fixture.MakePayload()); },
          [](StatusFlag) {});
      handler_spawner.Spawn(
          "HANDLER:PushServerTrailingMetadata",
          [&]() {
            call.handler.PushServerTrailingMetadata(
                fixture.MakeServerTrailingMetadata());
            return Empty{};
          },
          [&](Empty) { handler_done = true; });
      initiator_spawner.Spawn(
          "INITIATOR:PushMessage",
          [&]() { return call.initiator.PushMessage(fixture.MakePayload()); },
          [](StatusFlag) {});
      initiator_spawner.Spawn(
          "INITIATOR:PullServerInitialMetadata",
          [&]() { return call.initiator.PullServerInitialMetadata(); },
          [](absl::optional<ServerMetadataHandle> md) {
            CHECK(md.has_value());
          });
      initiator_spawner.Spawn(
          "INITIATOR:PullMessage",
          [&]() { return call.initiator.PullMessage(); },
          [](ValueOrFailure<absl::optional<MessageHandle>> msg) {
            CHECK(msg.ok());
          });
      initiator_spawner.Spawn(
          "INITIATOR:PullServerTrailingMetadata",
          [&]() { return call.initiator.PullServerTrailingMetadata(); },
          [&](ServerMetadataHandle md) {
            initiator_done = true;
            return Empty{};
          });
    }
    CHECK(handler_done);
    CHECK(initiator_done);
  }
}

}  // namespace grpc_core

#define GRPC_CALL_SPINE_BENCHMARK(Fixture)                \
  BENCHMARK(grpc_core::BM_UnaryWithSpawnPerEnd<Fixture>); \
  BENCHMARK(grpc_core::BM_UnaryWithSpawnPerOp<Fixture>)

#endif
