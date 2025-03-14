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

#include "src/core/call/call_spine.h"

#include <grpc/grpc.h>

#include <atomic>
#include <memory>
#include <queue>

#include "absl/strings/string_view.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "src/core/call/metadata.h"
#include "src/core/lib/resource_quota/arena.h"
#include "test/core/call/yodel/yodel_test.h"

namespace grpc_core {

using EventEngine = grpc_event_engine::experimental::EventEngine;

namespace {
const absl::string_view kTestPath = "/foo/bar";
}  // namespace

class CallSpineTest : public YodelTest {
 protected:
  using YodelTest::YodelTest;

  ClientMetadataHandle MakeClientInitialMetadata() {
    auto client_initial_metadata =
        Arena::MakePooledForOverwrite<ClientMetadata>();
    client_initial_metadata->Set(HttpPathMetadata(),
                                 Slice::FromCopiedString(kTestPath));
    return client_initial_metadata;
  }

  CallInitiatorAndHandler MakeCall(
      ClientMetadataHandle client_initial_metadata) {
    auto arena = SimpleArenaAllocator()->MakeArena();
    arena->SetContext<grpc_event_engine::experimental::EventEngine>(
        event_engine().get());
    return MakeCallPair(std::move(client_initial_metadata), std::move(arena));
  }

  void UnaryRequest(CallInitiator initiator, CallHandler handler);

 private:
  void InitCoreConfiguration() override {}

  void Shutdown() override {}
};

#define CALL_SPINE_TEST(name) YODEL_TEST(CallSpineTest, name)

CALL_SPINE_TEST(NoOp) {}

CALL_SPINE_TEST(Create) { MakeCall(MakeClientInitialMetadata()); }

void CallSpineTest::UnaryRequest(CallInitiator initiator, CallHandler handler) {
  SpawnTestSeq(
      initiator, "initiator",
      [initiator]() mutable {
        return initiator.PushMessage(Arena::MakePooled<Message>(
            SliceBuffer(Slice::FromCopiedString("hello world")), 0));
      },
      [initiator](StatusFlag status) mutable {
        EXPECT_TRUE(status.ok());
        initiator.FinishSends();
        return initiator.PullServerInitialMetadata();
      },
      [initiator](
          ValueOrFailure<std::optional<ServerMetadataHandle>> md) mutable {
        EXPECT_TRUE(md.ok());
        EXPECT_TRUE(md.value().has_value());
        EXPECT_EQ(*md.value().value()->get_pointer(ContentTypeMetadata()),
                  ContentTypeMetadata::kApplicationGrpc);
        return initiator.PullMessage();
      },
      [initiator](ServerToClientNextMessage msg) mutable {
        EXPECT_TRUE(msg.ok());
        EXPECT_TRUE(msg.has_value());
        EXPECT_EQ(msg.value().payload()->JoinIntoString(),
                  "why hello neighbor");
        return initiator.PullMessage();
      },
      [initiator](ServerToClientNextMessage msg) mutable {
        EXPECT_TRUE(msg.ok());
        EXPECT_FALSE(msg.has_value());
        return initiator.PullServerTrailingMetadata();
      },
      [initiator](ValueOrFailure<ServerMetadataHandle> md) mutable {
        EXPECT_TRUE(md.ok());
        EXPECT_EQ(*md.value()->get_pointer(GrpcStatusMetadata()),
                  GRPC_STATUS_UNIMPLEMENTED);
      });
  SpawnTestSeq(
      handler, "handler",
      [handler]() mutable { return handler.PullClientInitialMetadata(); },
      [handler](ValueOrFailure<ServerMetadataHandle> md) mutable {
        EXPECT_TRUE(md.ok());
        EXPECT_EQ(md.value()->get_pointer(HttpPathMetadata())->as_string_view(),
                  kTestPath);
        return handler.PullMessage();
      },
      [handler](ClientToServerNextMessage msg) mutable {
        EXPECT_TRUE(msg.ok());
        EXPECT_TRUE(msg.has_value());
        EXPECT_EQ(msg.value().payload()->JoinIntoString(), "hello world");
        return handler.PullMessage();
      },
      [handler](ClientToServerNextMessage msg) mutable {
        EXPECT_TRUE(msg.ok());
        EXPECT_FALSE(msg.has_value());
        auto md = Arena::MakePooledForOverwrite<ServerMetadata>();
        md->Set(ContentTypeMetadata(), ContentTypeMetadata::kApplicationGrpc);
        return handler.PushServerInitialMetadata(std::move(md));
      },
      [handler](StatusFlag result) mutable {
        EXPECT_TRUE(result.ok());
        return handler.PushMessage(Arena::MakePooled<Message>(
            SliceBuffer(Slice::FromCopiedString("why hello neighbor")), 0));
      },
      [handler](StatusFlag result) mutable {
        EXPECT_TRUE(result.ok());
        auto md = Arena::MakePooledForOverwrite<ServerMetadata>();
        md->Set(GrpcStatusMetadata(), GRPC_STATUS_UNIMPLEMENTED);
        handler.PushServerTrailingMetadata(std::move(md));
      });
}

CALL_SPINE_TEST(UnaryRequest) {
  auto call = MakeCall(MakeClientInitialMetadata());
  UnaryRequest(call.initiator, call.handler.StartCall());
  WaitForAllPendingWork();
}

CALL_SPINE_TEST(UnaryRequestThroughForwardCall) {
  auto call1 = MakeCall(MakeClientInitialMetadata());
  auto handler = call1.handler.StartCall();
  SpawnTestSeq(
      call1.initiator, "initiator",
      [handler]() mutable { return handler.PullClientInitialMetadata(); },
      [this, handler, initiator = call1.initiator](
          ValueOrFailure<ClientMetadataHandle> md) mutable {
        EXPECT_TRUE(md.ok());
        auto call2 = MakeCall(std::move(md.value()));
        ForwardCall(handler, call2.initiator);
        UnaryRequest(initiator, call2.handler.StartCall());
      });
  WaitForAllPendingWork();
}

CALL_SPINE_TEST(UnaryRequestThroughForwardCallWithServerTrailingMetadataHook) {
  auto call1 = MakeCall(MakeClientInitialMetadata());
  auto handler = call1.handler.StartCall();
  bool got_md = false;
  SpawnTestSeq(
      call1.initiator, "initiator",
      [handler]() mutable { return handler.PullClientInitialMetadata(); },
      [this, handler, initiator = call1.initiator,
       &got_md](ValueOrFailure<ClientMetadataHandle> md) mutable {
        EXPECT_TRUE(md.ok());
        auto call2 = MakeCall(std::move(md.value()));
        ForwardCall(handler, call2.initiator,
                    [&got_md](ServerMetadata&) { got_md = true; });
        UnaryRequest(initiator, call2.handler.StartCall());
      });
  WaitForAllPendingWork();
  EXPECT_TRUE(got_md);
}

}  // namespace grpc_core
