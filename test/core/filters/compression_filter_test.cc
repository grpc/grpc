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

// Each test here puts exactly one of the two compression filters in the stack
// and observes what the *other* end of the call sees on the wire. That is what
// makes the assertions meaningful: with only the client filter in the stack
// there is nothing to decompress the client->server message, so the server end
// observes the bytes exactly as the client filter emitted them.

#include "src/core/ext/filters/http/message_compress/compression_filter.h"

#include <grpc/impl/channel_arg_names.h>
#include <grpc/impl/compression_types.h>
#include <grpc/impl/grpc_types.h>
#include <grpc/status.h>

#include <string>

#include "src/core/call/metadata.h"
#include "src/core/lib/channel/channel_args.h"
#include "test/core/filters/filter_matchers.h"
#include "test/core/filters/filter_test.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/status/status.h"

namespace grpc_core {

namespace {
// A highly compressible payload: 1000 identical bytes always shrinks under
// gzip, so grpc_msg_compress() reports a real compression win (and thus sets
// the INTERNAL_COMPRESS flag).
std::string CompressiblePayload() { return std::string(1000, 'a'); }
}  // namespace

class CompressionFilterTest : public FilterTest {
 protected:
  using FilterTest::FilterTest;

  static ChannelArgs GzipArgs() {
    return ChannelArgs().Set(GRPC_COMPRESSION_CHANNEL_DEFAULT_ALGORITHM,
                             static_cast<int>(GRPC_COMPRESS_GZIP));
  }
};

// The client filter compresses client->server messages: the payload arrives at
// the server end flagged and strictly smaller than what was sent.
FILTER_TEST(CompressionFilterTest, ClientFilterCompressesOnTheWire) {
  ASSERT_TRUE(InitChannel<ClientCompressionFilter>(GzipArgs()).ok());
  const std::string request = CompressiblePayload();
  auto [initiator, handler] = StartCallForFilter(NewClientMetadata());

  PushClientMessage(NewMessage(request));
  PushClientHalfClose();

  ValueOrFailure<ClientMetadataHandle> client_initial_metadata =
      PullClientInitialMetadata(handler);
  ASSERT_TRUE(client_initial_metadata.ok());
  // The filter advertised its encoding on client initial metadata.
  EXPECT_THAT(**client_initial_metadata,
              HasMetadataKeyValue("grpc-encoding", "gzip"));

  ClientToServerNextMessage message = PullClientMessage(handler);
  ASSERT_TRUE(message.ok());
  ASSERT_TRUE(message.has_value());
  EXPECT_THAT(message.value(), HasMessageFlags(GRPC_WRITE_INTERNAL_COMPRESS));
  EXPECT_LT(message.value().payload()->Length(), request.size());
  EXPECT_TRUE(PullClientHalfClose(handler));

  PushServerTrailingMetadata(handler, ServerMetadataFromStatus(GRPC_STATUS_OK));
  EXPECT_TRUE(PullServerTrailingMetadata().ok());
  WaitForAllPendingWork();
}

// A message tagged GRPC_WRITE_NO_COMPRESS must bypass compression even though
// the channel default is gzip: the server end sees the original bytes with the
// NO_COMPRESS flag preserved and no INTERNAL_COMPRESS flag.
FILTER_TEST(CompressionFilterTest, ClientFilterHonorsNoCompressFlag) {
  ASSERT_TRUE(InitChannel<ClientCompressionFilter>(GzipArgs()).ok());
  const std::string request = CompressiblePayload();
  auto [initiator, handler] = StartCallForFilter(NewClientMetadata());

  PushClientMessage(NewMessage(request, GRPC_WRITE_NO_COMPRESS));
  PushClientHalfClose();

  ASSERT_TRUE(PullClientInitialMetadata(handler).ok());
  ClientToServerNextMessage message = PullClientMessage(handler);
  ASSERT_TRUE(message.ok());
  ASSERT_TRUE(message.has_value());
  EXPECT_THAT(message.value(), HasMessagePayload(request));
  EXPECT_THAT(message.value(), HasMessageFlags(GRPC_WRITE_NO_COMPRESS));
  EXPECT_TRUE(PullClientHalfClose(handler));

  PushServerTrailingMetadata(handler, ServerMetadataFromStatus(GRPC_STATUS_OK));
  EXPECT_TRUE(PullServerTrailingMetadata().ok());
  WaitForAllPendingWork();
}

// The server filter compresses server->client messages when the client
// advertises that it accepts the channel's default encoding.
FILTER_TEST(CompressionFilterTest, ServerFilterCompressesResponse) {
  ASSERT_TRUE(InitChannel<ServerCompressionFilter>(GzipArgs()).ok());
  const std::string response = CompressiblePayload();
  auto [initiator, handler] = StartCallForFilter(
      NewClientMetadata({{"grpc-accept-encoding", "identity,gzip"}}));

  ASSERT_TRUE(PullClientInitialMetadata(handler).ok());
  PushServerInitialMetadata(handler, NewServerMetadata());
  PushServerMessage(handler, NewMessage(response));

  ValueOrFailure<std::optional<ServerMetadataHandle>> server_initial_metadata =
      PullServerInitialMetadata();
  ASSERT_TRUE(server_initial_metadata.ok());
  ASSERT_TRUE(server_initial_metadata->has_value());
  EXPECT_THAT(***server_initial_metadata,
              HasMetadataKeyValue("grpc-encoding", "gzip"));

  ServerToClientNextMessage message = PullServerMessage();
  ASSERT_TRUE(message.ok());
  ASSERT_TRUE(message.has_value());
  EXPECT_THAT(message.value(), HasMessageFlags(GRPC_WRITE_INTERNAL_COMPRESS));
  EXPECT_LT(message.value().payload()->Length(), response.size());

  PushServerTrailingMetadata(handler, ServerMetadataFromStatus(GRPC_STATUS_OK));
  EXPECT_TRUE(PullServerTrailingMetadata().ok());
  WaitForAllPendingWork();
}

// The server filter enforces the receive message size limit on the
// (post-decompression) payload.
FILTER_TEST(CompressionFilterTest, ServerFilterRejectsOversizeMessage) {
  ASSERT_TRUE(InitChannel<ServerCompressionFilter>(
                  GzipArgs().Set(GRPC_ARG_MAX_RECEIVE_MESSAGE_LENGTH, 10))
                  .ok());
  auto [initiator, handler] = StartCallForFilter(NewClientMetadata());

  PushClientMessage(NewMessage(CompressiblePayload()));
  PushClientHalfClose();

  ASSERT_TRUE(PullClientInitialMetadata(handler).ok());
  // The filter rejected the oversized message: the pull fails rather than
  // delivering a message.
  EXPECT_FALSE(PullClientMessage(handler).ok());

  ValueOrFailure<ServerMetadataHandle> server_trailing_metadata =
      PullServerTrailingMetadata();
  ASSERT_TRUE(server_trailing_metadata.ok());
  std::optional<grpc_status_code> code =
      (**server_trailing_metadata).get(GrpcStatusMetadata());
  ASSERT_TRUE(code.has_value());
  EXPECT_EQ(*code, GRPC_STATUS_RESOURCE_EXHAUSTED);

  WaitForAllPendingWork();
}

// Negative scenario: the server rejects the call with a trailers-only error
// response. The filter carries no message here; the test asserts the failure
// propagates cleanly through it so the client observes the server's status.
FILTER_TEST(CompressionFilterTest, ServerAbortsCallWithErrorStatus) {
  ASSERT_TRUE(InitChannel<ClientCompressionFilter>(GzipArgs()).ok());
  auto [initiator, handler] = StartCallForFilter(NewClientMetadata());

  ASSERT_TRUE(PullClientInitialMetadata(handler).ok());
  PushServerTrailingMetadata(
      handler,
      ServerMetadataFromStatus(GRPC_STATUS_UNAVAILABLE, "server aborted"));

  ValueOrFailure<ServerMetadataHandle> server_trailing_metadata =
      PullServerTrailingMetadata();
  ASSERT_TRUE(server_trailing_metadata.ok());
  EXPECT_THAT(**server_trailing_metadata,
              HasMetadataResult(absl::UnavailableError("server aborted")));

  WaitForAllPendingWork();
}

}  // namespace grpc_core
