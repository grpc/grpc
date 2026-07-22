// Copyright 2023 gRPC authors.
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

#include "test/core/filters/filter_test.h"

#include <grpc/grpc.h>

#include <algorithm>
#include <chrono>
#include <memory>
#include <optional>
#include <queue>

#include "src/core/call/call_finalization.h"
#include "src/core/lib/event_engine/default_event_engine.h"
#include "src/core/lib/iomgr/timer_manager.h"
#include "src/core/lib/promise/activity.h"
#include "src/core/lib/promise/arena_promise.h"
#include "src/core/lib/promise/context.h"
#include "src/core/lib/promise/pipe.h"
#include "src/core/lib/promise/poll.h"
#include "src/core/lib/promise/seq.h"
#include "src/core/lib/resource_quota/arena.h"
#include "src/core/lib/slice/slice.h"
#include "src/core/util/crash.h"
#include "test/core/event_engine/fuzzing_event_engine/fuzzing_event_engine.pb.h"
#include "test/core/filters/filter_matchers.h"
#include "gtest/gtest.h"
#include "absl/memory/memory.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"

using grpc_event_engine::experimental::FuzzingEventEngine;

namespace grpc_core {

///////////////////////////////////////////////////////////////////////////////
// FilterTestV2Base::Call::Impl

class FilterTestV2Base::Call::Impl
    : public std::enable_shared_from_this<FilterTestV2Base::Call::Impl> {
 public:
  Impl(Call* call, std::shared_ptr<Channel::Impl> channel)
      : call_(call), channel_(std::move(channel)) {}
  ~Impl();

  Arena* arena() const { return arena_.get(); }
  const std::shared_ptr<Channel::Impl>& channel() const { return channel_; }
  CallFinalization* call_finalization() { return &call_finalization_; }

  void Start(ClientMetadataHandle md);
  void ForwardServerInitialMetadata(ServerMetadataHandle md);
  void ForwardMessageClientToServer(MessageHandle msg);
  void ForwardMessageServerToClient(MessageHandle msg);
  void FinishNextFilter(ServerMetadataHandle md);

  void StepLoop();

  grpc_event_engine::experimental::EventEngine* event_engine() {
    return channel_->test->event_engine();
  }

  Events& events() { return channel_->test->events; }

 private:
  bool StepOnce();
  Poll<ServerMetadataHandle> PollNextFilter();
  void ForceWakeup();

  Call* const call_;
  std::shared_ptr<Channel::Impl> const channel_;
  RefCountedPtr<Arena> arena_ = channel_->arena_factory->MakeArena();
  bool run_call_finalization_ = false;
  CallFinalization call_finalization_;
  std::optional<ArenaPromise<ServerMetadataHandle>> promise_;
  Poll<ServerMetadataHandle> poll_next_filter_result_;
  Pipe<ServerMetadataHandle> pipe_server_initial_metadata_{arena_.get()};
  Pipe<MessageHandle> pipe_server_to_client_messages_{arena_.get()};
  Pipe<MessageHandle> pipe_client_to_server_messages_{arena_.get()};
  PipeSender<ServerMetadataHandle>* server_initial_metadata_sender_ = nullptr;
  PipeSender<MessageHandle>* server_to_client_messages_sender_ = nullptr;
  PipeReceiver<MessageHandle>* client_to_server_messages_receiver_ = nullptr;
  std::optional<PipeSender<ServerMetadataHandle>::PushType>
      push_server_initial_metadata_;
  std::optional<PipeReceiverNextType<ServerMetadataHandle>>
      next_server_initial_metadata_;
  std::optional<PipeSender<MessageHandle>::PushType>
      push_server_to_client_messages_;
  std::optional<PipeReceiverNextType<MessageHandle>>
      next_server_to_client_messages_;
  std::optional<PipeSender<MessageHandle>::PushType>
      push_client_to_server_messages_;
  std::optional<PipeReceiverNextType<MessageHandle>>
      next_client_to_server_messages_;
  std::optional<ServerMetadataHandle> forward_server_initial_metadata_;
  std::queue<MessageHandle> forward_client_to_server_messages_;
  std::queue<MessageHandle> forward_server_to_client_messages_;
};

FilterTestV2Base::Call::Impl::~Impl() {
  if (!run_call_finalization_) {
    call_finalization_.Run(nullptr);
  }
}

void FilterTestV2Base::Call::Impl::Start(ClientMetadataHandle md) {
  EXPECT_EQ(promise_, std::nullopt);
  promise_ = channel_->filter->MakeCallPromise(
      CallArgs{std::move(md), ClientInitialMetadataOutstandingToken::Empty(),
               nullptr, &pipe_server_initial_metadata_.sender,
               &pipe_client_to_server_messages_.receiver,
               &pipe_server_to_client_messages_.sender},
      [this](CallArgs args) -> ArenaPromise<ServerMetadataHandle> {
        server_initial_metadata_sender_ = args.server_initial_metadata;
        client_to_server_messages_receiver_ = args.client_to_server_messages;
        server_to_client_messages_sender_ = args.server_to_client_messages;
        next_server_initial_metadata_.emplace(
            pipe_server_initial_metadata_.receiver.Next());
        events().Started(call_, *args.client_initial_metadata);
        return [this]() { return PollNextFilter(); };
      });
  EXPECT_NE(promise_, std::nullopt);
  ForceWakeup();
}

Poll<ServerMetadataHandle> FilterTestV2Base::Call::Impl::PollNextFilter() {
  return std::exchange(poll_next_filter_result_, Pending());
}

void FilterTestV2Base::Call::Impl::ForwardServerInitialMetadata(
    ServerMetadataHandle md) {
  EXPECT_FALSE(forward_server_initial_metadata_.has_value());
  forward_server_initial_metadata_ = std::move(md);
  ForceWakeup();
}

void FilterTestV2Base::Call::Impl::ForwardMessageClientToServer(
    MessageHandle msg) {
  forward_client_to_server_messages_.push(std::move(msg));
  ForceWakeup();
}

void FilterTestV2Base::Call::Impl::ForwardMessageServerToClient(
    MessageHandle msg) {
  forward_server_to_client_messages_.push(std::move(msg));
  ForceWakeup();
}

void FilterTestV2Base::Call::Impl::FinishNextFilter(ServerMetadataHandle md) {
  poll_next_filter_result_ = std::move(md);
  ForceWakeup();
}

bool FilterTestV2Base::Call::Impl::StepOnce() {
  if (!promise_.has_value()) return true;

  if (forward_server_initial_metadata_.has_value() &&
      !push_server_initial_metadata_.has_value()) {
    push_server_initial_metadata_.emplace(server_initial_metadata_sender_->Push(
        std::move(*forward_server_initial_metadata_)));
    forward_server_initial_metadata_.reset();
  }

  if (push_server_initial_metadata_.has_value()) {
    auto r = (*push_server_initial_metadata_)();
    if (r.ready()) push_server_initial_metadata_.reset();
  }

  if (next_server_initial_metadata_.has_value()) {
    auto r = (*next_server_initial_metadata_)();
    if (auto* p = r.value_if_ready()) {
      if (p->has_value()) {
        events().ForwardedServerInitialMetadata(call_, *p->value());
      }
      next_server_initial_metadata_.reset();
    }
  }

  if (server_initial_metadata_sender_ != nullptr &&
      !next_server_initial_metadata_.has_value()) {
    // We've finished sending server initial metadata, so we can
    // process server-to-client messages.
    if (!next_server_to_client_messages_.has_value()) {
      next_server_to_client_messages_.emplace(
          pipe_server_to_client_messages_.receiver.Next());
    }

    if (push_server_to_client_messages_.has_value()) {
      auto r = (*push_server_to_client_messages_)();
      if (r.ready()) push_server_to_client_messages_.reset();
    }

    {
      auto r = (*next_server_to_client_messages_)();
      if (auto* p = r.value_if_ready()) {
        if (p->has_value()) {
          events().ForwardedMessageServerToClient(call_, *p->value());
        }
        next_server_to_client_messages_.reset();
        GetContext<Activity>()->ForceImmediateRepoll();
      }
    }

    if (!push_server_to_client_messages_.has_value() &&
        !forward_server_to_client_messages_.empty()) {
      push_server_to_client_messages_.emplace(
          server_to_client_messages_sender_->Push(
              std::move(forward_server_to_client_messages_.front())));
      forward_server_to_client_messages_.pop();
      GetContext<Activity>()->ForceImmediateRepoll();
    }
  }

  if (client_to_server_messages_receiver_ != nullptr) {
    if (!next_client_to_server_messages_.has_value()) {
      next_client_to_server_messages_.emplace(
          client_to_server_messages_receiver_->Next());
    }

    if (push_client_to_server_messages_.has_value()) {
      auto r = (*push_client_to_server_messages_)();
      if (r.ready()) push_client_to_server_messages_.reset();
    }

    {
      auto r = (*next_client_to_server_messages_)();
      if (auto* p = r.value_if_ready()) {
        if (p->has_value()) {
          events().ForwardedMessageClientToServer(call_, *p->value());
        }
        next_client_to_server_messages_.reset();
        GetContext<Activity>()->ForceImmediateRepoll();
      }
    }

    if (!push_client_to_server_messages_.has_value() &&
        !forward_client_to_server_messages_.empty()) {
      push_client_to_server_messages_.emplace(
          pipe_client_to_server_messages_.sender.Push(
              std::move(forward_client_to_server_messages_.front())));
      forward_client_to_server_messages_.pop();
      GetContext<Activity>()->ForceImmediateRepoll();
    }
  }

  auto r = (*promise_)();
  if (r.pending()) return false;
  promise_.reset();
  events().Finished(call_, *r.value());
  return true;
}

///////////////////////////////////////////////////////////////////////////////
// FilterTestV2Base::Call::ScopedContext

class FilterTestV2Base::Call::ScopedContext final
    : public Activity,
      public promise_detail::Context<Arena>,
      public promise_detail::Context<CallFinalization> {
 private:
  class TestWakeable final : public Wakeable {
   public:
    explicit TestWakeable(ScopedContext* ctx)
        : tag_(ctx->DebugTag()), impl_(ctx->impl_) {}
    void Wakeup(WakeupMask) override {
      std::unique_ptr<TestWakeable> self(this);
      auto impl = impl_.lock();
      if (impl == nullptr) return;
      impl->event_engine()->Run([weak_impl = impl_]() {
        auto impl = weak_impl.lock();
        if (impl != nullptr) impl->StepLoop();
      });
    }
    void WakeupAsync(WakeupMask) override { Wakeup(0); }
    void Drop(WakeupMask) override { delete this; }
    std::string ActivityDebugTag(WakeupMask) const override { return tag_; }

   private:
    const std::string tag_;
    const std::weak_ptr<Impl> impl_;
  };

 public:
  explicit ScopedContext(std::shared_ptr<Impl> impl)
      : promise_detail::Context<Arena>(impl->arena()),
        promise_detail::Context<CallFinalization>(impl->call_finalization()),
        impl_(std::move(impl)) {}

  void Orphan() override { Crash("Orphan called on Call::ScopedContext"); }
  void ForceImmediateRepoll(WakeupMask) override { repoll_ = true; }
  Waker MakeOwningWaker() override { return Waker(new TestWakeable(this), 0); }
  Waker MakeNonOwningWaker() override {
    return Waker(new TestWakeable(this), 0);
  }
  std::string DebugTag() const override {
    return absl::StrFormat("FILTER_TEST_CALL[%p]", impl_.get());
  }

  bool repoll() const { return repoll_; }

 private:
  ScopedActivity scoped_activity_{this};
  const std::shared_ptr<Impl> impl_;
  bool repoll_ = false;
};

void FilterTestV2Base::Call::Impl::StepLoop() {
  for (;;) {
    ScopedContext ctx(shared_from_this());
    if (!StepOnce() && ctx.repoll()) continue;
    return;
  }
}

void FilterTestV2Base::Call::Impl::ForceWakeup() {
  ScopedContext(shared_from_this()).MakeOwningWaker().Wakeup();
}

///////////////////////////////////////////////////////////////////////////////
// FilterTestV2Base::Call

FilterTestV2Base::Call::Call(const Channel& channel)
    : impl_(std::make_unique<Impl>(this, channel.impl_)) {}

FilterTestV2Base::Call::~Call() { ScopedContext x(std::move(impl_)); }

Arena* FilterTestV2Base::Call::arena() const { return impl_->arena(); }

ClientMetadataHandle FilterTestV2Base::Call::NewClientMetadata(
    std::initializer_list<std::pair<absl::string_view, absl::string_view>>
        init) {
  auto md = impl_->arena()->MakePooled<ClientMetadata>();
  for (auto& p : init) {
    auto parsed = ClientMetadata::Parse(
        p.first, Slice::FromCopiedString(p.second), false,
        p.first.length() + p.second.length() + 32,
        [p](absl::string_view, const Slice&) {
          Crash(absl::StrCat("Illegal metadata value: ", p.first, ": ",
                             p.second));
        });
    md->Set(parsed);
  }
  return md;
}

ServerMetadataHandle FilterTestV2Base::Call::NewServerMetadata(
    std::initializer_list<std::pair<absl::string_view, absl::string_view>>
        init) {
  auto md = impl_->arena()->MakePooled<ClientMetadata>();
  for (auto& p : init) {
    auto parsed = ServerMetadata::Parse(
        p.first, Slice::FromCopiedString(p.second), false,
        p.first.length() + p.second.length() + 32,
        [p](absl::string_view, const Slice&) {
          Crash(absl::StrCat("Illegal metadata value: ", p.first, ": ",
                             p.second));
        });
    md->Set(parsed);
  }
  return md;
}

MessageHandle FilterTestV2Base::Call::NewMessage(absl::string_view payload,
                                                 uint32_t flags) {
  SliceBuffer buffer;
  if (!payload.empty()) buffer.Append(Slice::FromCopiedString(payload));
  return impl_->arena()->MakePooled<Message>(std::move(buffer), flags);
}

void FilterTestV2Base::Call::Start(ClientMetadataHandle md) {
  ScopedContext ctx(impl_);
  impl_->Start(std::move(md));
}

void FilterTestV2Base::Call::Cancel() {
  ScopedContext ctx(impl_);
  impl_ = absl::make_unique<Impl>(this, impl_->channel());
}

void FilterTestV2Base::Call::ForwardServerInitialMetadata(
    ServerMetadataHandle md) {
  impl_->ForwardServerInitialMetadata(std::move(md));
}

void FilterTestV2Base::Call::ForwardMessageClientToServer(MessageHandle msg) {
  impl_->ForwardMessageClientToServer(std::move(msg));
}

void FilterTestV2Base::Call::ForwardMessageServerToClient(MessageHandle msg) {
  impl_->ForwardMessageServerToClient(std::move(msg));
}

void FilterTestV2Base::Call::FinishNextFilter(ServerMetadataHandle md) {
  impl_->FinishNextFilter(std::move(md));
}

///////////////////////////////////////////////////////////////////////////////
// FilterTestV2Base

FilterTestV2Base::FilterTestV2Base() {
  FuzzingEventEngine::Options options;
  options.max_delay_run_after = std::chrono::milliseconds(500);
  options.max_delay_write = std::chrono::milliseconds(50);
  event_engine_ = std::make_shared<FuzzingEventEngine>(
      options, fuzzing_event_engine::Actions());
  grpc_event_engine::experimental::SetDefaultEventEngine(event_engine_);
  grpc_timer_manager_set_start_threaded(false);
  grpc_init();
}

FilterTestV2Base::~FilterTestV2Base() {
  grpc_shutdown();
  event_engine_->UnsetGlobalHooks();
  event_engine_.reset();
  grpc_event_engine::experimental::ShutdownDefaultEventEngine();
}

void FilterTestV2Base::Step() {
  event_engine_->TickUntilIdle();
  ::testing::Mock::VerifyAndClearExpectations(&events);
}

///////////////////////////////////////////////////////////////////////////////
// FilterTest::TestCallDestination

void FilterTest::TestCallDestination::StartCall(
    UnstartedCallHandler unstarted_call_handler) {
  // Start the call here rather than in GetHandler(): this is what a transport
  // does, and it means the handler is started on the party that created it.
  handlers_.push(unstarted_call_handler.StartCall());
}

std::optional<CallHandler> FilterTest::TestCallDestination::PopHandler() {
  if (handlers_.empty()) return std::nullopt;
  CallHandler handler = std::move(handlers_.front());
  handlers_.pop();
  return handler;
}

///////////////////////////////////////////////////////////////////////////////
// FilterTest: building the stack

ChannelArgs FilterTest::WithTestChannelArgs(const ChannelArgs& args) {
  return args.SetObject<grpc_event_engine::experimental::EventEngine>(
      event_engine());
}

absl::Status FilterTest::FinishInitChannel(InterceptionChainBuilder& builder) {
  CHECK(chain_ == nullptr) << "InitChannel() must be called exactly once";
  absl::StatusOr<RefCountedPtr<UnstartedCallDestination>> chain =
      builder.Build(destination_);
  if (!chain.ok()) return chain.status();
  chain_ = std::move(*chain);
  return absl::OkStatus();
}

void FilterTest::Shutdown() {
  initiator_.reset();
  chain_.reset();
  // Drain any handlers the test never collected, so the calls they keep alive
  // are torn down before the event engine goes away.
  while (destination_->PopHandler().has_value()) {
  }
  destination_.reset();
}

///////////////////////////////////////////////////////////////////////////////
// FilterTest: starting calls

CallInitiator FilterTest::StartCall(
    ClientMetadataHandle client_initial_metadata) {
  CHECK(chain_ != nullptr) << "InitChannel() must be called before StartCall()";
  CHECK(!initiator_.has_value())
      << "StartCall()/StartCallForFilter() may only be called once per test";
  CallInitiatorAndHandler call = MakeCall(std::move(client_initial_metadata));
  InitCallArena(call.handler.arena());
  initiator_ = call.initiator;
  SpawnTestSeq(call.initiator, "start-call",
               [chain = chain_, handler = std::move(call.handler)]() mutable {
                 chain->StartCall(std::move(handler));
               });
  return *initiator_;
}

CallHandler FilterTest::GetHandler() {
  auto poll = [this]() -> Poll<CallHandler> {
    std::optional<CallHandler> handler = destination_->PopHandler();
    if (handler.has_value()) return std::move(*handler);
    return Pending();
  };
  return TickUntil(absl::FunctionRef<Poll<CallHandler>()>(poll));
}

FilterTest::StartedCall FilterTest::StartCallForFilter(
    ClientMetadataHandle client_initial_metadata) {
  CallInitiator initiator = StartCall(std::move(client_initial_metadata));
  CallHandler handler = GetHandler();
  return StartedCall{std::move(initiator), std::move(handler)};
}

CallInitiator FilterTest::initiator() {
  CHECK(initiator_.has_value()) << "StartCall() must be called first";
  return *initiator_;
}

///////////////////////////////////////////////////////////////////////////////
// FilterTest: client -> server operations

namespace {
// A pulled message holds onto the call's state until it is destroyed, which
// blocks the next pull on that call. The Push/Pull API hands values back to the
// test to hold onto for as long as it likes, so release that state here -- on
// the call's party, which is where it must happen -- as soon as the message is
// pulled.
template <typename CallHalf>
typename CallHalf::NextMessage ReleaseCallState(
    typename CallHalf::NextMessage message) {
  if (!message.progressed()) message.Progress();
  return message;
}
}  // namespace

void FilterTest::PushClientMessage(MessageHandle message) {
  initiator().SpawnPushMessage(std::move(message));
}

void FilterTest::PushClientHalfClose() { initiator().SpawnFinishSends(); }

ValueOrFailure<ClientMetadataHandle> FilterTest::PullClientInitialMetadata(
    CallHandler handler) {
  return BlockingRun(
      handler, "pull-client-initial-metadata",
      [handler]() mutable { return handler.PullClientInitialMetadata(); });
}

ClientToServerNextMessage FilterTest::PullClientMessage(CallHandler handler) {
  return BlockingRun(handler, "pull-client-message", [handler]() mutable {
    return Map(handler.PullMessage(), ReleaseCallState<CallHandler>);
  });
}

bool FilterTest::PullClientHalfClose(CallHandler handler) {
  ClientToServerNextMessage message = PullClientMessage(std::move(handler));
  return message.ok() && !message.has_value();
}

///////////////////////////////////////////////////////////////////////////////
// FilterTest: server -> client operations

void FilterTest::PushServerInitialMetadata(CallHandler handler,
                                           ServerMetadataHandle md) {
  handler.SpawnPushServerInitialMetadata(std::move(md));
}

void FilterTest::PushServerMessage(CallHandler handler, MessageHandle message) {
  handler.SpawnPushMessage(std::move(message));
}

void FilterTest::PushServerTrailingMetadata(CallHandler handler,
                                            ServerMetadataHandle md) {
  handler.SpawnPushServerTrailingMetadata(std::move(md));
}

ValueOrFailure<std::optional<ServerMetadataHandle>>
FilterTest::PullServerInitialMetadata() {
  CallInitiator initiator = this->initiator();
  return BlockingRun(
      initiator, "pull-server-initial-metadata",
      [initiator]() mutable { return initiator.PullServerInitialMetadata(); });
}

ServerToClientNextMessage FilterTest::PullServerMessage() {
  CallInitiator initiator = this->initiator();
  return BlockingRun(initiator, "pull-server-message", [initiator]() mutable {
    return Map(initiator.PullMessage(), ReleaseCallState<CallInitiator>);
  });
}

ValueOrFailure<ServerMetadataHandle> FilterTest::PullServerTrailingMetadata() {
  CallInitiator initiator = this->initiator();
  return BlockingRun(
      initiator, "pull-server-trailing-metadata",
      [initiator]() mutable { return initiator.PullServerTrailingMetadata(); });
}

///////////////////////////////////////////////////////////////////////////////
// FilterTest: constructing metadata and messages

namespace {
template <typename Metadata>
Arena::PoolPtr<Metadata> NewMetadata(
    std::initializer_list<std::pair<absl::string_view, absl::string_view>>
        init) {
  Arena::PoolPtr<Metadata> md = Arena::MakePooledForOverwrite<Metadata>();
  for (auto& p : init) {
    ParsedMetadata<Metadata> parsed = Metadata::Parse(
        p.first, Slice::FromCopiedString(p.second), false,
        p.first.length() + p.second.length() + 32,
        [p](absl::string_view, const Slice&) {
          Crash(absl::StrCat("Illegal metadata value: ", p.first, ": ",
                             p.second));
        });
    md->Set(parsed);
  }
  return md;
}
}  // namespace

ClientMetadataHandle FilterTest::NewClientMetadata(
    std::initializer_list<std::pair<absl::string_view, absl::string_view>>
        init) {
  return NewMetadata<ClientMetadata>(init);
}

ServerMetadataHandle FilterTest::NewServerMetadata(
    std::initializer_list<std::pair<absl::string_view, absl::string_view>>
        init) {
  return NewMetadata<ServerMetadata>(init);
}

MessageHandle FilterTest::NewMessage(absl::string_view payload,
                                     uint32_t flags) {
  SliceBuffer buffer;
  if (!payload.empty()) buffer.Append(Slice::FromCopiedString(payload));
  return Arena::MakePooled<Message>(std::move(buffer), flags);
}

}  // namespace grpc_core
