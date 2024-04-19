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

#include <grpc/support/log.h>
#include <grpc/support/port_platform.h>

#include "src/core/lib/promise/detail/status.h"
#include "src/core/lib/promise/for_each.h"
#include "src/core/lib/promise/if.h"
#include "src/core/lib/promise/latch.h"
#include "src/core/lib/promise/party.h"
#include "src/core/lib/promise/pipe.h"
#include "src/core/lib/promise/prioritized_race.h"
#include "src/core/lib/promise/promise.h"
#include "src/core/lib/promise/status_flag.h"
#include "src/core/lib/promise/try_seq.h"
#include "src/core/lib/transport/message.h"
#include "src/core/lib/transport/metadata.h"

namespace grpc_core {

// The common middle part of a call - a reference is held by each of
// CallInitiator and CallHandler - which provide interfaces that are appropriate
// for each side of a call.
// The spine will ultimately host the pipes, filters, and context for one part
// of a call: ie top-half client channel, sub channel call, server call.
// TODO(ctiller): eventually drop this when we don't need to reference into
// legacy promise calls anymore
class CallSpineInterface {
 public:
  virtual ~CallSpineInterface() = default;
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
  virtual Party& party() = 0;
  virtual Arena* arena() = 0;
  virtual void IncrementRefCount() = 0;
  virtual void Unref() = 0;

  virtual Promise<ValueOrFailure<absl::optional<ServerMetadataHandle>>>
  PullServerInitialMetadata() = 0;
  virtual Promise<ServerMetadataHandle> PullServerTrailingMetadata() = 0;
  virtual Promise<StatusFlag> PushClientToServerMessage(
      MessageHandle message) = 0;
  virtual Promise<ValueOrFailure<absl::optional<MessageHandle>>>
  PullClientToServerMessage() = 0;
  virtual Promise<StatusFlag> PushServerToClientMessage(
      MessageHandle message) = 0;
  virtual Promise<ValueOrFailure<absl::optional<MessageHandle>>>
  PullServerToClientMessage() = 0;
  virtual void PushServerTrailingMetadata(ServerMetadataHandle md) = 0;
  virtual void FinishSends() = 0;
  virtual Promise<ValueOrFailure<ClientMetadataHandle>>
  PullClientInitialMetadata() = 0;
  virtual Promise<StatusFlag> PushServerInitialMetadata(
      absl::optional<ServerMetadataHandle> md) = 0;
  virtual Promise<bool> WasCancelled() = 0;

  // Wrap a promise so that if it returns failure it automatically cancels
  // the rest of the call.
  // The resulting (returned) promise will resolve to Empty.
  template <typename Promise>
  auto CancelIfFails(Promise promise) {
    GPR_DEBUG_ASSERT(GetContext<Activity>() == &party());
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
    party().Spawn(name, std::move(promise_factory), [](Empty) {});
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
    party().Spawn(
        name, std::move(promise_factory), [this, whence](ResultType r) {
          if (!IsStatusOk(r)) {
            if (grpc_trace_promise_primitives.enabled()) {
              gpr_log(GPR_INFO, "SpawnGuarded sees failure: %s (source: %s:%d)",
                      r.ToString().c_str(), whence.file(), whence.line());
            }
            auto status = StatusCast<ServerMetadataHandle>(std::move(r));
            status->Set(GrpcCallWasCancelled(), true);
            PushServerTrailingMetadata(std::move(status));
          }
        });
  }

 private:
  absl::AnyInvocable<void()> on_done_{nullptr};
};

// Implementation of CallSpine atop the v2 Pipe based arrangement.
// This implementation will go away in favor of an implementation atop
// CallFilters by the time v3 lands.
class PipeBasedCallSpine : public CallSpineInterface {
 public:
  virtual Pipe<ClientMetadataHandle>& client_initial_metadata() = 0;
  virtual Pipe<ServerMetadataHandle>& server_initial_metadata() = 0;
  virtual Pipe<MessageHandle>& client_to_server_messages() = 0;
  virtual Pipe<MessageHandle>& server_to_client_messages() = 0;
  virtual Latch<ServerMetadataHandle>& cancel_latch() = 0;
  virtual Latch<bool>& was_cancelled_latch() = 0;

  Promise<ValueOrFailure<absl::optional<ServerMetadataHandle>>>
  PullServerInitialMetadata() final {
    GPR_DEBUG_ASSERT(GetContext<Activity>() == &party());
    return Map(server_initial_metadata().receiver.Next(),
               [](NextResult<ServerMetadataHandle> md)
                   -> ValueOrFailure<absl::optional<ServerMetadataHandle>> {
                 if (!md.has_value()) {
                   if (md.cancelled()) return Failure{};
                   return absl::optional<ServerMetadataHandle>();
                 }
                 return absl::optional<ServerMetadataHandle>(std::move(*md));
               });
  }

  Promise<ServerMetadataHandle> PullServerTrailingMetadata() final {
    GPR_DEBUG_ASSERT(GetContext<Activity>() == &party());
    return cancel_latch().Wait();
  }

  Promise<ValueOrFailure<absl::optional<MessageHandle>>>
  PullServerToClientMessage() final {
    GPR_DEBUG_ASSERT(GetContext<Activity>() == &party());
    return Map(server_to_client_messages().receiver.Next(), MapNextMessage);
  }

  Promise<StatusFlag> PushClientToServerMessage(MessageHandle message) final {
    GPR_DEBUG_ASSERT(GetContext<Activity>() == &party());
    return Map(client_to_server_messages().sender.Push(std::move(message)),
               [](bool r) { return StatusFlag(r); });
  }

  Promise<ValueOrFailure<absl::optional<MessageHandle>>>
  PullClientToServerMessage() final {
    GPR_DEBUG_ASSERT(GetContext<Activity>() == &party());
    return Map(client_to_server_messages().receiver.Next(), MapNextMessage);
  }

  Promise<StatusFlag> PushServerToClientMessage(MessageHandle message) final {
    GPR_DEBUG_ASSERT(GetContext<Activity>() == &party());
    return Map(server_to_client_messages().sender.Push(std::move(message)),
               [](bool r) { return StatusFlag(r); });
  }

  void FinishSends() final {
    GPR_DEBUG_ASSERT(GetContext<Activity>() == &party());
    client_to_server_messages().sender.Close();
  }

  void PushServerTrailingMetadata(ServerMetadataHandle metadata) final {
    GPR_DEBUG_ASSERT(GetContext<Activity>() == &party());
    auto& c = cancel_latch();
    if (c.is_set()) return;
    const bool was_cancelled =
        metadata->get(GrpcCallWasCancelled()).value_or(false);
    c.Set(std::move(metadata));
    CallOnDone();
    was_cancelled_latch().Set(was_cancelled);
    client_initial_metadata().sender.CloseWithError();
    server_initial_metadata().sender.Close();
    client_to_server_messages().sender.CloseWithError();
    server_to_client_messages().sender.Close();
  }

  Promise<bool> WasCancelled() final {
    GPR_DEBUG_ASSERT(GetContext<Activity>() == &party());
    return was_cancelled_latch().Wait();
  }

  Promise<ValueOrFailure<ClientMetadataHandle>> PullClientInitialMetadata()
      final {
    GPR_DEBUG_ASSERT(GetContext<Activity>() == &party());
    return Map(client_initial_metadata().receiver.Next(),
               [](NextResult<ClientMetadataHandle> md)
                   -> ValueOrFailure<ClientMetadataHandle> {
                 if (!md.has_value()) return Failure{};
                 return std::move(*md);
               });
  }

  Promise<StatusFlag> PushServerInitialMetadata(
      absl::optional<ServerMetadataHandle> md) final {
    GPR_DEBUG_ASSERT(GetContext<Activity>() == &party());
    return If(
        md.has_value(),
        [&md, this]() {
          return Map(server_initial_metadata().sender.Push(std::move(*md)),
                     [](bool ok) { return StatusFlag(ok); });
        },
        [this]() {
          server_initial_metadata().sender.Close();
          return []() -> StatusFlag { return Success{}; };
        });
  }

 private:
  static ValueOrFailure<absl::optional<MessageHandle>> MapNextMessage(
      NextResult<MessageHandle> r) {
    if (!r.has_value()) {
      if (r.cancelled()) return Failure{};
      return absl::optional<MessageHandle>();
    }
    return absl::optional<MessageHandle>(std::move(*r));
  }
};

class CallSpine final : public PipeBasedCallSpine, public Party {
 public:
  static RefCountedPtr<CallSpine> Create(
      ClientMetadataHandle client_initial_metadata,
      grpc_event_engine::experimental::EventEngine* event_engine, Arena* arena,
      bool is_arena_owned) {
    auto spine = RefCountedPtr<CallSpine>(
        arena->New<CallSpine>(event_engine, arena, is_arena_owned));
    spine->SpawnInfallible(
        "push_client_initial_metadata",
        [spine = spine.get(), client_initial_metadata = std::move(
                                  client_initial_metadata)]() mutable {
          return Map(spine->client_initial_metadata_.sender.Push(
                         std::move(client_initial_metadata)),
                     [](bool) { return Empty{}; });
        });
    return spine;
  }

  Pipe<ClientMetadataHandle>& client_initial_metadata() override {
    return client_initial_metadata_;
  }
  Pipe<ServerMetadataHandle>& server_initial_metadata() override {
    return server_initial_metadata_;
  }
  Pipe<MessageHandle>& client_to_server_messages() override {
    return client_to_server_messages_;
  }
  Pipe<MessageHandle>& server_to_client_messages() override {
    return server_to_client_messages_;
  }
  Latch<ServerMetadataHandle>& cancel_latch() override { return cancel_latch_; }
  Latch<bool>& was_cancelled_latch() override { return was_cancelled_latch_; }
  Party& party() override { return *this; }
  Arena* arena() override { return arena_; }
  void IncrementRefCount() override { Party::IncrementRefCount(); }
  void Unref() override { Party::Unref(); }

 private:
  friend class Arena;
  CallSpine(grpc_event_engine::experimental::EventEngine* event_engine,
            Arena* arena, bool is_arena_owned)
      : Party(1),
        arena_(arena),
        is_arena_owned_(is_arena_owned),
        event_engine_(event_engine) {}

  class ScopedContext : public ScopedActivity,
                        public promise_detail::Context<Arena> {
   public:
    explicit ScopedContext(CallSpine* spine)
        : ScopedActivity(&spine->party()), Context<Arena>(spine->arena()) {}
  };

  bool RunParty() override {
    ScopedContext context(this);
    return Party::RunParty();
  }

  void PartyOver() override {
    Arena* a = arena();
    {
      ScopedContext context(this);
      CancelRemainingParticipants();
      a->DestroyManagedNewObjects();
    }
    this->~CallSpine();
    a->Destroy();
  }

  grpc_event_engine::experimental::EventEngine* event_engine() const override {
    return event_engine_;
  }

  Arena* arena_;
  bool is_arena_owned_;
  // Initial metadata from client to server
  Pipe<ClientMetadataHandle> client_initial_metadata_{arena()};
  // Initial metadata from server to client
  Pipe<ServerMetadataHandle> server_initial_metadata_{arena()};
  // Messages travelling from the application to the transport.
  Pipe<MessageHandle> client_to_server_messages_{arena()};
  // Messages travelling from the transport to the application.
  Pipe<MessageHandle> server_to_client_messages_{arena()};
  // Latch that can be set to terminate the call
  Latch<ServerMetadataHandle> cancel_latch_;
  Latch<bool> was_cancelled_latch_;
  // Event engine associated with this call
  grpc_event_engine::experimental::EventEngine* const event_engine_;
};

class CallInitiator {
 public:
  explicit CallInitiator(RefCountedPtr<CallSpineInterface> spine)
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

  void Cancel() {
    auto status = ServerMetadataFromStatus(absl::CancelledError());
    status->Set(GrpcCallWasCancelled(), true);
    spine_->PushServerTrailingMetadata(std::move(status));
  }

  void OnDone(absl::AnyInvocable<void()> fn) { spine_->OnDone(std::move(fn)); }

  template <typename PromiseFactory>
  void SpawnGuarded(absl::string_view name, PromiseFactory promise_factory) {
    spine_->SpawnGuarded(name, std::move(promise_factory));
  }

  template <typename PromiseFactory>
  void SpawnInfallible(absl::string_view name, PromiseFactory promise_factory) {
    spine_->SpawnInfallible(name, std::move(promise_factory));
  }

  template <typename PromiseFactory>
  auto SpawnWaitable(absl::string_view name, PromiseFactory promise_factory) {
    return spine_->party().SpawnWaitable(name, std::move(promise_factory));
  }

  Arena* arena() { return spine_->arena(); }

 private:
  RefCountedPtr<CallSpineInterface> spine_;
};

class CallHandler {
 public:
  explicit CallHandler(RefCountedPtr<CallSpineInterface> spine)
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

  template <typename PromiseFactory>
  void SpawnGuarded(absl::string_view name, PromiseFactory promise_factory,
                    DebugLocation whence = {}) {
    spine_->SpawnGuarded(name, std::move(promise_factory), whence);
  }

  template <typename PromiseFactory>
  void SpawnInfallible(absl::string_view name, PromiseFactory promise_factory) {
    spine_->SpawnInfallible(name, std::move(promise_factory));
  }

  template <typename PromiseFactory>
  auto SpawnWaitable(absl::string_view name, PromiseFactory promise_factory) {
    return spine_->party().SpawnWaitable(name, std::move(promise_factory));
  }

  Arena* arena() { return spine_->arena(); }

 private:
  RefCountedPtr<CallSpineInterface> spine_;
};

class UnstartedCallHandler {
 public:
  explicit UnstartedCallHandler(RefCountedPtr<CallSpineInterface> spine)
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
  void SpawnInfallible(absl::string_view name, PromiseFactory promise_factory) {
    spine_->SpawnInfallible(name, std::move(promise_factory));
  }

  template <typename PromiseFactory>
  auto SpawnWaitable(absl::string_view name, PromiseFactory promise_factory) {
    return spine_->party().SpawnWaitable(name, std::move(promise_factory));
  }

  CallHandler V2HackToStartCallWithoutACallFilterStack() {
    GPR_ASSERT(DownCast<PipeBasedCallSpine*>(spine_.get()) != nullptr);
    return CallHandler(std::move(spine_));
  }

  Arena* arena() { return spine_->arena(); }

 private:
  RefCountedPtr<CallSpineInterface> spine_;
};

struct CallInitiatorAndHandler {
  CallInitiator initiator;
  UnstartedCallHandler handler;
};

CallInitiatorAndHandler MakeCall(
    ClientMetadataHandle client_initial_metadata,
    grpc_event_engine::experimental::EventEngine* event_engine, Arena* arena,
    bool is_arena_owned);

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
