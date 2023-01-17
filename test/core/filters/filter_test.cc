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

#include <queue>

#include "absl/strings/str_cat.h"
#include "absl/types/variant.h"
#include "gtest/gtest.h"

#include "src/core/lib/gprpp/crash.h"
#include "src/core/lib/slice/slice.h"

namespace grpc_core {

///////////////////////////////////////////////////////////////////////////////
// FilterTest::Call::Impl

class FilterTest::Call::Impl {
 public:
  Impl(Call* call, std::shared_ptr<Channel> channel)
      : call_(call), channel_(std::move(channel)) {}

  Arena* arena() { return arena_.get(); }
  grpc_call_context_element* legacy_context() { return legacy_context_; }

  void Start(ClientMetadataHandle md);
  void ForwardServerInitialMetadata(ServerMetadataHandle md);
  void ForwardMessageClientToServer(MessageHandle msg);
  void ForwardMessageServerToClient(MessageHandle msg);
  void FinishNextFilter(ServerMetadataHandle md);

  bool StepOnce();

 private:
  Poll<ServerMetadataHandle> PollNextFilter();

  Call* const call_;
  std::shared_ptr<Channel> const channel_;
  ScopedArenaPtr arena_{MakeScopedArena(channel_->initial_arena_size,
                                        &channel_->memory_allocator)};
  absl::optional<ArenaPromise<ServerMetadataHandle>> promise_;
  Poll<ServerMetadataHandle> poll_next_filter_result_;
  Pipe<ServerMetadataHandle> pipe_server_initial_metadata_{arena_.get()};
  Pipe<MessageHandle> pipe_server_to_client_messages_{arena_.get()};
  Pipe<MessageHandle> pipe_client_to_server_messages_{arena_.get()};
  PipeSender<ServerMetadataHandle>* server_initial_metadata_sender_ = nullptr;
  PipeSender<MessageHandle>* server_to_client_messages_sender_ = nullptr;
  PipeReceiver<MessageHandle>* client_to_server_messages_receiver_ = nullptr;
  absl::optional<PipeSender<ServerMetadataHandle>::PushType>
      push_server_initial_metadata_;
  absl::optional<PipeReceiverNextType<ServerMetadataHandle>>
      next_server_initial_metadata_;
  absl::optional<PipeSender<MessageHandle>::PushType>
      push_server_to_client_messages_;
  absl::optional<PipeReceiverNextType<MessageHandle>>
      next_server_to_client_messages_;
  absl::optional<PipeSender<MessageHandle>::PushType>
      push_client_to_server_messages_;
  absl::optional<PipeReceiverNextType<MessageHandle>>
      next_client_to_server_messages_;
  absl::optional<ServerMetadataHandle> forward_server_initial_metadata_;
  std::queue<MessageHandle> forward_client_to_server_messages_;
  std::queue<MessageHandle> forward_server_to_client_messages_;
  // Contexts for various subsystems (security, tracing, ...).
  grpc_call_context_element legacy_context_[GRPC_CONTEXT_COUNT] = {};
};

void FilterTest::Call::Impl::Start(ClientMetadataHandle md) {
  EXPECT_EQ(promise_, absl::nullopt);
  promise_ = channel_->filter->MakeCallPromise(
      CallArgs{std::move(md), &pipe_server_initial_metadata_.sender,
               &pipe_client_to_server_messages_.receiver,
               &pipe_server_to_client_messages_.sender},
      [this](CallArgs args) -> ArenaPromise<ServerMetadataHandle> {
        server_initial_metadata_sender_ = args.server_initial_metadata;
        client_to_server_messages_receiver_ = args.client_to_server_messages;
        server_to_client_messages_sender_ = args.server_to_client_messages;
        next_server_initial_metadata_.emplace(
            pipe_server_initial_metadata_.receiver.Next());
        call_->Started(*args.client_initial_metadata);
        return [this]() { return PollNextFilter(); };
      });
  EXPECT_NE(promise_, absl::nullopt);
}

Poll<ServerMetadataHandle> FilterTest::Call::Impl::PollNextFilter() {
  return std::exchange(poll_next_filter_result_, Pending());
}

void FilterTest::Call::Impl::ForwardServerInitialMetadata(
    ServerMetadataHandle md) {
  EXPECT_FALSE(forward_server_initial_metadata_.has_value());
  forward_server_initial_metadata_ = std::move(md);
}

void FilterTest::Call::Impl::ForwardMessageClientToServer(MessageHandle msg) {
  forward_client_to_server_messages_.push(std::move(msg));
}

void FilterTest::Call::Impl::ForwardMessageServerToClient(MessageHandle msg) {
  forward_server_to_client_messages_.push(std::move(msg));
}

void FilterTest::Call::Impl::FinishNextFilter(ServerMetadataHandle md) {
  poll_next_filter_result_ = std::move(md);
}

bool FilterTest::Call::Impl::StepOnce() {
  EXPECT_NE(promise_, absl::nullopt);
  if (forward_server_initial_metadata_.has_value() &&
      !push_server_initial_metadata_.has_value()) {
    push_server_initial_metadata_.emplace(server_initial_metadata_sender_->Push(
        std::move(*forward_server_initial_metadata_)));
    forward_server_initial_metadata_.reset();
  }

  if (push_server_initial_metadata_.has_value()) {
    auto r = (*push_server_initial_metadata_)();
    if (!absl::holds_alternative<Pending>(r)) {
      push_server_initial_metadata_.reset();
    }
  }

  if (next_server_initial_metadata_.has_value()) {
    auto r = (*next_server_initial_metadata_)();
    if (auto* p = absl::get_if<NextResult<ServerMetadataHandle>>(&r)) {
      if (p->has_value()) call_->ForwardedServerInitialMetadata(*p->value());
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
      if (!absl::holds_alternative<Pending>(r)) {
        push_server_to_client_messages_.reset();
      }
    }

    {
      auto r = (*next_server_to_client_messages_)();
      if (auto* p = absl::get_if<NextResult<MessageHandle>>(&r)) {
        if (p->has_value()) call_->ForwardedMessageServerToClient(*p->value());
        next_server_to_client_messages_.reset();
        Activity::current()->ForceImmediateRepoll();
      }
    }

    if (!push_server_to_client_messages_.has_value() &&
        !forward_server_to_client_messages_.empty()) {
      push_server_to_client_messages_.emplace(
          server_to_client_messages_sender_->Push(
              std::move(forward_server_to_client_messages_.front())));
      forward_server_to_client_messages_.pop();
      Activity::current()->ForceImmediateRepoll();
    }
  }

  if (client_to_server_messages_receiver_ != nullptr) {
    if (!next_client_to_server_messages_.has_value()) {
      next_client_to_server_messages_.emplace(
          client_to_server_messages_receiver_->Next());
    }

    if (push_client_to_server_messages_.has_value()) {
      auto r = (*push_client_to_server_messages_)();
      if (!absl::holds_alternative<Pending>(r)) {
        push_client_to_server_messages_.reset();
      }
    }

    {
      auto r = (*next_client_to_server_messages_)();
      if (auto* p = absl::get_if<NextResult<MessageHandle>>(&r)) {
        if (p->has_value()) call_->ForwardedMessageClientToServer(*p->value());
        next_client_to_server_messages_.reset();
        Activity::current()->ForceImmediateRepoll();
      }
    }

    if (!push_client_to_server_messages_.has_value() &&
        !forward_client_to_server_messages_.empty()) {
      push_client_to_server_messages_.emplace(
          pipe_client_to_server_messages_.sender.Push(
              std::move(forward_client_to_server_messages_.front())));
      forward_client_to_server_messages_.pop();
      Activity::current()->ForceImmediateRepoll();
    }
  }

  auto r = (*promise_)();
  if (absl::holds_alternative<Pending>(r)) return false;
  promise_.reset();
  call_->Finished(*absl::get<ServerMetadataHandle>(r));
  return true;
}

///////////////////////////////////////////////////////////////////////////////
// FilterTest::Call::ScopedContext

class FilterTest::Call::ScopedContext final
    : public Activity,
      public promise_detail::Context<Arena>,
      public promise_detail::Context<grpc_call_context_element> {
 public:
  ScopedContext(Call* call)
      : promise_detail::Context<Arena>(call->impl_->arena()),
        promise_detail::Context<grpc_call_context_element>(
            call->impl_->legacy_context()),
        call_(call) {}

  void Orphan() override { Crash("Orphan called on Call::ScopedContext"); }
  void ForceImmediateRepoll() override { repoll_ = true; }
  Waker MakeOwningWaker() override { return Waker(new NoOpWakeable(this)); }
  Waker MakeNonOwningWaker() override { return Waker(new NoOpWakeable(this)); }
  std::string DebugTag() const override {
    return absl::StrFormat("FILTER_TEST_CALL[%p]", call_);
  }

  bool repoll() const { return repoll_; }

 private:
  class NoOpWakeable final : public Wakeable {
   public:
    NoOpWakeable(ScopedContext* ctx) : tag_(ctx->DebugTag()) {}
    void Wakeup() override { delete this; }
    void Drop() override { delete this; }
    std::string ActivityDebugTag() const override { return tag_; }

   private:
    const std::string tag_;
  };

  ScopedActivity scoped_activity_{this};
  Call* const call_;
  bool repoll_ = false;
};

///////////////////////////////////////////////////////////////////////////////
// FilterTest::Call

FilterTest::Call::Call(const FilterTest& test)
    : impl_(std::make_unique<Impl>(this, test.channel_)) {}

FilterTest::Call::~Call() {
  ScopedContext ctx(this);
  impl_.reset();
}

ClientMetadataHandle FilterTest::Call::NewClientMetadata(
    std::initializer_list<std::pair<absl::string_view, absl::string_view>>
        init) {
  auto md = impl_->arena()->MakePooled<ClientMetadata>(impl_->arena());
  for (auto& p : init) {
    auto parsed = ClientMetadata::Parse(
        p.first, Slice::FromCopiedString(p.second),
        p.first.length() + p.second.length() + 32,
        [p](absl::string_view, const Slice&) {
          Crash(absl::StrCat("Illegal metadata value: ", p.first, ": ",
                             p.second));
        });
    md->Set(parsed);
  }
  return md;
}

ServerMetadataHandle FilterTest::Call::NewServerMetadata(
    std::initializer_list<std::pair<absl::string_view, absl::string_view>>
        init) {
  auto md = impl_->arena()->MakePooled<ClientMetadata>(impl_->arena());
  for (auto& p : init) {
    auto parsed = ServerMetadata::Parse(
        p.first, Slice::FromCopiedString(p.second),
        p.first.length() + p.second.length() + 32,
        [p](absl::string_view, const Slice&) {
          Crash(absl::StrCat("Illegal metadata value: ", p.first, ": ",
                             p.second));
        });
    md->Set(parsed);
  }
  return md;
}

MessageHandle FilterTest::Call::NewMessage(absl::string_view data,
                                           uint32_t flags) {
  SliceBuffer buffer;
  if (!data.empty()) buffer.Append(Slice::FromCopiedString(data));
  return impl_->arena()->MakePooled<Message>(std::move(buffer), flags);
}

void FilterTest::Call::Start(ClientMetadataHandle md) {
  ScopedContext ctx(this);
  impl_->Start(std::move(md));
}

void FilterTest::Call::ForwardServerInitialMetadata(ServerMetadataHandle md) {
  impl_->ForwardServerInitialMetadata(std::move(md));
}

void FilterTest::Call::ForwardMessageClientToServer(MessageHandle msg) {
  impl_->ForwardMessageClientToServer(std::move(msg));
}

void FilterTest::Call::ForwardMessageServerToClient(MessageHandle msg) {
  impl_->ForwardMessageServerToClient(std::move(msg));
}

void FilterTest::Call::FinishNextFilter(ServerMetadataHandle md) {
  impl_->FinishNextFilter(std::move(md));
}

void FilterTest::Call::Step() {
  for (;;) {
    ScopedContext ctx(this);
    if (!impl_->StepOnce() && ctx.repoll()) continue;
    return;
  }
}

///////////////////////////////////////////////////////////////////////////////
// FilterTest

}  // namespace grpc_core
