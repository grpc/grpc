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

#include "src/core/lib/channel/context.h"
#include "src/core/lib/promise/detail/status.h"
#include "src/core/lib/promise/if.h"
#include "src/core/lib/promise/latch.h"
#include "src/core/lib/promise/party.h"
#include "src/core/lib/promise/pipe.h"
#include "src/core/lib/promise/promise.h"
#include "src/core/lib/promise/status_flag.h"
#include "src/core/lib/transport/call_arena_allocator.h"
#include "src/core/lib/transport/call_filters.h"
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
  virtual ClientMetadata& UnprocessedClientInitialMetadata() = 0;
  virtual void V2HackToStartCallWithoutACallFilterStack() = 0;

  // Wrap a promise so that if it returns failure it automatically cancels
  // the rest of the call.
  // The resulting (returned) promise will resolve to Empty.
  template <typename Promise>
  auto CancelIfFails(Promise promise) {
    DCHECK(GetContext<Activity>() == &party());
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
    DCHECK(GetContext<Activity>() == &party());
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
    DCHECK(GetContext<Activity>() == &party());
    return cancel_latch().Wait();
  }

  Promise<ValueOrFailure<absl::optional<MessageHandle>>>
  PullServerToClientMessage() final {
    DCHECK(GetContext<Activity>() == &party());
    return Map(server_to_client_messages().receiver.Next(), MapNextMessage);
  }

  Promise<StatusFlag> PushClientToServerMessage(MessageHandle message) final {
    DCHECK(GetContext<Activity>() == &party());
    return Map(client_to_server_messages().sender.Push(std::move(message)),
               [](bool r) { return StatusFlag(r); });
  }

  Promise<ValueOrFailure<absl::optional<MessageHandle>>>
  PullClientToServerMessage() final {
    DCHECK(GetContext<Activity>() == &party());
    return Map(client_to_server_messages().receiver.Next(), MapNextMessage);
  }

  Promise<StatusFlag> PushServerToClientMessage(MessageHandle message) final {
    DCHECK(GetContext<Activity>() == &party());
    return Map(server_to_client_messages().sender.Push(std::move(message)),
               [](bool r) { return StatusFlag(r); });
  }

  void FinishSends() final {
    DCHECK(GetContext<Activity>() == &party());
    client_to_server_messages().sender.Close();
  }

  void PushServerTrailingMetadata(ServerMetadataHandle metadata) final {
    DCHECK(GetContext<Activity>() == &party());
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
    DCHECK(GetContext<Activity>() == &party());
    return was_cancelled_latch().Wait();
  }

  Promise<ValueOrFailure<ClientMetadataHandle>> PullClientInitialMetadata()
      final {
    DCHECK(GetContext<Activity>() == &party());
    return Map(client_initial_metadata().receiver.Next(),
               [](NextResult<ClientMetadataHandle> md)
                   -> ValueOrFailure<ClientMetadataHandle> {
                 if (!md.has_value()) return Failure{};
                 return std::move(*md);
               });
  }

  Promise<StatusFlag> PushServerInitialMetadata(
      absl::optional<ServerMetadataHandle> md) final {
    DCHECK(GetContext<Activity>() == &party());
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

class CallSpine final : public CallSpineInterface, public Party {
 public:
  static RefCountedPtr<CallSpine> Create(
      ClientMetadataHandle client_initial_metadata,
      grpc_event_engine::experimental::EventEngine* event_engine, Arena* arena,
      RefCountedPtr<CallArenaAllocator> call_arena_allocator_if_arena_is_owned,
      grpc_call_context_element* legacy_context) {
    return RefCountedPtr<CallSpine>(arena->New<CallSpine>(
        std::move(client_initial_metadata), event_engine, arena,
        std::move(call_arena_allocator_if_arena_is_owned), legacy_context));
  }

  ~CallSpine() override {
    if (legacy_context_is_owned_) {
      for (size_t i = 0; i < GRPC_CONTEXT_COUNT; i++) {
        grpc_call_context_element& elem = legacy_context_[i];
        if (elem.destroy != nullptr) elem.destroy(&elem);
      }
    }
  }

  CallFilters& call_filters() { return call_filters_; }

  Party& party() override { return *this; }

  Arena* arena() override { return arena_; }

  void IncrementRefCount() override { Party::IncrementRefCount(); }

  void Unref() override { Party::Unref(); }

  Promise<ValueOrFailure<absl::optional<ServerMetadataHandle>>>
  PullServerInitialMetadata() override {
    return call_filters().PullServerInitialMetadata();
  }

  Promise<ServerMetadataHandle> PullServerTrailingMetadata() override {
    return call_filters().PullServerTrailingMetadata();
  }

  Promise<StatusFlag> PushClientToServerMessage(
      MessageHandle message) override {
    return call_filters().PushClientToServerMessage(std::move(message));
  }

  Promise<ValueOrFailure<absl::optional<MessageHandle>>>
  PullClientToServerMessage() override {
    return call_filters().PullClientToServerMessage();
  }

  Promise<StatusFlag> PushServerToClientMessage(
      MessageHandle message) override {
    return call_filters().PushServerToClientMessage(std::move(message));
  }

  Promise<ValueOrFailure<absl::optional<MessageHandle>>>
  PullServerToClientMessage() override {
    return call_filters().PullServerToClientMessage();
  }

  void PushServerTrailingMetadata(ServerMetadataHandle md) override {
    call_filters().PushServerTrailingMetadata(std::move(md));
  }

  void FinishSends() override { call_filters().FinishClientToServerSends(); }

  Promise<ValueOrFailure<ClientMetadataHandle>> PullClientInitialMetadata()
      override {
    return call_filters().PullClientInitialMetadata();
  }

  Promise<StatusFlag> PushServerInitialMetadata(
      absl::optional<ServerMetadataHandle> md) override {
    if (md.has_value()) {
      return call_filters().PushServerInitialMetadata(std::move(*md));
    } else {
      call_filters().NoServerInitialMetadata();
      return Immediate<StatusFlag>(Success{});
    }
  }

  Promise<bool> WasCancelled() override {
    return call_filters().WasCancelled();
  }

  ClientMetadata& UnprocessedClientInitialMetadata() override {
    return *call_filters().unprocessed_client_initial_metadata();
  }

  // TODO(ctiller): re-evaluate legacy context apis
  grpc_call_context_element& legacy_context(grpc_context_index index) const {
    return legacy_context_[index];
  }

  grpc_call_context_element* legacy_context() { return legacy_context_; }

  grpc_event_engine::experimental::EventEngine* event_engine() const override {
    return event_engine_;
  }

  void V2HackToStartCallWithoutACallFilterStack() override {
    CallFilters::StackBuilder empty_stack_builder;
    call_filters().SetStack(empty_stack_builder.Build());
  }

 private:
  friend class Arena;
  CallSpine(ClientMetadataHandle client_initial_metadata,
            grpc_event_engine::experimental::EventEngine* event_engine,
            Arena* arena,
            RefCountedPtr<CallArenaAllocator> call_arena_allocator,
            grpc_call_context_element* legacy_context)
      : Party(1),
        call_filters_(std::move(client_initial_metadata)),
        arena_(arena),
        event_engine_(event_engine),
        call_arena_allocator_if_arena_is_owned_(
            std::move(call_arena_allocator)) {
    if (legacy_context == nullptr) {
      legacy_context_ = static_cast<grpc_call_context_element*>(
          arena->Alloc(sizeof(grpc_call_context_element) * GRPC_CONTEXT_COUNT));
      memset(legacy_context_, 0,
             sizeof(grpc_call_context_element) * GRPC_CONTEXT_COUNT);
      legacy_context_is_owned_ = true;
    } else {
      legacy_context_ = legacy_context;
      legacy_context_is_owned_ = false;
    }
  }

  class ScopedContext
      : public ScopedActivity,
        public promise_detail::Context<Arena>,
        public promise_detail::Context<
            grpc_event_engine::experimental::EventEngine>,
        public promise_detail::Context<grpc_call_context_element> {
   public:
    explicit ScopedContext(CallSpine* spine)
        : ScopedActivity(spine),
          Context<Arena>(spine->arena_),
          Context<grpc_event_engine::experimental::EventEngine>(
              spine->event_engine()),
          Context<grpc_call_context_element>(spine->legacy_context_) {}
  };

  bool RunParty() override {
    ScopedContext context(this);
    return Party::RunParty();
  }

  void PartyOver() override {
    Arena* a = arena_;
    RefCountedPtr<CallArenaAllocator> call_arena_allocator_if_arena_is_owned =
        std::move(call_arena_allocator_if_arena_is_owned_);
    {
      ScopedContext context(this);
      CancelRemainingParticipants();
      a->DestroyManagedNewObjects();
    }
    this->~CallSpine();
    if (call_arena_allocator_if_arena_is_owned != nullptr) {
      call_arena_allocator_if_arena_is_owned->Destroy(a);
    }
  }

  // Call filters/pipes part of the spine
  CallFilters call_filters_;
  Arena* const arena_;
  // Event engine associated with this call
  grpc_event_engine::experimental::EventEngine* const event_engine_;
  // Legacy context
  // TODO(ctiller): remove
  grpc_call_context_element* legacy_context_;
  RefCountedPtr<CallArenaAllocator> call_arena_allocator_if_arena_is_owned_;
  bool legacy_context_is_owned_;
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

  grpc_event_engine::experimental::EventEngine* event_engine() {
    return DownCast<CallSpine*>(spine_.get())->event_engine();
  }

  // TODO(ctiller): re-evaluate this API
  grpc_call_context_element* legacy_context() {
    return DownCast<CallSpine*>(spine_.get())->legacy_context();
  }

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

  ClientMetadata& UnprocessedClientInitialMetadata() {
    return spine_->UnprocessedClientInitialMetadata();
  }

  CallHandler V2HackToStartCallWithoutACallFilterStack() {
    spine_->V2HackToStartCallWithoutACallFilterStack();
    return CallHandler(std::move(spine_));
  }

  CallHandler StartCall(RefCountedPtr<CallFilters::Stack> call_filters) {
    DownCast<CallSpine*>(spine_.get())
        ->call_filters()
        .SetStack(std::move(call_filters));
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

CallInitiatorAndHandler MakeCallPair(
    ClientMetadataHandle client_initial_metadata,
    grpc_event_engine::experimental::EventEngine* event_engine, Arena* arena,
    RefCountedPtr<CallArenaAllocator> call_arena_allocator_if_arena_is_owned,
    grpc_call_context_element* legacy_context);

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
