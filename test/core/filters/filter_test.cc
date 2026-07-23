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
