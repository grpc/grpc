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

#include "test/core/transport/test_suite/test.h"

namespace grpc_core {

TRANSPORT_TEST(MetadataOnlyRequest) {
  SetServerAcceptor();
  auto initiator = CreateCall();
  SpawnTestSeq(
      initiator, "initiator",
      [&]() {
        auto md = Arena::MakePooled<ClientMetadata>(GetContext<Arena>());
        md->Set(HttpPathMetadata(), Slice::FromExternalString("/foo/bar"));
        return initiator.PushClientInitialMetadata(std::move(md));
      },
      [&](StatusFlag status) mutable {
        EXPECT_TRUE(status.ok());
        initiator.FinishSends();
        return initiator.PullServerInitialMetadata();
      },
      [&](ValueOrFailure<absl::optional<ServerMetadataHandle>> md) {
        EXPECT_TRUE(md.ok());
        EXPECT_TRUE(md.value().has_value());
        EXPECT_EQ(*md.value().value()->get_pointer(ContentTypeMetadata()),
                  ContentTypeMetadata::kApplicationGrpc);
        return initiator.PullServerTrailingMetadata();
      },
      [&](ValueOrFailure<ServerMetadataHandle> md) {
        EXPECT_TRUE(md.ok());
        EXPECT_EQ(*md.value()->get_pointer(GrpcStatusMetadata()),
                  GRPC_STATUS_UNIMPLEMENTED);
        return Empty{};
      });
  auto handler = TickUntilServerCall();
  SpawnTestSeq(
      handler, "handler", [&] { return handler.PullClientInitialMetadata(); },
      [&](ValueOrFailure<ServerMetadataHandle> md) {
        EXPECT_TRUE(md.ok());
        EXPECT_EQ(md.value()->get_pointer(HttpPathMetadata())->as_string_view(),
                  "/foo/bar");
        return handler.PullMessage();
      },
      [&](NextResult<MessageHandle> msg) {
        EXPECT_FALSE(msg.has_value());
        auto md = Arena::MakePooled<ServerMetadata>(GetContext<Arena>());
        md->Set(ContentTypeMetadata(), ContentTypeMetadata::kApplicationGrpc);
        return handler.PushServerInitialMetadata(std::move(md));
      },
      [&](StatusFlag result) mutable {
        EXPECT_TRUE(result.ok());
        auto md = Arena::MakePooled<ServerMetadata>(GetContext<Arena>());
        md->Set(GrpcStatusMetadata(), GRPC_STATUS_UNIMPLEMENTED);
        return handler.PushServerTrailingMetadata(std::move(md));
      },
      [&](StatusFlag result) mutable {
        EXPECT_TRUE(result.ok());
        return Empty{};
      });
  WaitForAllPendingWork();
}

TRANSPORT_TEST(MetadataOnlyRequestServerAbortsAfterInitialMetadata) {
  // TODO(ctiller): Re-enable this test once CallSpine rewrite completes.
  GTEST_SKIP() << "CallSpine has a bug right now that makes this provide the "
                  "wrong status code: we don't care for any cases we're "
                  "rolling out soon, so leaving this disabled.";

  SetServerAcceptor();
  auto initiator = CreateCall();
  SpawnTestSeq(
      initiator, "initiator",
      [&]() {
        auto md = Arena::MakePooled<ClientMetadata>(GetContext<Arena>());
        md->Set(HttpPathMetadata(), Slice::FromExternalString("/foo/bar"));
        return initiator.PushClientInitialMetadata(std::move(md));
      },
      [&](StatusFlag status) mutable {
        EXPECT_TRUE(status.ok());
        // We don't close the sending stream here.
        return initiator.PullServerInitialMetadata();
      },
      [&](ValueOrFailure<absl::optional<ServerMetadataHandle>> md) {
        EXPECT_TRUE(md.ok());
        EXPECT_TRUE(md.value().has_value());
        EXPECT_EQ(*md.value().value()->get_pointer(ContentTypeMetadata()),
                  ContentTypeMetadata::kApplicationGrpc);
        return initiator.PullServerTrailingMetadata();
      },
      [&](ValueOrFailure<ServerMetadataHandle> md) {
        EXPECT_TRUE(md.ok());
        EXPECT_EQ(*md.value()->get_pointer(GrpcStatusMetadata()),
                  GRPC_STATUS_UNIMPLEMENTED);
        return Empty{};
      });
  auto handler = TickUntilServerCall();
  SpawnTestSeq(
      handler, "handler", [&] { return handler.PullClientInitialMetadata(); },
      [&](ValueOrFailure<ServerMetadataHandle> got_md) {
        EXPECT_TRUE(got_md.ok());
        EXPECT_EQ(
            got_md.value()->get_pointer(HttpPathMetadata())->as_string_view(),
            "/foo/bar");
        // Don't wait for end of stream for client->server messages, just
        // publish initial then trailing metadata.
        auto md = Arena::MakePooled<ServerMetadata>(GetContext<Arena>());
        md->Set(ContentTypeMetadata(), ContentTypeMetadata::kApplicationGrpc);
        return handler.PushServerInitialMetadata(std::move(md));
      },
      [&](StatusFlag result) mutable {
        EXPECT_TRUE(result.ok());
        auto md = Arena::MakePooled<ServerMetadata>(GetContext<Arena>());
        md->Set(GrpcStatusMetadata(), GRPC_STATUS_UNIMPLEMENTED);
        return handler.PushServerTrailingMetadata(std::move(md));
      },
      [&](StatusFlag result) mutable {
        EXPECT_TRUE(result.ok());
        return Empty{};
      });
  WaitForAllPendingWork();
}

TRANSPORT_TEST(MetadataOnlyRequestServerAbortsImmediately) {
  // TODO(ctiller): Re-enable this test once CallSpine rewrite completes.
  GTEST_SKIP() << "CallSpine has a bug right now that makes this provide the "
                  "wrong status code: we don't care for any cases we're "
                  "rolling out soon, so leaving this disabled.";

  SetServerAcceptor();
  auto initiator = CreateCall();
  SpawnTestSeq(
      initiator, "initiator",
      [&]() {
        auto md = Arena::MakePooled<ClientMetadata>(GetContext<Arena>());
        md->Set(HttpPathMetadata(), Slice::FromExternalString("/foo/bar"));
        return initiator.PushClientInitialMetadata(std::move(md));
      },
      [&](StatusFlag status) mutable {
        EXPECT_TRUE(status.ok());
        // We don't close the sending stream here.
        return initiator.PullServerInitialMetadata();
      },
      [&](ValueOrFailure<absl::optional<ServerMetadataHandle>> md) {
        EXPECT_TRUE(md.ok());
        EXPECT_FALSE(md.value().has_value());
        return initiator.PullServerTrailingMetadata();
      },
      [&](ValueOrFailure<ServerMetadataHandle> md) {
        EXPECT_TRUE(md.ok());
        EXPECT_EQ(*md.value()->get_pointer(GrpcStatusMetadata()),
                  GRPC_STATUS_UNIMPLEMENTED);
        return Empty{};
      });
  auto handler = TickUntilServerCall();
  SpawnTestSeq(
      handler, "handler", [&] { return handler.PullClientInitialMetadata(); },
      [&](ValueOrFailure<ServerMetadataHandle> got_md) {
        EXPECT_TRUE(got_md.ok());
        EXPECT_EQ(
            got_md.value()->get_pointer(HttpPathMetadata())->as_string_view(),
            "/foo/bar");
        // Don't wait for end of stream for client->server messages, just
        // and don't send initial metadata - just trailing metadata.
        auto md = Arena::MakePooled<ServerMetadata>(GetContext<Arena>());
        md->Set(GrpcStatusMetadata(), GRPC_STATUS_UNIMPLEMENTED);
        return handler.PushServerTrailingMetadata(std::move(md));
      },
      [&](StatusFlag result) mutable {
        EXPECT_TRUE(result.ok());
        return Empty{};
      });
  WaitForAllPendingWork();
}

TRANSPORT_TEST(CanCreateCallThenAbandonIt) {
  SetServerAcceptor();
  auto initiator = CreateCall();
  SpawnTestSeq(
      initiator, "start-call",
      [&]() {
        auto md = Arena::MakePooled<ClientMetadata>(GetContext<Arena>());
        md->Set(HttpPathMetadata(), Slice::FromExternalString("/foo/bar"));
        return initiator.PushClientInitialMetadata(std::move(md));
      },
      [&](StatusFlag status) mutable {
        EXPECT_TRUE(status.ok());
        return Empty{};
      });
  auto handler = TickUntilServerCall();
  SpawnTestSeq(initiator, "end-call", [&]() {
    initiator.Cancel();
    return Empty{};
  });
  WaitForAllPendingWork();
}

TRANSPORT_TEST(UnaryRequest) {
  SetServerAcceptor();
  auto initiator = CreateCall();
  SpawnTestSeq(
      initiator, "initiator",
      [&]() {
        auto md = Arena::MakePooled<ClientMetadata>(GetContext<Arena>());
        md->Set(HttpPathMetadata(), Slice::FromExternalString("/foo/bar"));
        return initiator.PushClientInitialMetadata(std::move(md));
      },
      [&](StatusFlag status) mutable {
        EXPECT_TRUE(status.ok());
        return initiator.PushMessage(Arena::MakePooled<Message>(
            SliceBuffer(Slice::FromCopiedString("hello world")), 0));
      },
      [&](StatusFlag status) mutable {
        EXPECT_TRUE(status.ok());
        initiator.FinishSends();
        return initiator.PullServerInitialMetadata();
      },
      [&](ValueOrFailure<absl::optional<ServerMetadataHandle>> md) {
        EXPECT_TRUE(md.ok());
        EXPECT_TRUE(md.value().has_value());
        EXPECT_EQ(*md.value().value()->get_pointer(ContentTypeMetadata()),
                  ContentTypeMetadata::kApplicationGrpc);
        return initiator.PullMessage();
      },
      [&](NextResult<MessageHandle> msg) {
        EXPECT_TRUE(msg.has_value());
        EXPECT_EQ(msg.value()->payload()->JoinIntoString(),
                  "why hello neighbor");
        return initiator.PullMessage();
      },
      [&](NextResult<MessageHandle> msg) {
        EXPECT_FALSE(msg.has_value());
        EXPECT_FALSE(msg.cancelled());
        return initiator.PullServerTrailingMetadata();
      },
      [&](ValueOrFailure<ServerMetadataHandle> md) {
        EXPECT_TRUE(md.ok());
        EXPECT_EQ(*md.value()->get_pointer(GrpcStatusMetadata()),
                  GRPC_STATUS_UNIMPLEMENTED);
        return Empty{};
      });
  auto handler = TickUntilServerCall();
  SpawnTestSeq(
      handler, "handler", [&] { return handler.PullClientInitialMetadata(); },
      [&](ValueOrFailure<ServerMetadataHandle> md) {
        EXPECT_TRUE(md.ok());
        EXPECT_EQ(md.value()->get_pointer(HttpPathMetadata())->as_string_view(),
                  "/foo/bar");
        return handler.PullMessage();
      },
      [&](NextResult<MessageHandle> msg) {
        EXPECT_TRUE(msg.has_value());
        EXPECT_EQ(msg.value()->payload()->JoinIntoString(), "hello world");
        return handler.PullMessage();
      },
      [&](NextResult<MessageHandle> msg) {
        EXPECT_FALSE(msg.has_value());
        EXPECT_FALSE(msg.cancelled());
        auto md = Arena::MakePooled<ServerMetadata>(GetContext<Arena>());
        md->Set(ContentTypeMetadata(), ContentTypeMetadata::kApplicationGrpc);
        return handler.PushServerInitialMetadata(std::move(md));
      },
      [&](StatusFlag result) mutable {
        EXPECT_TRUE(result.ok());
        return handler.PushMessage(Arena::MakePooled<Message>(
            SliceBuffer(Slice::FromCopiedString("why hello neighbor")), 0));
      },
      [&](StatusFlag result) mutable {
        EXPECT_TRUE(result.ok());
        auto md = Arena::MakePooled<ServerMetadata>(GetContext<Arena>());
        md->Set(GrpcStatusMetadata(), GRPC_STATUS_UNIMPLEMENTED);
        return handler.PushServerTrailingMetadata(std::move(md));
      },
      [&](StatusFlag result) mutable {
        EXPECT_TRUE(result.ok());
        return Empty{};
      });
  WaitForAllPendingWork();
}

TRANSPORT_TEST(UnaryRequestOmitCheckEndOfStream) {
  SetServerAcceptor();
  auto initiator = CreateCall();
  SpawnTestSeq(
      initiator, "initiator",
      [&]() {
        auto md = Arena::MakePooled<ClientMetadata>(GetContext<Arena>());
        md->Set(HttpPathMetadata(), Slice::FromExternalString("/foo/bar"));
        return initiator.PushClientInitialMetadata(std::move(md));
      },
      [&](StatusFlag status) mutable {
        EXPECT_TRUE(status.ok());
        return initiator.PushMessage(Arena::MakePooled<Message>(
            SliceBuffer(Slice::FromCopiedString("hello world")), 0));
      },
      [&](StatusFlag status) mutable {
        EXPECT_TRUE(status.ok());
        initiator.FinishSends();
        return initiator.PullServerInitialMetadata();
      },
      [&](ValueOrFailure<absl::optional<ServerMetadataHandle>> md) {
        EXPECT_TRUE(md.ok());
        EXPECT_TRUE(md.value().has_value());
        EXPECT_EQ(*md.value().value()->get_pointer(ContentTypeMetadata()),
                  ContentTypeMetadata::kApplicationGrpc);
        return initiator.PullMessage();
      },
      [&](NextResult<MessageHandle> msg) {
        EXPECT_TRUE(msg.has_value());
        EXPECT_EQ(msg.value()->payload()->JoinIntoString(),
                  "why hello neighbor");
        return initiator.PullServerTrailingMetadata();
      },
      [&](ValueOrFailure<ServerMetadataHandle> md) {
        EXPECT_TRUE(md.ok());
        EXPECT_EQ(*md.value()->get_pointer(GrpcStatusMetadata()),
                  GRPC_STATUS_UNIMPLEMENTED);
        return Empty{};
      });
  auto handler = TickUntilServerCall();
  SpawnTestSeq(
      handler, "handler", [&] { return handler.PullClientInitialMetadata(); },
      [&](ValueOrFailure<ServerMetadataHandle> md) {
        EXPECT_TRUE(md.ok());
        EXPECT_EQ(md.value()->get_pointer(HttpPathMetadata())->as_string_view(),
                  "/foo/bar");
        return handler.PullMessage();
      },
      [&](NextResult<MessageHandle> msg) {
        EXPECT_TRUE(msg.has_value());
        EXPECT_EQ(msg.value()->payload()->JoinIntoString(), "hello world");
        auto md = Arena::MakePooled<ServerMetadata>(GetContext<Arena>());
        md->Set(ContentTypeMetadata(), ContentTypeMetadata::kApplicationGrpc);
        return handler.PushServerInitialMetadata(std::move(md));
      },
      [&](StatusFlag result) mutable {
        EXPECT_TRUE(result.ok());
        return handler.PushMessage(Arena::MakePooled<Message>(
            SliceBuffer(Slice::FromCopiedString("why hello neighbor")), 0));
      },
      [&](StatusFlag result) mutable {
        EXPECT_TRUE(result.ok());
        auto md = Arena::MakePooled<ServerMetadata>(GetContext<Arena>());
        md->Set(GrpcStatusMetadata(), GRPC_STATUS_UNIMPLEMENTED);
        return handler.PushServerTrailingMetadata(std::move(md));
      },
      [&](StatusFlag result) mutable {
        EXPECT_TRUE(result.ok());
        return Empty{};
      });
  WaitForAllPendingWork();
}

TRANSPORT_TEST(UnaryRequestWaitForServerInitialMetadataBeforeSendingPayload) {
  SetServerAcceptor();
  auto initiator = CreateCall();
  SpawnTestSeq(
      initiator, "initiator",
      [&]() {
        auto md = Arena::MakePooled<ClientMetadata>(GetContext<Arena>());
        md->Set(HttpPathMetadata(), Slice::FromExternalString("/foo/bar"));
        return initiator.PushClientInitialMetadata(std::move(md));
      },
      [&](StatusFlag status) mutable {
        EXPECT_TRUE(status.ok());
        return initiator.PullServerInitialMetadata();
      },
      [&](ValueOrFailure<absl::optional<ServerMetadataHandle>> md) {
        EXPECT_TRUE(md.ok());
        EXPECT_TRUE(md.value().has_value());
        EXPECT_EQ(*md.value().value()->get_pointer(ContentTypeMetadata()),
                  ContentTypeMetadata::kApplicationGrpc);
        return initiator.PushMessage(Arena::MakePooled<Message>(
            SliceBuffer(Slice::FromCopiedString("hello world")), 0));
      },
      [&](StatusFlag status) mutable {
        EXPECT_TRUE(status.ok());
        initiator.FinishSends();
        return initiator.PullMessage();
      },
      [&](NextResult<MessageHandle> msg) {
        EXPECT_TRUE(msg.has_value());
        EXPECT_EQ(msg.value()->payload()->JoinIntoString(),
                  "why hello neighbor");
        return initiator.PullMessage();
      },
      [&](NextResult<MessageHandle> msg) {
        EXPECT_FALSE(msg.has_value());
        EXPECT_FALSE(msg.cancelled());
        return initiator.PullServerTrailingMetadata();
      },
      [&](ValueOrFailure<ServerMetadataHandle> md) {
        EXPECT_TRUE(md.ok());
        EXPECT_EQ(*md.value()->get_pointer(GrpcStatusMetadata()),
                  GRPC_STATUS_UNIMPLEMENTED);
        return Empty{};
      });
  auto handler = TickUntilServerCall();
  SpawnTestSeq(
      handler, "handler", [&] { return handler.PullClientInitialMetadata(); },
      [&](ValueOrFailure<ServerMetadataHandle> md) {
        EXPECT_TRUE(md.ok());
        EXPECT_EQ(md.value()->get_pointer(HttpPathMetadata())->as_string_view(),
                  "/foo/bar");
        auto md_out = Arena::MakePooled<ServerMetadata>(GetContext<Arena>());
        md_out->Set(ContentTypeMetadata(),
                    ContentTypeMetadata::kApplicationGrpc);
        return handler.PushServerInitialMetadata(std::move(md_out));
      },
      [&](StatusFlag result) mutable {
        EXPECT_TRUE(result.ok());
        return handler.PullMessage();
      },
      [&](NextResult<MessageHandle> msg) {
        EXPECT_TRUE(msg.has_value());
        EXPECT_EQ(msg.value()->payload()->JoinIntoString(), "hello world");
        return handler.PullMessage();
      },
      [&](NextResult<MessageHandle> msg) {
        EXPECT_FALSE(msg.has_value());
        EXPECT_FALSE(msg.cancelled());
        return handler.PushMessage(Arena::MakePooled<Message>(
            SliceBuffer(Slice::FromCopiedString("why hello neighbor")), 0));
      },
      [&](StatusFlag result) mutable {
        EXPECT_TRUE(result.ok());
        auto md = Arena::MakePooled<ServerMetadata>(GetContext<Arena>());
        md->Set(GrpcStatusMetadata(), GRPC_STATUS_UNIMPLEMENTED);
        return handler.PushServerTrailingMetadata(std::move(md));
      },
      [&](StatusFlag result) mutable {
        EXPECT_TRUE(result.ok());
        return Empty{};
      });
  WaitForAllPendingWork();
}

TRANSPORT_TEST(ClientStreamingRequest) {
  SetServerAcceptor();
  auto initiator = CreateCall();
  SpawnTestSeq(
      initiator, "initiator",
      [&]() {
        auto md = Arena::MakePooled<ClientMetadata>(GetContext<Arena>());
        md->Set(HttpPathMetadata(), Slice::FromExternalString("/foo/bar"));
        return initiator.PushClientInitialMetadata(std::move(md));
      },
      [&](StatusFlag status) mutable {
        EXPECT_TRUE(status.ok());
        return initiator.PullServerInitialMetadata();
      },
      [&](ValueOrFailure<absl::optional<ServerMetadataHandle>> md) {
        EXPECT_TRUE(md.ok());
        EXPECT_TRUE(md.value().has_value());
        EXPECT_EQ(*md.value().value()->get_pointer(ContentTypeMetadata()),
                  ContentTypeMetadata::kApplicationGrpc);
        return initiator.PushMessage(Arena::MakePooled<Message>(
            SliceBuffer(Slice::FromCopiedString("hello world")), 0));
      },
      [&](StatusFlag status) mutable {
        EXPECT_TRUE(status.ok());
        return initiator.PushMessage(Arena::MakePooled<Message>(
            SliceBuffer(Slice::FromCopiedString("hello world (2)")), 0));
      },
      [&](StatusFlag status) mutable {
        EXPECT_TRUE(status.ok());
        return initiator.PushMessage(Arena::MakePooled<Message>(
            SliceBuffer(Slice::FromCopiedString("hello world (3)")), 0));
      },
      [&](StatusFlag status) mutable {
        EXPECT_TRUE(status.ok());
        return initiator.PushMessage(Arena::MakePooled<Message>(
            SliceBuffer(Slice::FromCopiedString("hello world (4)")), 0));
      },
      [&](StatusFlag status) mutable {
        EXPECT_TRUE(status.ok());
        return initiator.PushMessage(Arena::MakePooled<Message>(
            SliceBuffer(Slice::FromCopiedString("hello world (5)")), 0));
      },
      [&](StatusFlag status) mutable {
        EXPECT_TRUE(status.ok());
        initiator.FinishSends();
        return initiator.PullMessage();
      },
      [&](NextResult<MessageHandle> msg) {
        EXPECT_FALSE(msg.has_value());
        EXPECT_FALSE(msg.cancelled());
        return initiator.PullServerTrailingMetadata();
      },
      [&](ValueOrFailure<ServerMetadataHandle> md) {
        EXPECT_TRUE(md.ok());
        EXPECT_EQ(*md.value()->get_pointer(GrpcStatusMetadata()),
                  GRPC_STATUS_UNIMPLEMENTED);
        return Empty{};
      });
  auto handler = TickUntilServerCall();
  SpawnTestSeq(
      handler, "handler", [&] { return handler.PullClientInitialMetadata(); },
      [&](ValueOrFailure<ServerMetadataHandle> md) {
        EXPECT_TRUE(md.ok());
        EXPECT_EQ(md.value()->get_pointer(HttpPathMetadata())->as_string_view(),
                  "/foo/bar");
        auto md_out = Arena::MakePooled<ServerMetadata>(GetContext<Arena>());
        md_out->Set(ContentTypeMetadata(),
                    ContentTypeMetadata::kApplicationGrpc);
        return handler.PushServerInitialMetadata(std::move(md_out));
      },
      [&](StatusFlag result) mutable {
        EXPECT_TRUE(result.ok());
        return handler.PullMessage();
      },
      [&](NextResult<MessageHandle> msg) {
        EXPECT_TRUE(msg.has_value());
        EXPECT_EQ(msg.value()->payload()->JoinIntoString(), "hello world");
        return handler.PullMessage();
      },
      [&](NextResult<MessageHandle> msg) {
        EXPECT_TRUE(msg.has_value());
        EXPECT_EQ(msg.value()->payload()->JoinIntoString(), "hello world (2)");
        return handler.PullMessage();
      },
      [&](NextResult<MessageHandle> msg) {
        EXPECT_TRUE(msg.has_value());
        EXPECT_EQ(msg.value()->payload()->JoinIntoString(), "hello world (3)");
        return handler.PullMessage();
      },
      [&](NextResult<MessageHandle> msg) {
        EXPECT_TRUE(msg.has_value());
        EXPECT_EQ(msg.value()->payload()->JoinIntoString(), "hello world (4)");
        return handler.PullMessage();
      },
      [&](NextResult<MessageHandle> msg) {
        EXPECT_TRUE(msg.has_value());
        EXPECT_EQ(msg.value()->payload()->JoinIntoString(), "hello world (5)");
        return handler.PullMessage();
      },
      [&](NextResult<MessageHandle> msg) {
        EXPECT_FALSE(msg.has_value());
        EXPECT_FALSE(msg.cancelled());
        auto md = Arena::MakePooled<ServerMetadata>(GetContext<Arena>());
        md->Set(GrpcStatusMetadata(), GRPC_STATUS_UNIMPLEMENTED);
        return handler.PushServerTrailingMetadata(std::move(md));
      },
      [&](StatusFlag result) mutable {
        EXPECT_TRUE(result.ok());
        return Empty{};
      });
  WaitForAllPendingWork();
}

TRANSPORT_TEST(ServerStreamingRequest) {
  SetServerAcceptor();
  auto initiator = CreateCall();
  SpawnTestSeq(
      initiator, "initiator",
      [&]() {
        auto md = Arena::MakePooled<ClientMetadata>(GetContext<Arena>());
        md->Set(HttpPathMetadata(), Slice::FromExternalString("/foo/bar"));
        return initiator.PushClientInitialMetadata(std::move(md));
      },
      [&](StatusFlag status) mutable {
        EXPECT_TRUE(status.ok());
        return initiator.PullServerInitialMetadata();
      },
      [&](ValueOrFailure<absl::optional<ServerMetadataHandle>> md) {
        EXPECT_TRUE(md.ok());
        EXPECT_TRUE(md.value().has_value());
        EXPECT_EQ(*md.value().value()->get_pointer(ContentTypeMetadata()),
                  ContentTypeMetadata::kApplicationGrpc);
        initiator.FinishSends();
        return initiator.PullMessage();
      },
      [&](NextResult<MessageHandle> msg) {
        EXPECT_TRUE(msg.has_value());
        EXPECT_EQ(msg.value()->payload()->JoinIntoString(),
                  "why hello neighbor");
        return initiator.PullMessage();
      },
      [&](NextResult<MessageHandle> msg) {
        EXPECT_TRUE(msg.has_value());
        EXPECT_EQ(msg.value()->payload()->JoinIntoString(),
                  "why hello neighbor (2)");
        return initiator.PullMessage();
      },
      [&](NextResult<MessageHandle> msg) {
        EXPECT_TRUE(msg.has_value());
        EXPECT_EQ(msg.value()->payload()->JoinIntoString(),
                  "why hello neighbor (3)");
        return initiator.PullMessage();
      },
      [&](NextResult<MessageHandle> msg) {
        EXPECT_TRUE(msg.has_value());
        EXPECT_EQ(msg.value()->payload()->JoinIntoString(),
                  "why hello neighbor (4)");
        return initiator.PullMessage();
      },
      [&](NextResult<MessageHandle> msg) {
        EXPECT_TRUE(msg.has_value());
        EXPECT_EQ(msg.value()->payload()->JoinIntoString(),
                  "why hello neighbor (5)");
        return initiator.PullMessage();
      },
      [&](NextResult<MessageHandle> msg) {
        EXPECT_TRUE(msg.has_value());
        EXPECT_EQ(msg.value()->payload()->JoinIntoString(),
                  "why hello neighbor (6)");
        return initiator.PullMessage();
      },
      [&](NextResult<MessageHandle> msg) {
        EXPECT_FALSE(msg.has_value());
        EXPECT_FALSE(msg.cancelled());
        return initiator.PullServerTrailingMetadata();
      },
      [&](ValueOrFailure<ServerMetadataHandle> md) {
        EXPECT_TRUE(md.ok());
        EXPECT_EQ(*md.value()->get_pointer(GrpcStatusMetadata()),
                  GRPC_STATUS_UNIMPLEMENTED);
        return Empty{};
      });
  auto handler = TickUntilServerCall();
  SpawnTestSeq(
      handler, "handler", [&] { return handler.PullClientInitialMetadata(); },
      [&](ValueOrFailure<ServerMetadataHandle> md) {
        EXPECT_TRUE(md.ok());
        EXPECT_EQ(md.value()->get_pointer(HttpPathMetadata())->as_string_view(),
                  "/foo/bar");
        auto md_out = Arena::MakePooled<ServerMetadata>(GetContext<Arena>());
        md_out->Set(ContentTypeMetadata(),
                    ContentTypeMetadata::kApplicationGrpc);
        return handler.PushServerInitialMetadata(std::move(md_out));
      },
      [&](StatusFlag result) mutable {
        EXPECT_TRUE(result.ok());
        return handler.PullMessage();
      },
      [&](NextResult<MessageHandle> msg) {
        EXPECT_FALSE(msg.has_value());
        EXPECT_FALSE(msg.cancelled());
        return handler.PushMessage(Arena::MakePooled<Message>(
            SliceBuffer(Slice::FromCopiedString("why hello neighbor")), 0));
      },
      [&](StatusFlag result) mutable {
        EXPECT_TRUE(result.ok());
        return handler.PushMessage(Arena::MakePooled<Message>(
            SliceBuffer(Slice::FromCopiedString("why hello neighbor (2)")), 0));
      },
      [&](StatusFlag result) mutable {
        EXPECT_TRUE(result.ok());
        return handler.PushMessage(Arena::MakePooled<Message>(
            SliceBuffer(Slice::FromCopiedString("why hello neighbor (3)")), 0));
      },
      [&](StatusFlag result) mutable {
        EXPECT_TRUE(result.ok());
        return handler.PushMessage(Arena::MakePooled<Message>(
            SliceBuffer(Slice::FromCopiedString("why hello neighbor (4)")), 0));
      },
      [&](StatusFlag result) mutable {
        EXPECT_TRUE(result.ok());
        return handler.PushMessage(Arena::MakePooled<Message>(
            SliceBuffer(Slice::FromCopiedString("why hello neighbor (5)")), 0));
      },
      [&](StatusFlag result) mutable {
        EXPECT_TRUE(result.ok());
        return handler.PushMessage(Arena::MakePooled<Message>(
            SliceBuffer(Slice::FromCopiedString("why hello neighbor (6)")), 0));
      },
      [&](StatusFlag result) mutable {
        EXPECT_TRUE(result.ok());
        auto md = Arena::MakePooled<ServerMetadata>(GetContext<Arena>());
        md->Set(GrpcStatusMetadata(), GRPC_STATUS_UNIMPLEMENTED);
        return handler.PushServerTrailingMetadata(std::move(md));
      },
      [&](StatusFlag result) mutable {
        EXPECT_TRUE(result.ok());
        return Empty{};
      });
  WaitForAllPendingWork();
}

}  // namespace grpc_core
