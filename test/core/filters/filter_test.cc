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
#include "src/core/service_config/service_config_call_data.h"
#include "src/core/service_config/service_config_impl.h"
#include "src/core/util/crash.h"
#include "src/core/util/grpc_check.h"
#include "test/core/event_engine/fuzzing_event_engine/fuzzing_event_engine.pb.h"
#include "test/core/filters/filter_matchers.h"
#include "gtest/gtest.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"

namespace grpc_core {

///////////////////////////////////////////////////////////////////////////////
// FilterTest::TestCallDestination

void FilterTest::TestCallDestination::StartCall(
    UnstartedCallHandler unstarted_call_handler) {
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
  GRPC_CHECK(chain_ == nullptr) << "CreateFilterChain() must be called once";
  absl::StatusOr<RefCountedPtr<UnstartedCallDestination>>
      unstarted_call_destination = builder.Build(destination_);
  if (!unstarted_call_destination.ok()) {
    return unstarted_call_destination.status();
  }
  chain_ = std::move(*unstarted_call_destination);
  return absl::OkStatus();
}

void FilterTest::Shutdown() {
  // Cancel the call under test so its parties tear down before the event engine
  // goes away, rather than relying on ref drops alone.
  if (initiator_.has_value()) initiator_->SpawnCancel();
  initiator_.reset();
  handler_.reset();
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
  GRPC_CHECK(chain_ != nullptr)
      << "CreateFilterChain() must be called before StartCall()";
  CallInitiatorAndHandler call = MakeCall(std::move(client_initial_metadata));
  InitAfterCallArena(call.handler.arena());
  SpawnTestSeq(call.initiator, "start-call",
               [chain = chain_, handler = std::move(call.handler)]() mutable {
                 chain->StartCall(std::move(handler));
               });
  return call.initiator;
}

std::optional<CallHandler> FilterTest::GetNextHandler(
    grpc_event_engine::experimental::EventEngine::Duration timeout) {
  auto poll = [this]() -> Poll<CallHandler> {
    std::optional<CallHandler> handler = destination_->PopHandler();
    if (handler.has_value()) return std::move(*handler);
    return Pending();
  };
  return TryTickUntil<CallHandler>(timeout, poll);
}

void FilterTest::StartCallForFilter(
    ClientMetadataHandle client_initial_metadata) {
  GRPC_CHECK(!initiator_.has_value())
      << "StartCallForFilter() may only be called once per test; use "
         "StartCall()/GetNextHandler() for tests that create multiple calls";
  initiator_ = StartCall(std::move(client_initial_metadata));
  auto handler = GetNextHandler();
  GRPC_CHECK(handler.has_value())
      << "a filter must create exactly one child call per call started";
  handler_ = *std::move(handler);
}

void FilterTest::InitAfterCallArena(Arena* arena) {
  if (service_config_ == nullptr) return;
  const ServiceConfigParser::ParsedConfigVector* method_configs =
      service_config_->GetMethodParsedConfigVector(
          Slice::FromCopiedString(kTestPath).c_slice());
  arena->New<ServiceConfigCallData>(arena)->SetServiceConfig(service_config_,
                                                             method_configs);
}

void FilterTest::SetServiceConfig(absl::string_view method_config_fields) {
  absl::StatusOr<RefCountedPtr<ServiceConfig>> service_config =
      ServiceConfigImpl::Create(
          ChannelArgs(), absl::StrCat(R"({"methodConfig":[{"name":[{}],)",
                                      method_config_fields, "}]}"));
  ASSERT_TRUE(service_config.ok()) << service_config.status();
  service_config_ = std::move(*service_config);
}

///////////////////////////////////////////////////////////////////////////////
// FilterTest: initiator operations

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

void FilterTest::PushClientMessage(CallInitiator initiator,
                                   MessageHandle message) {
  initiator.SpawnPushMessage(std::move(message));
}

void FilterTest::PushClientHalfClose(CallInitiator initiator) {
  initiator.SpawnFinishSends();
}

ValueOrFailure<std::optional<ServerMetadataHandle>>
FilterTest::PullServerInitialMetadata(CallInitiator initiator) {
  return BlockingRun(
      initiator, "pull-server-initial-metadata",
      [initiator]() mutable { return initiator.PullServerInitialMetadata(); });
}

ServerToClientNextMessage FilterTest::PullServerMessage(
    CallInitiator initiator) {
  return BlockingRun(initiator, "pull-server-message", [initiator]() mutable {
    return Map(initiator.PullMessage(), ReleaseCallState<CallInitiator>);
  });
}

ValueOrFailure<ServerMetadataHandle> FilterTest::PullServerTrailingMetadata(
    CallInitiator initiator) {
  return BlockingRun(
      initiator, "pull-server-trailing-metadata",
      [initiator]() mutable { return initiator.PullServerTrailingMetadata(); });
}

absl::Status FilterTest::PullServerTrailingStatus(CallInitiator initiator) {
  ValueOrFailure<ServerMetadataHandle> md =
      PullServerTrailingMetadata(std::move(initiator));
  if (!md.ok()) {
    return absl::InternalError("failed to pull server trailing metadata");
  }
  return ServerMetadataToStatus(**md);
}

///////////////////////////////////////////////////////////////////////////////
// FilterTest: handler operations

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
    std::initializer_list<std::pair<absl::string_view, absl::string_view>> init,
    absl::string_view path) {
  auto md = NewMetadata<ClientMetadata>(init);
  md->Set(HttpPathMetadata(), Slice::FromCopiedString(path));
  return md;
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
