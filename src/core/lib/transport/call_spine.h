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

#include <grpc/support/log.h>

#include "src/core/lib/promise/detail/status.h"
#include "src/core/lib/promise/for_each.h"
#include "src/core/lib/promise/if.h"
#include "src/core/lib/promise/latch.h"
#include "src/core/lib/promise/party.h"
#include "src/core/lib/promise/pipe.h"
#include "src/core/lib/promise/prioritized_race.h"
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
  virtual Pipe<ClientMetadataHandle>& client_initial_metadata() = 0;
  virtual Pipe<ServerMetadataHandle>& server_initial_metadata() = 0;
  virtual Pipe<MessageHandle>& client_to_server_messages() = 0;
  virtual Pipe<MessageHandle>& server_to_client_messages() = 0;
  virtual Pipe<ServerMetadataHandle>& server_trailing_metadata() = 0;
  virtual Latch<ServerMetadataHandle>& cancel_latch() = 0;
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
  virtual void IncrementRefCount() = 0;
  virtual void Unref() = 0;

  // Cancel the call with the given metadata.
  // Regarding the `MUST_USE_RESULT absl::nullopt_t`:
  // Most cancellation calls right now happen in pipe interceptors;
  // there `nullopt` indicates terminate processing of this pipe and close with
  // error.
  // It's convenient then to have the Cancel operation (setting the latch to
  // terminate the call) be the last thing that occurs in a pipe interceptor,
  // and this construction supports that (and has helped the author not write
  // some bugs).
  GRPC_MUST_USE_RESULT absl::nullopt_t Cancel(ServerMetadataHandle metadata) {
    GPR_DEBUG_ASSERT(GetContext<Activity>() == &party());
    auto& c = cancel_latch();
    if (c.is_set()) return absl::nullopt;
    c.Set(std::move(metadata));
    CallOnDone();
    client_initial_metadata().sender.CloseWithError();
    server_initial_metadata().sender.CloseWithError();
    client_to_server_messages().sender.CloseWithError();
    server_to_client_messages().sender.CloseWithError();
    server_trailing_metadata().sender.CloseWithError();
    return absl::nullopt;
  }

  auto WaitForCancel() {
    GPR_DEBUG_ASSERT(GetContext<Activity>() == &party());
    return cancel_latch().Wait();
  }

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
        std::ignore = Cancel(StatusCast<ServerMetadataHandle>(r));
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
  void SpawnGuarded(absl::string_view name, PromiseFactory promise_factory) {
    using FactoryType =
        promise_detail::OncePromiseFactory<void, PromiseFactory>;
    using PromiseType = typename FactoryType::Promise;
    using ResultType = typename PromiseType::Result;
    static_assert(
        std::is_same<bool,
                     decltype(IsStatusOk(std::declval<ResultType>()))>::value,
        "SpawnGuarded promise must return a status-like object");
    party().Spawn(name, std::move(promise_factory), [this](ResultType r) {
      if (!IsStatusOk(r)) {
        if (grpc_trace_promise_primitives.enabled()) {
          gpr_log(GPR_DEBUG, "SpawnGuarded sees failure: %s",
                  r.ToString().c_str());
        }
        std::ignore = Cancel(StatusCast<ServerMetadataHandle>(std::move(r)));
      }
    });
  }

 private:
  absl::AnyInvocable<void()> on_done_{nullptr};
};

class CallSpine final : public CallSpineInterface, public Party {
 public:
  static RefCountedPtr<CallSpine> Create(
      grpc_event_engine::experimental::EventEngine* event_engine,
      Arena* arena) {
    return RefCountedPtr<CallSpine>(arena->New<CallSpine>(event_engine, arena));
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
  Pipe<ServerMetadataHandle>& server_trailing_metadata() override {
    return server_trailing_metadata_;
  }
  Latch<ServerMetadataHandle>& cancel_latch() override { return cancel_latch_; }
  Party& party() override { return *this; }
  void IncrementRefCount() override { Party::IncrementRefCount(); }
  void Unref() override { Party::Unref(); }

 private:
  friend class Arena;
  CallSpine(grpc_event_engine::experimental::EventEngine* event_engine,
            Arena* arena)
      : Party(arena, 1), event_engine_(event_engine) {}

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

  // Initial metadata from client to server
  Pipe<ClientMetadataHandle> client_initial_metadata_{arena()};
  // Initial metadata from server to client
  Pipe<ServerMetadataHandle> server_initial_metadata_{arena()};
  // Messages travelling from the application to the transport.
  Pipe<MessageHandle> client_to_server_messages_{arena()};
  // Messages travelling from the transport to the application.
  Pipe<MessageHandle> server_to_client_messages_{arena()};
  // Trailing metadata from server to client
  Pipe<ServerMetadataHandle> server_trailing_metadata_{arena()};
  // Latch that can be set to terminate the call
  Latch<ServerMetadataHandle> cancel_latch_;
  // Event engine associated with this call
  grpc_event_engine::experimental::EventEngine* const event_engine_;
};

class CallInitiator {
 public:
  explicit CallInitiator(RefCountedPtr<CallSpineInterface> spine)
      : spine_(std::move(spine)) {}

  auto PushClientInitialMetadata(ClientMetadataHandle md) {
    GPR_DEBUG_ASSERT(GetContext<Activity>() == &spine_->party());
    return Map(spine_->client_initial_metadata().sender.Push(std::move(md)),
               [](bool ok) { return StatusFlag(ok); });
  }

  auto PullServerInitialMetadata() {
    GPR_DEBUG_ASSERT(GetContext<Activity>() == &spine_->party());
    return Map(spine_->server_initial_metadata().receiver.Next(),
               [](NextResult<ServerMetadataHandle> md)
                   -> ValueOrFailure<absl::optional<ServerMetadataHandle>> {
                 if (!md.has_value()) {
                   if (md.cancelled()) return Failure{};
                   return absl::optional<ServerMetadataHandle>();
                 }
                 return absl::optional<ServerMetadataHandle>(std::move(*md));
               });
  }

  auto PullServerTrailingMetadata() {
    GPR_DEBUG_ASSERT(GetContext<Activity>() == &spine_->party());
    return PrioritizedRace(
        Seq(spine_->server_trailing_metadata().receiver.Next(),
            [spine = spine_](NextResult<ServerMetadataHandle> md) mutable {
              return [md = std::move(md),
                      spine]() mutable -> Poll<ServerMetadataHandle> {
                // If the pipe was closed at cancellation time, we'll see no
                // value here. Return pending and allow the cancellation to win
                // the race.
                if (!md.has_value()) return Pending{};
                spine->server_trailing_metadata().sender.Close();
                return std::move(*md);
              };
            }),
        Map(spine_->WaitForCancel(),
            [spine = spine_](ServerMetadataHandle md) -> ServerMetadataHandle {
              spine->server_trailing_metadata().sender.CloseWithError();
              return md;
            }));
  }

  auto PullMessage() {
    GPR_DEBUG_ASSERT(GetContext<Activity>() == &spine_->party());
    return spine_->server_to_client_messages().receiver.Next();
  }

  auto PushMessage(MessageHandle message) {
    GPR_DEBUG_ASSERT(GetContext<Activity>() == &spine_->party());
    return Map(
        spine_->client_to_server_messages().sender.Push(std::move(message)),
        [](bool r) { return StatusFlag(r); });
  }

  void FinishSends() {
    GPR_DEBUG_ASSERT(GetContext<Activity>() == &spine_->party());
    spine_->client_to_server_messages().sender.Close();
  }

  template <typename Promise>
  auto CancelIfFails(Promise promise) {
    return spine_->CancelIfFails(std::move(promise));
  }

  void Cancel() {
    GPR_DEBUG_ASSERT(GetContext<Activity>() == &spine_->party());
    std::ignore =
        spine_->Cancel(ServerMetadataFromStatus(absl::CancelledError()));
  }

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

  Arena* arena() { return spine_->party().arena(); }

 private:
  RefCountedPtr<CallSpineInterface> spine_;
};

class CallHandler {
 public:
  explicit CallHandler(RefCountedPtr<CallSpineInterface> spine)
      : spine_(std::move(spine)) {}

  auto PullClientInitialMetadata() {
    GPR_DEBUG_ASSERT(GetContext<Activity>() == &spine_->party());
    return Map(spine_->client_initial_metadata().receiver.Next(),
               [](NextResult<ClientMetadataHandle> md)
                   -> ValueOrFailure<ClientMetadataHandle> {
                 if (!md.has_value()) return Failure{};
                 return std::move(*md);
               });
  }

  auto PushServerInitialMetadata(absl::optional<ServerMetadataHandle> md) {
    GPR_DEBUG_ASSERT(GetContext<Activity>() == &spine_->party());
    return If(
        md.has_value(),
        [&md, this]() {
          return Map(
              spine_->server_initial_metadata().sender.Push(std::move(*md)),
              [](bool ok) { return StatusFlag(ok); });
        },
        [this]() {
          spine_->server_initial_metadata().sender.Close();
          return []() -> StatusFlag { return Success{}; };
        });
  }

  auto PushServerTrailingMetadata(ServerMetadataHandle md) {
    GPR_DEBUG_ASSERT(GetContext<Activity>() == &spine_->party());
    spine_->server_initial_metadata().sender.Close();
    spine_->server_to_client_messages().sender.Close();
    spine_->client_to_server_messages().receiver.CloseWithError();
    spine_->CallOnDone();
    return Map(spine_->server_trailing_metadata().sender.Push(std::move(md)),
               [](bool ok) { return StatusFlag(ok); });
  }

  auto PullMessage() {
    GPR_DEBUG_ASSERT(GetContext<Activity>() == &spine_->party());
    return spine_->client_to_server_messages().receiver.Next();
  }

  auto PushMessage(MessageHandle message) {
    GPR_DEBUG_ASSERT(GetContext<Activity>() == &spine_->party());
    return Map(
        spine_->server_to_client_messages().sender.Push(std::move(message)),
        [](bool ok) { return StatusFlag(ok); });
  }

  void Cancel(ServerMetadataHandle status) {
    GPR_DEBUG_ASSERT(GetContext<Activity>() == &spine_->party());
    std::ignore = spine_->Cancel(std::move(status));
  }

  void OnDone(absl::AnyInvocable<void()> fn) { spine_->OnDone(std::move(fn)); }

  template <typename Promise>
  auto CancelIfFails(Promise promise) {
    return spine_->CancelIfFails(std::move(promise));
  }

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

  Arena* arena() { return spine_->party().arena(); }

 private:
  RefCountedPtr<CallSpineInterface> spine_;
};

struct CallInitiatorAndHandler {
  CallInitiator initiator;
  CallHandler handler;
};

CallInitiatorAndHandler MakeCall(
    grpc_event_engine::experimental::EventEngine* event_engine, Arena* arena);

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
void ForwardCall(CallHandler call_handler, CallInitiator call_initiator,
                 ClientMetadataHandle client_initial_metadata);

}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_LIB_TRANSPORT_CALL_SPINE_H
