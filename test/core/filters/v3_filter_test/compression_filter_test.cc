// Copyright 2026 gRPC authors.
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

#include <grpc/impl/channel_arg_names.h>
#include <grpc/impl/compression_types.h>
#include <grpc/impl/grpc_types.h>
#include <grpc/status.h>

#include <optional>
#include <string>

#include "absl/status/status.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "src/core/call/message.h"
#include "src/core/call/metadata.h"
#include "src/core/ext/filters/http/message_compress/compression_filter.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/promise/status_flag.h"
#include "src/core/lib/resource_quota/arena.h"
#include "src/core/lib/slice/slice.h"
#include "src/core/lib/slice/slice_buffer.h"
#include "test/core/filters/filter_matchers.h"
#include "test/core/filters/v3_filter_test/v3_filter_test.h"

namespace grpc_core {

namespace {
// A highly compressible payload: 1000 identical bytes always shrinks under
// gzip, so grpc_msg_compress() reports a real compression win (and thus sets
// the INTERNAL_COMPRESS flag).
std::string CompressiblePayload() { return std::string(1000, 'a'); }

ChannelArgs GzipArgs() {
  return ChannelArgs().Set(GRPC_COMPRESSION_CHANNEL_DEFAULT_ALGORITHM,
                           static_cast<int>(GRPC_COMPRESS_GZIP));
}
}  // namespace

FILTER_TEST_V3(RoundTripCompressesAndDecompresses) {
  ASSERT_TRUE(Add<ClientCompressionFilter>()
                  .Add<ServerCompressionFilter>()
                  .Build(GzipArgs())
                  .ok());
  const std::string request = CompressiblePayload();
  const std::string response = CompressiblePayload();

  auto [initiator, handler] = StartCall(Arena::MakePooledForOverwrite<ClientMetadata>());
  SpawnTestSeq(
      initiator, "client",
      [initiator = initiator, request]() mutable {
        return initiator.PushMessage(Arena::MakePooled<Message>(SliceBuffer(Slice::FromCopiedString(request)), 0));
      },
      [initiator = initiator](StatusFlag ok) mutable {
        EXPECT_TRUE(ok.ok());
        initiator.FinishSends();
        return initiator.PullServerInitialMetadata();
      },
      [initiator = initiator](
          ValueOrFailure<std::optional<ServerMetadataHandle>> md) mutable {
        EXPECT_TRUE(md.ok());
        EXPECT_TRUE(md.value().has_value());
        // Server filter advertised its encoding on server initial metadata.
        EXPECT_THAT(***md, HasMetadataKeyValue("grpc-encoding", "gzip"));
        return initiator.PullMessage();
      },
      [initiator = initiator, response](ServerToClientNextMessage msg) mutable {
        EXPECT_TRUE(msg.ok());
        EXPECT_TRUE(msg.has_value());
        // Client filter decompressed the server's message back to the original.
        EXPECT_THAT(msg.value(), HasMessagePayload(response));
        EXPECT_THAT(msg.value(),
                    HasMessageFlags(GRPC_WRITE_INTERNAL_TEST_ONLY_WAS_COMPRESSED));
        return initiator.PullMessage();
      },
      [initiator = initiator](ServerToClientNextMessage msg) mutable {
        EXPECT_TRUE(msg.ok());
        EXPECT_FALSE(msg.has_value());
        return initiator.PullServerTrailingMetadata();
      },
      [](ValueOrFailure<ServerMetadataHandle> md) {
        EXPECT_TRUE(md.ok());
        EXPECT_THAT(**md, HasMetadataResult(absl::OkStatus()));
      });

  SpawnTestSeq(
      handler, "server",
      [handler = handler]() mutable { return handler.PullClientInitialMetadata(); },
      [handler = handler](ValueOrFailure<ClientMetadataHandle> md) mutable {
        EXPECT_TRUE(md.ok());
        // Client filter advertised its encoding on client initial metadata.
        EXPECT_THAT(**md, HasMetadataKeyValue("grpc-encoding", "gzip"));
        return handler.PullMessage();
      },
      [handler = handler, request](ClientToServerNextMessage msg) mutable {
        EXPECT_TRUE(msg.ok());
        EXPECT_TRUE(msg.has_value());
        // Server filter decompressed the client's message back to the original.
        EXPECT_THAT(msg.value(), HasMessagePayload(request));
        EXPECT_THAT(msg.value(),
                    HasMessageFlags(GRPC_WRITE_INTERNAL_TEST_ONLY_WAS_COMPRESSED));
        return handler.PushServerInitialMetadata(Arena::MakePooledForOverwrite<ServerMetadata>());
      },
      [handler = handler, response](StatusFlag ok) mutable {
        EXPECT_TRUE(ok.ok());
        return handler.PushMessage(Arena::MakePooled<Message>(SliceBuffer(Slice::FromCopiedString(response)), 0));
      },
      [handler = handler](StatusFlag ok) mutable {
        EXPECT_TRUE(ok.ok());
        return handler.PullMessage();
      },
      [handler = handler](ClientToServerNextMessage msg) mutable {
        EXPECT_TRUE(msg.ok());
        EXPECT_FALSE(msg.has_value());  // client half-closed
        handler.PushServerTrailingMetadata(
            ServerMetadataFromStatus(GRPC_STATUS_OK));
      });

  WaitForAllPendingWork();
}

// With only the client filter in the stack there is nothing to decompress the
// client->server message, so the server end observes the message still
// compressed: fewer bytes than the original and the INTERNAL_COMPRESS flag set.
// This directly proves the client filter compressed the payload on the wire.
FILTER_TEST_V3(ClientFilterCompressesOnTheWire) {
  ASSERT_TRUE(Add<ClientCompressionFilter>().Build(GzipArgs()).ok());
  const std::string request = CompressiblePayload();

  auto [initiator, handler] = StartCall(Arena::MakePooledForOverwrite<ClientMetadata>());
  SpawnTestSeq(
      initiator, "client",
      [initiator = initiator, request]() mutable {
        return initiator.PushMessage(Arena::MakePooled<Message>(SliceBuffer(Slice::FromCopiedString(request)), 0));
      },
      [initiator = initiator](StatusFlag ok) mutable {
        EXPECT_TRUE(ok.ok());
        initiator.FinishSends();
        return initiator.PullServerInitialMetadata();
      },
      [initiator = initiator](
          ValueOrFailure<std::optional<ServerMetadataHandle>> md) mutable {
        EXPECT_TRUE(md.ok());
        EXPECT_TRUE(md.value().has_value());
        return initiator.PullMessage();
      },
      [initiator = initiator](ServerToClientNextMessage msg) mutable {
        EXPECT_TRUE(msg.ok());
        EXPECT_FALSE(msg.has_value());  // server sends no message
        return initiator.PullServerTrailingMetadata();
      },
      [](ValueOrFailure<ServerMetadataHandle> md) {
        EXPECT_TRUE(md.ok());
        EXPECT_THAT(**md, HasMetadataResult(absl::OkStatus()));
      });

  SpawnTestSeq(
      handler, "server",
      [handler = handler]() mutable { return handler.PullClientInitialMetadata(); },
      [handler = handler](ValueOrFailure<ClientMetadataHandle> md) mutable {
        EXPECT_TRUE(md.ok());
        EXPECT_THAT(**md, HasMetadataKeyValue("grpc-encoding", "gzip"));
        return handler.PullMessage();
      },
      [handler = handler, request](ClientToServerNextMessage msg) mutable {
        EXPECT_TRUE(msg.ok());
        EXPECT_TRUE(msg.has_value());
        // The payload arrives compressed: flagged and strictly smaller.
        EXPECT_THAT(msg.value(), HasMessageFlags(GRPC_WRITE_INTERNAL_COMPRESS));
        EXPECT_LT(msg.value().payload()->Length(), request.size());
        return handler.PushServerInitialMetadata(Arena::MakePooledForOverwrite<ServerMetadata>());
      },
      [handler = handler](StatusFlag ok) mutable {
        EXPECT_TRUE(ok.ok());
        return handler.PullMessage();
      },
      [handler = handler](ClientToServerNextMessage msg) mutable {
        EXPECT_TRUE(msg.ok());
        EXPECT_FALSE(msg.has_value());  // client half-closed
        handler.PushServerTrailingMetadata(
            ServerMetadataFromStatus(GRPC_STATUS_OK));
      });

  WaitForAllPendingWork();
}

// A message tagged GRPC_WRITE_NO_COMPRESS must bypass compression even though
// the channel default is gzip: the server end sees the original bytes with the
// NO_COMPRESS flag preserved and no INTERNAL_COMPRESS flag.
FILTER_TEST_V3(NoCompressFlagIsHonored) {
  ASSERT_TRUE(Add<ClientCompressionFilter>().Build(GzipArgs()).ok());
  const std::string request = CompressiblePayload();

  auto [initiator, handler] = StartCall(Arena::MakePooledForOverwrite<ClientMetadata>());
  SpawnTestSeq(
      initiator, "client",
      [initiator = initiator, request]() mutable {
        return initiator.PushMessage(Arena::MakePooled<Message>(SliceBuffer(Slice::FromCopiedString(request)), GRPC_WRITE_NO_COMPRESS));
      },
      [initiator = initiator](StatusFlag ok) mutable {
        EXPECT_TRUE(ok.ok());
        initiator.FinishSends();
        return initiator.PullServerInitialMetadata();
      },
      [initiator = initiator](
          ValueOrFailure<std::optional<ServerMetadataHandle>> md) mutable {
        EXPECT_TRUE(md.ok());
        EXPECT_TRUE(md.value().has_value());
        return initiator.PullMessage();
      },
      [initiator = initiator](ServerToClientNextMessage msg) mutable {
        EXPECT_TRUE(msg.ok());
        EXPECT_FALSE(msg.has_value());  // server sends no message
        return initiator.PullServerTrailingMetadata();
      },
      [](ValueOrFailure<ServerMetadataHandle> md) {
        EXPECT_TRUE(md.ok());
        EXPECT_THAT(**md, HasMetadataResult(absl::OkStatus()));
      });

  SpawnTestSeq(
      handler, "server",
      [handler = handler]() mutable { return handler.PullClientInitialMetadata(); },
      [handler = handler](ValueOrFailure<ClientMetadataHandle> md) mutable {
        EXPECT_TRUE(md.ok());
        return handler.PullMessage();
      },
      [handler = handler, request](ClientToServerNextMessage msg) mutable {
        EXPECT_TRUE(msg.ok());
        EXPECT_TRUE(msg.has_value());
        // Untouched: original bytes, NO_COMPRESS preserved, not compressed.
        EXPECT_THAT(msg.value(), HasMessagePayload(request));
        EXPECT_THAT(msg.value(), HasMessageFlags(GRPC_WRITE_NO_COMPRESS));
        return handler.PushServerInitialMetadata(Arena::MakePooledForOverwrite<ServerMetadata>());
      },
      [handler = handler](StatusFlag ok) mutable {
        EXPECT_TRUE(ok.ok());
        return handler.PullMessage();
      },
      [handler = handler](ClientToServerNextMessage msg) mutable {
        EXPECT_TRUE(msg.ok());
        EXPECT_FALSE(msg.has_value());  // client half-closed
        handler.PushServerTrailingMetadata(
            ServerMetadataFromStatus(GRPC_STATUS_OK));
      });

  WaitForAllPendingWork();
}

// Negative scenario: the server rejects the call with an error status (a
// trailers-only response). The compression filters carry no message here; the
// test asserts the failure propagates cleanly through the filter stack so the
// client observes the server's status. Robust under any fuzzed schedule -- there
// is no message to drain, mirroring the initial-metadata abort pattern.
FILTER_TEST_V3(ServerAbortsCallWithErrorStatus) {
  ASSERT_TRUE(Add<ClientCompressionFilter>()
                  .Add<ServerCompressionFilter>()
                  .Build(GzipArgs())
                  .ok());

  auto [initiator, handler] = StartCall(Arena::MakePooledForOverwrite<ClientMetadata>());
  SpawnTestSeq(
      initiator, "client",
      [initiator = initiator]() mutable { return initiator.PullServerTrailingMetadata(); },
      [](ValueOrFailure<ServerMetadataHandle> md) {
        EXPECT_TRUE(md.ok());
        EXPECT_THAT(**md,
                    HasMetadataResult(absl::UnavailableError("server aborted")));
      });

  SpawnTestSeq(
      handler, "server",
      [handler = handler]() mutable { return handler.PullClientInitialMetadata(); },
      [handler = handler](ValueOrFailure<ClientMetadataHandle> md) mutable {
        EXPECT_TRUE(md.ok());
        handler.PushServerTrailingMetadata(ServerMetadataFromStatus(
            GRPC_STATUS_UNAVAILABLE, "server aborted"));
      });

  WaitForAllPendingWork();
}

FILTER_TEST_V3(OversizeCompressedMessageRejected) {
  ASSERT_TRUE(
      Add<ClientCompressionFilter>()
          .Add<ServerCompressionFilter>()
          .Build(GzipArgs().Set(GRPC_ARG_MAX_RECEIVE_MESSAGE_LENGTH, 10))
          .ok());
  const std::string request = CompressiblePayload();

  auto [initiator, handler] = StartCall(Arena::MakePooledForOverwrite<ClientMetadata>());
  SpawnTestSeq(
      initiator, "client",
      [initiator = initiator, request]() mutable {
        return initiator.PushMessage(Arena::MakePooled<Message>(SliceBuffer(Slice::FromCopiedString(request)), 0));
      },
      [initiator = initiator](StatusFlag) mutable {
        // The push may resolve ok or fail depending on when the downstream
        // rejection lands; either way the call ends with the error below.
        initiator.FinishSends();
        return initiator.PullServerTrailingMetadata();
      },
      [](ValueOrFailure<ServerMetadataHandle> md) {
        EXPECT_TRUE(md.ok());
        auto code = (**md).get(GrpcStatusMetadata());
        ASSERT_TRUE(code.has_value());
        EXPECT_EQ(*code, GRPC_STATUS_RESOURCE_EXHAUSTED);
      });

  SpawnTestSeq(
      handler, "server",
      [handler = handler]() mutable { return handler.PullClientInitialMetadata(); },
      [handler = handler](ValueOrFailure<ClientMetadataHandle> md) mutable {
        EXPECT_TRUE(md.ok());
        return handler.PullMessage();
      },
      [](ClientToServerNextMessage msg) {
        // The server filter rejected the oversized message: the pull fails
        // rather than delivering a message.
        EXPECT_FALSE(msg.ok());
      });

  WaitForAllPendingWork();
}

}  // namespace grpc_core
