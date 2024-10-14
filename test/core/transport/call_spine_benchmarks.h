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

#ifndef GRPC_TEST_CORE_TRANSPORT_CALL_SPINE_BENCHMARKS_H
#define GRPC_TEST_CORE_TRANSPORT_CALL_SPINE_BENCHMARKS_H

#include <memory>

#include "benchmark/benchmark.h"
#include "src/core/lib/event_engine/default_event_engine.h"
#include "src/core/lib/event_engine/event_engine_context.h"
#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/lib/promise/all_ok.h"
#include "src/core/lib/promise/map.h"
#include "src/core/lib/resource_quota/resource_quota.h"
#include "src/core/lib/transport/call_spine.h"
#include "src/core/lib/transport/transport.h"
#include "src/core/util/notification.h"

namespace grpc_core {

struct BenchmarkCall {
  CallInitiator initiator;
  CallHandler handler;
};

// Unary call with one spawn on each end of the spine.
template <typename Fixture>
void BM_UnaryWithSpawnPerEnd(benchmark::State& state) {
  Fixture fixture;
  for (auto _ : state) {
    Notification handler_done;
    Notification initiator_done;
    {
      ExecCtx exec_ctx;
      BenchmarkCall call = fixture.MakeCall();
      call.handler.SpawnInfallible("handler", [handler = call.handler, &fixture,
                                               &handler_done]() mutable {
        handler.PushServerInitialMetadata(fixture.MakeServerInitialMetadata());
        return Map(
            AllOk<StatusFlag>(
                Map(handler.PullClientInitialMetadata(),
                    [](ValueOrFailure<ClientMetadataHandle> md) {
                      return md.status();
                    }),
                Map(handler.PullMessage(),
                    [](ValueOrFailure<absl::optional<MessageHandle>> msg) {
                      return msg.status();
                    }),
                handler.PushMessage(fixture.MakePayload())),
            [&handler_done, &fixture, handler](StatusFlag status) mutable {
              CHECK(status.ok());
              handler.PushServerTrailingMetadata(
                  fixture.MakeServerTrailingMetadata());
              handler_done.Notify();
              return Empty{};
            });
      });
      call.initiator.SpawnInfallible(
          "initiator",
          [initiator = call.initiator, &fixture, &initiator_done]() mutable {
            return Map(
                AllOk<StatusFlag>(
                    Map(initiator.PushMessage(fixture.MakePayload()),
                        [](StatusFlag) { return Success{}; }),
                    Map(initiator.PullServerInitialMetadata(),
                        [](absl::optional<ServerMetadataHandle> md) {
                          return Success{};
                        }),
                    Map(initiator.PullMessage(),
                        [](ValueOrFailure<absl::optional<MessageHandle>> msg) {
                          return msg.status();
                        }),
                    Map(initiator.PullServerTrailingMetadata(),
                        [](ServerMetadataHandle) { return Success(); })),
                [&initiator_done](StatusFlag result) {
                  CHECK(result.ok());
                  initiator_done.Notify();
                  return Empty{};
                });
          });
    }
    handler_done.WaitForNotification();
    initiator_done.WaitForNotification();
  }
}

// Unary call with one promise spawned per operation on the spine.
// It's a little unclear what the optimum should be between the above variant
// and this: whilst a spawn per end minimizes the number of spawns we need to
// do, a spawn per operation can conceivably (but not at the time of writing)
// minimize the number of internal wakeups in the parties.
// For now we track both.
template <typename Fixture>
void BM_UnaryWithSpawnPerOp(benchmark::State& state) {
  Fixture fixture;
  for (auto _ : state) {
    BenchmarkCall call = fixture.MakeCall();
    Notification handler_done;
    Notification initiator_done;
    {
      ExecCtx exec_ctx;
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
          [&](ValueOrFailure<absl::optional<MessageHandle>> msg) {
            CHECK(msg.ok());
            call.handler.SpawnInfallible(
                "HANDLER:PushServerTrailingMetadata", [&]() {
                  call.handler.PushServerTrailingMetadata(
                      fixture.MakeServerTrailingMetadata());
                  handler_done.Notify();
                  return Empty{};
                });
          });
      handler_spawner.Spawn(
          "HANDLER:PushMessage",
          [&]() { return call.handler.PushMessage(fixture.MakePayload()); },
          [](StatusFlag) {});

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
            initiator_done.Notify();
            return Empty{};
          });
    }
    handler_done.WaitForNotification();
    initiator_done.WaitForNotification();
  }
}

template <typename Fixture>
void BM_ClientToServerStreaming(benchmark::State& state) {
  Fixture fixture;
  BenchmarkCall call = fixture.MakeCall();
  Notification handler_metadata_done;
  Notification initiator_metadata_done;
  call.handler.SpawnInfallible("handler-initial-metadata", [&]() {
    return Map(call.handler.PullClientInitialMetadata(),
               [&](ValueOrFailure<ClientMetadataHandle> md) {
                 CHECK(md.ok());
                 call.handler.PushServerInitialMetadata(
                     fixture.MakeServerInitialMetadata());
                 handler_metadata_done.Notify();
                 return Empty{};
               });
  });
  call.initiator.SpawnInfallible("initiator-initial-metadata", [&]() {
    return Map(call.initiator.PullServerInitialMetadata(),
               [&](absl::optional<ServerMetadataHandle> md) {
                 CHECK(md.has_value());
                 initiator_metadata_done.Notify();
                 return Empty{};
               });
  });
  handler_metadata_done.WaitForNotification();
  initiator_metadata_done.WaitForNotification();
  for (auto _ : state) {
    Notification handler_done;
    Notification initiator_done;
    call.handler.SpawnInfallible("handler", [&]() {
      return Map(call.handler.PullMessage(),
                 [&](ValueOrFailure<absl::optional<MessageHandle>> msg) {
                   CHECK(msg.ok());
                   handler_done.Notify();
                   return Empty{};
                 });
    });
    call.initiator.SpawnInfallible("initiator", [&]() {
      return Map(call.initiator.PushMessage(fixture.MakePayload()),
                 [&](StatusFlag result) {
                   CHECK(result.ok());
                   initiator_done.Notify();
                   return Empty{};
                 });
    });
    handler_done.WaitForNotification();
    initiator_done.WaitForNotification();
  }
  call.initiator.SpawnInfallible("done",
                                 [initiator = call.initiator]() mutable {
                                   initiator.Cancel();
                                   return Empty{};
                                 });
  call.handler.SpawnInfallible("done", [handler = call.handler]() mutable {
    handler.PushServerTrailingMetadata(
        CancelledServerMetadataFromStatus(GRPC_STATUS_CANCELLED));
    return Empty{};
  });
}

// Base class for fixtures that wrap a single filter.
// Traits should have MakeClientInitialMetadata, MakeServerInitialMetadata,
// MakePayload, MakeServerTrailingMetadata, MakeChannelArgs and a type named
// Filter.
template <class Traits>
class FilterFixture {
 public:
  BenchmarkCall MakeCall() {
    auto arena = arena_allocator_->MakeArena();
    arena->SetContext<grpc_event_engine::experimental::EventEngine>(
        event_engine_.get());
    auto p =
        MakeCallPair(traits_.MakeClientInitialMetadata(), std::move(arena));
    return {std::move(p.initiator), p.handler.StartCall()};
  }

  ServerMetadataHandle MakeServerInitialMetadata() {
    return traits_.MakeServerInitialMetadata();
  }

  MessageHandle MakePayload() { return traits_.MakePayload(); }

  ServerMetadataHandle MakeServerTrailingMetadata() {
    return traits_.MakeServerTrailingMetadata();
  }

 private:
  Traits traits_;
  std::shared_ptr<grpc_event_engine::experimental::EventEngine> event_engine_ =
      grpc_event_engine::experimental::GetDefaultEventEngine();
  RefCountedPtr<CallArenaAllocator> arena_allocator_ =
      MakeRefCounted<CallArenaAllocator>(
          ResourceQuota::Default()->memory_quota()->CreateMemoryAllocator(
              "test-allocator"),
          1024);
  const RefCountedPtr<CallFilters::Stack> stack_ = [this]() {
    auto filter = Traits::Filter::Create(traits_.MakeChannelArgs(),
                                         typename Traits::Filter::Args{});
    CHECK(filter.ok());
    CallFilters::StackBuilder builder;
    builder.Add(filter->get());
    builder.AddOwnedObject(std::move(*filter));
    return builder.Build();
  }();
};

// Base class for fixtures that wrap an UnstartedCallDestination.
template <class Traits>
class UnstartedCallDestinationFixture {
 public:
  BenchmarkCall MakeCall() {
    auto arena = arena_allocator_->MakeArena();
    arena->SetContext<grpc_event_engine::experimental::EventEngine>(
        event_engine_.get());
    auto p =
        MakeCallPair(traits_->MakeClientInitialMetadata(), std::move(arena));
    p.handler.SpawnInfallible("initiator_setup", [&]() {
      top_destination_->StartCall(std::move(p.handler));
      return Empty{};
    });
    auto handler = bottom_destination_->TakeHandler();
    absl::optional<CallHandler> started_handler;
    Notification started;
    handler.SpawnInfallible("handler_setup", [&]() {
      started_handler = handler.StartCall();
      started.Notify();
      return Empty{};
    });
    started.WaitForNotification();
    CHECK(started_handler.has_value());
    return {std::move(p.initiator), std::move(*started_handler)};
  }

  ~UnstartedCallDestinationFixture() {
    // TODO(ctiller): entire destructor can be deleted once ExecCtx is gone.
    ExecCtx exec_ctx;
    top_destination_.reset();
    bottom_destination_.reset();
    arena_allocator_.reset();
    event_engine_.reset();
    traits_.reset();
  }

  ServerMetadataHandle MakeServerInitialMetadata() {
    return traits_->MakeServerInitialMetadata();
  }

  MessageHandle MakePayload() { return traits_->MakePayload(); }

  ServerMetadataHandle MakeServerTrailingMetadata() {
    return traits_->MakeServerTrailingMetadata();
  }

 private:
  class SinkDestination : public UnstartedCallDestination {
   public:
    void StartCall(UnstartedCallHandler handler) override {
      MutexLock lock(&mu_);
      handler_ = std::move(handler);
    }
    void Orphaned() override {}

    UnstartedCallHandler TakeHandler() {
      mu_.LockWhen(absl::Condition(
          +[](SinkDestination* dest) ABSL_EXCLUSIVE_LOCKS_REQUIRED(dest->mu_) {
            return dest->handler_.has_value();
          },
          this));
      auto h = std::move(*handler_);
      handler_.reset();
      mu_.Unlock();
      return h;
    }

   private:
    absl::Mutex mu_;
    absl::optional<UnstartedCallHandler> handler_ ABSL_GUARDED_BY(mu_);
  };

  // TODO(ctiller): no need for unique_ptr once ExecCtx is gone
  std::unique_ptr<Traits> traits_ = std::make_unique<Traits>();
  std::shared_ptr<grpc_event_engine::experimental::EventEngine> event_engine_ =
      grpc_event_engine::experimental::GetDefaultEventEngine();
  RefCountedPtr<CallArenaAllocator> arena_allocator_ =
      MakeRefCounted<CallArenaAllocator>(
          ResourceQuota::Default()->memory_quota()->CreateMemoryAllocator(
              "test-allocator"),
          1024);
  RefCountedPtr<SinkDestination> bottom_destination_ =
      MakeRefCounted<SinkDestination>();
  RefCountedPtr<UnstartedCallDestination> top_destination_ =
      traits_->CreateCallDestination(bottom_destination_);
};

// Base class for transports
// Traits should have MakeClientInitialMetadata, MakeServerInitialMetadata,
// MakePayload, MakeServerTrailingMetadata.
// They should also have a MakeTransport returning a BenchmarkTransport.

struct BenchmarkTransport {
  OrphanablePtr<ClientTransport> client;
  OrphanablePtr<ServerTransport> server;
};

template <class Traits>
class TransportFixture {
 public:
  TransportFixture() { transport_.server->SetCallDestination(acceptor_); };

  BenchmarkCall MakeCall() {
    auto arena = arena_allocator_->MakeArena();
    arena->SetContext<grpc_event_engine::experimental::EventEngine>(
        event_engine_.get());
    auto p =
        MakeCallPair(traits_.MakeClientInitialMetadata(), std::move(arena));
    transport_.client->StartCall(p.handler.StartCall());
    auto handler = acceptor_->TakeHandler();
    absl::optional<CallHandler> started_handler;
    Notification started;
    handler.SpawnInfallible("handler_setup", [&]() {
      started_handler = handler.StartCall();
      started.Notify();
      return Empty{};
    });
    started.WaitForNotification();
    CHECK(started_handler.has_value());
    return {std::move(p.initiator), std::move(*started_handler)};
  }

  ServerMetadataHandle MakeServerInitialMetadata() {
    return traits_.MakeServerInitialMetadata();
  }

  MessageHandle MakePayload() { return traits_.MakePayload(); }

  ServerMetadataHandle MakeServerTrailingMetadata() {
    return traits_.MakeServerTrailingMetadata();
  }

 private:
  class Acceptor : public UnstartedCallDestination {
   public:
    void StartCall(UnstartedCallHandler handler) override {
      MutexLock lock(&mu_);
      handler_ = std::move(handler);
    }
    void Orphaned() override {}

    UnstartedCallHandler TakeHandler() {
      mu_.LockWhen(absl::Condition(
          +[](Acceptor* dest) ABSL_EXCLUSIVE_LOCKS_REQUIRED(dest->mu_) {
            return dest->handler_.has_value();
          },
          this));
      auto h = std::move(*handler_);
      handler_.reset();
      mu_.Unlock();
      return h;
    }

    absl::Mutex mu_;
    absl::optional<UnstartedCallHandler> handler_ ABSL_GUARDED_BY(mu_);
  };

  Traits traits_;
  std::shared_ptr<grpc_event_engine::experimental::EventEngine> event_engine_ =
      grpc_event_engine::experimental::GetDefaultEventEngine();
  RefCountedPtr<CallArenaAllocator> arena_allocator_ =
      MakeRefCounted<CallArenaAllocator>(
          ResourceQuota::Default()->memory_quota()->CreateMemoryAllocator(
              "test-allocator"),
          1024);
  RefCountedPtr<Acceptor> acceptor_ = MakeRefCounted<Acceptor>();
  BenchmarkTransport transport_ = traits_.MakeTransport();
};

}  // namespace grpc_core

// Declare all relevant benchmarks for a given fixture
// Must be called within the grpc_core namespace
#define GRPC_CALL_SPINE_BENCHMARK(Fixture)     \
  BENCHMARK(BM_UnaryWithSpawnPerEnd<Fixture>); \
  BENCHMARK(BM_UnaryWithSpawnPerOp<Fixture>);  \
  BENCHMARK(BM_ClientToServerStreaming<Fixture>)

#endif  // GRPC_TEST_CORE_TRANSPORT_CALL_SPINE_BENCHMARKS_H
