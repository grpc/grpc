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

#include <grpc/support/port_platform.h>

#include "absl/log/check.h"
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
#include "src/core/util/dual_ref_counted.h"

namespace grpc_core {

// The common middle part of a call - a reference is held by each of
// CallInitiator and CallHandler - which provide interfaces that are appropriate
// for each side of a call.
// Hosts context, call filters, and the arena.
class CallSpine final : public Party {
 public:
  static RefCountedPtr<CallSpine> Create(
      ClientMetadataHandle client_initial_metadata,
      RefCountedPtr<Arena> arena) {
    Arena* arena_ptr = arena.get();
    return RefCountedPtr<CallSpine>(arena_ptr->New<CallSpine>(
        std::move(client_initial_metadata), std::move(arena)));
  }

  ~CallSpine() override { CallOnDone(true); }

  CallFilters& call_filters() { return call_filters_; }

  // Add a callback to be called when server trailing metadata is received and
  // return true.
  // If CallOnDone has already been invoked, does nothing and returns false.
  GRPC_MUST_USE_RESULT bool OnDone(absl::AnyInvocable<void(bool)> fn) {
    if (call_filters().WasServerTrailingMetadataPulled()) {
      return false;
    }
    if (on_done_ == nullptr) {
      on_done_ = std::move(fn);
      return true;
    }
    on_done_ = [first = std::move(fn),
                next = std::move(on_done_)](bool cancelled) mutable {
      first(cancelled);
      next(cancelled);
    };
    return true;
  }
  void CallOnDone(bool cancelled) {
    if (on_done_ != nullptr) std::exchange(on_done_, nullptr)(cancelled);
  }

  auto PullServerInitialMetadata() {
    return call_filters().PullServerInitialMetadata();
  }

  auto PullServerTrailingMetadata() {
    return Map(
        call_filters().PullServerTrailingMetadata(),
        [this](ServerMetadataHandle result) {
          CallOnDone(result->get(GrpcCallWasCancelled()).value_or(false));
          return result;
        });
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

  StatusFlag PushServerInitialMetadata(ServerMetadataHandle md) {
    return call_filters().PushServerInitialMetadata(std::move(md));
  }

  auto WasCancelled() { return call_filters().WasCancelled(); }

  ClientMetadata& UnprocessedClientInitialMetadata() {
    return *call_filters().unprocessed_client_initial_metadata();
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
        auto md = StatusCast<ServerMetadataHandle>(r);
        md->Set(GrpcCallWasCancelled(), true);
        PushServerTrailingMetadata(std::move(md));
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
            RefCountedPtr<Arena> arena)
      : Party(std::move(arena)),
        call_filters_(std::move(client_initial_metadata)) {}

  // Call filters/pipes part of the spine
  CallFilters call_filters_;
  absl::AnyInvocable<void(bool)> on_done_{nullptr};
};

class CallInitiator {
 public:
  using NextMessage = ServerToClientNextMessage;

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

  GRPC_MUST_USE_RESULT bool OnDone(absl::AnyInvocable<void(bool)> fn) {
    return spine_->OnDone(std::move(fn));
  }

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

  bool WasCancelledPushed() const {
    return spine_->call_filters().WasCancelledPushed();
  }

  Arena* arena() { return spine_->arena(); }
  Party* party() { return spine_.get(); }

 private:
  RefCountedPtr<CallSpine> spine_;
};

class CallHandler {
 public:
  using NextMessage = ClientToServerNextMessage;

  explicit CallHandler(RefCountedPtr<CallSpine> spine)
      : spine_(std::move(spine)) {}

  auto PullClientInitialMetadata() {
    return spine_->PullClientInitialMetadata();
  }

  auto PushServerInitialMetadata(ServerMetadataHandle md) {
    return spine_->PushServerInitialMetadata(std::move(md));
  }

  void PushServerTrailingMetadata(ServerMetadataHandle status) {
    spine_->PushServerTrailingMetadata(std::move(status));
  }

  GRPC_MUST_USE_RESULT bool OnDone(absl::AnyInvocable<void(bool)> fn) {
    return spine_->OnDone(std::move(fn));
  }

  template <typename Promise>
  auto CancelIfFails(Promise promise) {
    return spine_->CancelIfFails(std::move(promise));
  }

  auto PushMessage(MessageHandle message) {
    return spine_->PushServerToClientMessage(std::move(message));
  }

  auto PullMessage() { return spine_->PullClientToServerMessage(); }

  auto WasCancelled() { return spine_->WasCancelled(); }

  bool WasCancelledPushed() const {
    return spine_->call_filters().WasCancelledPushed();
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

  Arena* arena() { return spine_->arena(); }
  Party* party() { return spine_.get(); }

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

  GRPC_MUST_USE_RESULT bool OnDone(absl::AnyInvocable<void(bool)> fn) {
    return spine_->OnDone(std::move(fn));
  }

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

  void AddCallStack(RefCountedPtr<CallFilters::Stack> call_filters) {
    spine_->call_filters().AddStack(std::move(call_filters));
  }

  CallHandler StartCall() {
    spine_->call_filters().Start();
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
    ClientMetadataHandle client_initial_metadata, RefCountedPtr<Arena> arena);

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
// `on_server_trailing_metadata_from_initiator` is a callback that will be
// called with the server trailing metadata received by the initiator, and can
// be used to mutate that metadata if desired.
void ForwardCall(
    CallHandler call_handler, CallInitiator call_initiator,
    absl::AnyInvocable<void(ServerMetadata&)>
        on_server_trailing_metadata_from_initiator = [](ServerMetadata&) {});

}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_LIB_TRANSPORT_CALL_SPINE_H
