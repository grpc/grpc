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

#ifndef GRPC_SRC_CORE_LIB_TRANSPORT_CALL_SPINE_H
#define GRPC_SRC_CORE_LIB_TRANSPORT_CALL_SPINE_H

#include "absl/log/check.h"

#include <grpc/support/log.h>
#include <grpc/support/port_platform.h>

#include "src/core/lib/gprpp/dual_ref_counted.h"
#include "src/core/lib/promise/detail/status.h"
#include "src/core/lib/promise/if.h"
#include "src/core/lib/promise/latch.h"
#include "src/core/lib/promise/party.h"
#include "src/core/lib/promise/pipe.h"
#include "src/core/lib/promise/prioritized_race.h"
#include "src/core/lib/promise/promise.h"
#include "src/core/lib/promise/status_flag.h"
#include "src/core/lib/promise/try_seq.h"
#include "src/core/lib/transport/call_arena_allocator.h"
#include "src/core/lib/transport/call_filters.h"
#include "src/core/lib/transport/message.h"
#include "src/core/lib/transport/metadata.h"

namespace grpc_core {

// The common middle part of a call - a reference is held by each of
// CallInitiator and CallHandler - which provide interfaces that are appropriate
// for each side of a call.
// Hosts context, call filters, and the arena.
class CallSpine final : public Party {
 public:
  static RefCountedPtr<CallSpine> Create(
      ClientMetadataHandle client_initial_metadata,
      grpc_event_engine::experimental::EventEngine* event_engine,
      RefCountedPtr<Arena> arena) {
    Arena* arena_ptr = arena.get();
    return RefCountedPtr<CallSpine>(arena_ptr->New<CallSpine>(
        std::move(client_initial_metadata), event_engine, std::move(arena)));
  }

  ~CallSpine() override {}

  CallFilters& call_filters() { return call_filters_; }
  Arena* arena() { return arena_.get(); }

  // Add a callback to be called when server trailing metadata is received.
  void OnDone(absl::AnyInvocable<void()> fn) {
    if (on_done_ == nullptr) {
      on_done_ = std::move(fn);
      return;
    }
    on_done_ = [first = std::move(fn), next = std::move(on_done_)]() mutable {
      first();
      next();
    };
  }
  void CallOnDone() {
    if (on_done_ != nullptr) std::exchange(on_done_, nullptr)();
  }

  auto PullServerInitialMetadata() {
    return call_filters().PullServerInitialMetadata();
  }

  auto PullServerTrailingMetadata() {
    return call_filters().PullServerTrailingMetadata();
  }

  auto PushClientToServerMessage(MessageHandle message) {
    return call_filters().PushClientToServerMessage(std::move(message));
  }

  auto PullClientToServerMessage() {
    return call_filters().PullClientToServerMessage();
  }

  auto PushServerToClientMessage(MessageHandle message) {
    return call_filters().PushServerToClientMessage(std::move(message));
  }

  auto PullServerToClientMessage() {
    return call_filters().PullServerToClientMessage();
  }

  void PushServerTrailingMetadata(ServerMetadataHandle md) {
    call_filters().PushServerTrailingMetadata(std::move(md));
  }

  void FinishSends() { call_filters().FinishClientToServerSends(); }

  auto PullClientInitialMetadata() {
    return call_filters().PullClientInitialMetadata();
  }

  auto PushServerInitialMetadata(absl::optional<ServerMetadataHandle> md) {
    bool has_md = md.has_value();
    return If(
        has_md,
        [this, md = std::move(md)]() mutable {
          return call_filters().PushServerInitialMetadata(std::move(*md));
        },
        [this]() {
          call_filters().NoServerInitialMetadata();
          return Immediate<StatusFlag>(Success{});
        });
  }

  auto WasCancelled() { return call_filters().WasCancelled(); }

  ClientMetadata& UnprocessedClientInitialMetadata() {
    return *call_filters().unprocessed_client_initial_metadata();
  }

  grpc_event_engine::experimental::EventEngine* event_engine() const override {
    return event_engine_;
  }

  // Wrap a promise so that if it returns failure it automatically cancels
  // the rest of the call.
  // The resulting (returned) promise will resolve to Empty.
  template <typename Promise>
  auto CancelIfFails(Promise promise) {
    DCHECK(GetContext<Activity>() == this);
    using P = promise_detail::PromiseLike<Promise>;
    using ResultType = typename P::Result;
    return Map(std::move(promise), [this](ResultType r) {
      if (!IsStatusOk(r)) {
        PushServerTrailingMetadata(StatusCast<ServerMetadataHandle>(r));
      }
      return r;
    });
  }

  // Spawn a promise that returns Empty{} and save some boilerplate handling
  // that detail.
  template <typename PromiseFactory>
  void SpawnInfallible(absl::string_view name, PromiseFactory promise_factory) {
    Spawn(name, std::move(promise_factory), [](Empty) {});
  }

  // Spawn a promise that returns some status-like type; if the status
  // represents failure automatically cancel the rest of the call.
  template <typename PromiseFactory>
  void SpawnGuarded(absl::string_view name, PromiseFactory promise_factory,
                    DebugLocation whence = {}) {
    using FactoryType =
        promise_detail::OncePromiseFactory<void, PromiseFactory>;
    using PromiseType = typename FactoryType::Promise;
    using ResultType = typename PromiseType::Result;
    static_assert(
        std::is_same<bool,
                     decltype(IsStatusOk(std::declval<ResultType>()))>::value,
        "SpawnGuarded promise must return a status-like object");
    Spawn(name, std::move(promise_factory), [this, whence](ResultType r) {
      if (!IsStatusOk(r)) {
        GRPC_TRACE_LOG(promise_primitives, INFO)
            << "SpawnGuarded sees failure: " << r
            << " (source: " << whence.file() << ":" << whence.line() << ")";
        auto status = StatusCast<ServerMetadataHandle>(std::move(r));
        status->Set(GrpcCallWasCancelled(), true);
        PushServerTrailingMetadata(std::move(status));
      }
    });
  }

  // Wrap a promise so that if the call completes that promise is cancelled.
  template <typename Promise>
  auto UntilCallCompletes(Promise promise) {
    using Result = PromiseResult<Promise>;
    return PrioritizedRace(std::move(promise), Map(WasCancelled(), [](bool) {
                             return FailureStatusCast<Result>(Failure{});
                           }));
  }

  template <typename PromiseFactory>
  void SpawnGuardedUntilCallCompletes(absl::string_view name,
                                      PromiseFactory promise_factory) {
    SpawnGuarded(name, [this, promise_factory]() mutable {
      return UntilCallCompletes(promise_factory());
    });
  }

 private:
  friend class Arena;
  CallSpine(ClientMetadataHandle client_initial_metadata,
            grpc_event_engine::experimental::EventEngine* event_engine,
            RefCountedPtr<Arena> arena)
      : Party(1),
        arena_(std::move(arena)),
        call_filters_(std::move(client_initial_metadata)),
        event_engine_(event_engine) {}

  class ScopedContext : public ScopedActivity,
                        public promise_detail::Context<Arena>,
                        public promise_detail::Context<
                            grpc_event_engine::experimental::EventEngine> {
   public:
    explicit ScopedContext(CallSpine* spine)
        : ScopedActivity(spine),
          Context<Arena>(spine->arena_.get()),
          Context<grpc_event_engine::experimental::EventEngine>(
              spine->event_engine()) {}
  };

  bool RunParty() override {
    ScopedContext context(this);
    return Party::RunParty();
  }

  void PartyOver() override {
    auto arena = arena_;
    {
      ScopedContext context(this);
      CancelRemainingParticipants();
      arena->DestroyManagedNewObjects();
    }
    this->~CallSpine();
  }

  const RefCountedPtr<Arena> arena_;
  // Call filters/pipes part of the spine
  CallFilters call_filters_;
  // Event engine associated with this call
  grpc_event_engine::experimental::EventEngine* const event_engine_;
  absl::AnyInvocable<void()> on_done_{nullptr};
};

class CallInitiator {
 public:
  CallInitiator() = default;
  explicit CallInitiator(RefCountedPtr<CallSpine> spine)
      : spine_(std::move(spine)) {}

  template <typename Promise>
  auto CancelIfFails(Promise promise) {
    return spine_->CancelIfFails(std::move(promise));
  }

  auto PullServerInitialMetadata() {
    return spine_->PullServerInitialMetadata();
  }

  auto PushMessage(MessageHandle message) {
    return spine_->PushClientToServerMessage(std::move(message));
  }

  void FinishSends() { spine_->FinishSends(); }

  auto PullMessage() { return spine_->PullServerToClientMessage(); }

  auto PullServerTrailingMetadata() {
    return spine_->PullServerTrailingMetadata();
  }

  void Cancel(absl::Status error = absl::CancelledError()) {
    CHECK(!error.ok());
    auto status = ServerMetadataFromStatus(error);
    status->Set(GrpcCallWasCancelled(), true);
    spine_->PushServerTrailingMetadata(std::move(status));
  }

  void OnDone(absl::AnyInvocable<void()> fn) { spine_->OnDone(std::move(fn)); }

  template <typename PromiseFactory>
  void SpawnGuarded(absl::string_view name, PromiseFactory promise_factory) {
    spine_->SpawnGuarded(name, std::move(promise_factory));
  }

  template <typename PromiseFactory>
  void SpawnGuardedUntilCallCompletes(absl::string_view name,
                                      PromiseFactory promise_factory) {
    spine_->SpawnGuardedUntilCallCompletes(name, std::move(promise_factory));
  }

  template <typename PromiseFactory>
  void SpawnInfallible(absl::string_view name, PromiseFactory promise_factory) {
    spine_->SpawnInfallible(name, std::move(promise_factory));
  }

  template <typename PromiseFactory>
  auto SpawnWaitable(absl::string_view name, PromiseFactory promise_factory) {
    return spine_->SpawnWaitable(name, std::move(promise_factory));
  }

  Arena* arena() { return spine_->arena(); }

  grpc_event_engine::experimental::EventEngine* event_engine() const {
    return spine_->event_engine();
  }

 private:
  RefCountedPtr<CallSpine> spine_;
};

class CallHandler {
 public:
  explicit CallHandler(RefCountedPtr<CallSpine> spine)
      : spine_(std::move(spine)) {}

  auto PullClientInitialMetadata() {
    return spine_->PullClientInitialMetadata();
  }

  auto PushServerInitialMetadata(absl::optional<ServerMetadataHandle> md) {
    return spine_->PushServerInitialMetadata(std::move(md));
  }

  void PushServerTrailingMetadata(ServerMetadataHandle status) {
    spine_->PushServerTrailingMetadata(std::move(status));
  }

  void OnDone(absl::AnyInvocable<void()> fn) { spine_->OnDone(std::move(fn)); }

  template <typename Promise>
  auto CancelIfFails(Promise promise) {
    return spine_->CancelIfFails(std::move(promise));
  }

  auto PushMessage(MessageHandle message) {
    return spine_->PushServerToClientMessage(std::move(message));
  }

  auto PullMessage() { return spine_->PullClientToServerMessage(); }

  auto WasCancelled() { return spine_->WasCancelled(); }

  template <typename PromiseFactory>
  void SpawnGuarded(absl::string_view name, PromiseFactory promise_factory,
                    DebugLocation whence = {}) {
    spine_->SpawnGuarded(name, std::move(promise_factory), whence);
  }

  template <typename PromiseFactory>
  void SpawnGuardedUntilCallCompletes(absl::string_view name,
                                      PromiseFactory promise_factory) {
    spine_->SpawnGuardedUntilCallCompletes(name, std::move(promise_factory));
  }

  template <typename PromiseFactory>
  void SpawnInfallible(absl::string_view name, PromiseFactory promise_factory) {
    spine_->SpawnInfallible(name, std::move(promise_factory));
  }

  template <typename PromiseFactory>
  auto SpawnWaitable(absl::string_view name, PromiseFactory promise_factory) {
    return spine_->SpawnWaitable(name, std::move(promise_factory));
  }

  Arena* arena() { return spine_->arena(); }

  grpc_event_engine::experimental::EventEngine* event_engine() const {
    return spine_->event_engine();
  }

 private:
  RefCountedPtr<CallSpine> spine_;
};

class UnstartedCallHandler {
 public:
  explicit UnstartedCallHandler(RefCountedPtr<CallSpine> spine)
      : spine_(std::move(spine)) {}

  void PushServerTrailingMetadata(ServerMetadataHandle status) {
    spine_->PushServerTrailingMetadata(std::move(status));
  }

  void OnDone(absl::AnyInvocable<void()> fn) { spine_->OnDone(std::move(fn)); }

  template <typename Promise>
  auto CancelIfFails(Promise promise) {
    return spine_->CancelIfFails(std::move(promise));
  }

  template <typename PromiseFactory>
  void SpawnGuarded(absl::string_view name, PromiseFactory promise_factory,
                    DebugLocation whence = {}) {
    spine_->SpawnGuarded(name, std::move(promise_factory), whence);
  }

  template <typename PromiseFactory>
  void SpawnGuardedUntilCallCompletes(absl::string_view name,
                                      PromiseFactory promise_factory) {
    spine_->SpawnGuardedUntilCallCompletes(name, std::move(promise_factory));
  }

  template <typename PromiseFactory>
  void SpawnInfallible(absl::string_view name, PromiseFactory promise_factory) {
    spine_->SpawnInfallible(name, std::move(promise_factory));
  }

  template <typename PromiseFactory>
  auto SpawnWaitable(absl::string_view name, PromiseFactory promise_factory) {
    return spine_->SpawnWaitable(name, std::move(promise_factory));
  }

  ClientMetadata& UnprocessedClientInitialMetadata() {
    return spine_->UnprocessedClientInitialMetadata();
  }

  // Helper for the very common situation in tests where we want to start a call
  // with an empty filter stack.
  CallHandler StartWithEmptyFilterStack() {
    return StartCall(CallFilters::StackBuilder().Build());
  }

  CallHandler StartCall(RefCountedPtr<CallFilters::Stack> call_filters) {
    spine_->call_filters().SetStack(std::move(call_filters));
    return CallHandler(std::move(spine_));
  }

  Arena* arena() { return spine_->arena(); }

 private:
  RefCountedPtr<CallSpine> spine_;
};

struct CallInitiatorAndHandler {
  CallInitiator initiator;
  UnstartedCallHandler handler;
};

CallInitiatorAndHandler MakeCallPair(
    ClientMetadataHandle client_initial_metadata,
    grpc_event_engine::experimental::EventEngine* event_engine,
    RefCountedPtr<Arena> arena);

template <typename CallHalf>
auto OutgoingMessages(CallHalf h) {
  struct Wrapper {
    CallHalf h;
    auto Next() { return h.PullMessage(); }
  };
  return Wrapper{std::move(h)};
}

// Forward a call from `call_handler` to `call_initiator` (with initial metadata
// `client_initial_metadata`)
void ForwardCall(CallHandler call_handler, CallInitiator call_initiator);

}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_LIB_TRANSPORT_CALL_SPINE_H
