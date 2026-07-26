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
#include "src/core/lib/compression/message_compress.h"
#include "src/core/lib/resource_quota/arena.h"
#include "src/core/lib/slice/slice.h"
#include "src/core/lib/slice/slice_buffer.h"
#include "src/core/service_config/service_config.h"
#include "src/core/service_config/service_config_call_data.h"
#include "src/core/service_config/service_config_impl.h"
#include "src/core/util/grpc_check.h"
#include "src/core/util/ref_counted_ptr.h"
#include "test/core/filters/filter_matchers.h"
#include "test/core/filters/filter_test.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"

namespace grpc_core {

namespace {
const absl::string_view kTestPath = "/test_method";

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

  // A gzip-compressed message carrying `payload`, flagged the way a peer would
  // flag it on the wire, so the filter under test decompresses it.
  MessageHandle GzipCompressedMessage(absl::string_view payload) {
    SliceBuffer input;
    input.Append(Slice::FromCopiedString(payload));
    SliceBuffer output;
    GRPC_CHECK(grpc_msg_compress(GRPC_COMPRESS_GZIP, input.c_slice_buffer(),
                                 output.c_slice_buffer()))
        << "payload did not compress; pick a more compressible one";
    return Arena::MakePooled<Message>(std::move(output),
                                      GRPC_WRITE_INTERNAL_COMPRESS);
  }

  // Make every call carry a service config with the given method config fields,
  // so we exercise the service-config path rather than channel args.
  void SetMessageSizeServiceConfig(absl::string_view fields) {
    absl::StatusOr<RefCountedPtr<ServiceConfig>> service_config =
        ServiceConfigImpl::Create(
            ChannelArgs(),
            absl::StrCat(R"({"methodConfig":[{"name":[{}],)", fields, "}]}"));
    ASSERT_TRUE(service_config.ok()) << service_config.status();
    service_config_ = std::move(*service_config);
    method_configs_ = service_config_->GetMethodParsedConfigVector(
        Slice::FromCopiedString(kTestPath).c_slice());
  }

  ClientMetadataHandle MakeClientInitialMetadata() {
    ClientMetadataHandle md = NewClientMetadata();
    md->Set(HttpPathMetadata(), Slice::FromCopiedString(kTestPath));
    return md;
  }

  void InitAfterCallArena(Arena* arena) override {
    if (service_config_ == nullptr) return;
    arena->New<ServiceConfigCallData>(arena)->SetServiceConfig(service_config_,
                                                               method_configs_);
  }

 private:
  RefCountedPtr<ServiceConfig> service_config_;
  const ServiceConfigParser::ParsedConfigVector* method_configs_ = nullptr;
};

// The client filter compresses client->server messages: the payload arrives at
// the server end flagged and strictly smaller than what was sent.
FILTER_TEST(CompressionFilterTest, ClientFilterCompressesRequest) {
  ASSERT_TRUE(CreateFilterChain<ClientCompressionFilter>(GzipArgs()).ok());
  const std::string request = CompressiblePayload();
  StartCallForFilter(NewClientMetadata());

  PushClientMessage(NewMessage(request));
  PushClientHalfClose();

  ValueOrFailure<ClientMetadataHandle> client_initial_metadata =
      PullClientInitialMetadata();
  ASSERT_TRUE(client_initial_metadata.ok());
  // The filter advertised its encoding on client initial metadata.
  EXPECT_THAT(**client_initial_metadata,
              HasMetadataKeyValue("grpc-encoding", "gzip"));

  ClientToServerNextMessage message = PullClientMessage();
  ASSERT_TRUE(message.ok());
  ASSERT_TRUE(message.has_value());
  EXPECT_THAT(message.value(), HasMessageFlags(GRPC_WRITE_INTERNAL_COMPRESS));
  EXPECT_LT(message.value().payload()->Length(), request.size());
  EXPECT_TRUE(PullClientHalfClose());

  PushServerTrailingMetadata(ServerMetadataFromStatus(GRPC_STATUS_OK));
  EXPECT_TRUE(PullServerTrailingStatus().ok());
  WaitForAllPendingWork();
}

// The client filter decompresses server->client messages: a compressed response
// tagged gzip arrives at the client end restored to its original bytes.
FILTER_TEST(CompressionFilterTest, ClientFilterDecompressesResponse) {
  ASSERT_TRUE(CreateFilterChain<ClientCompressionFilter>(GzipArgs()).ok());
  const std::string response = CompressiblePayload();
  StartCallForFilter(NewClientMetadata());

  PushClientHalfClose();
  ASSERT_TRUE(PullClientInitialMetadata().ok());
  PushServerInitialMetadata(NewServerMetadata({{"grpc-encoding", "gzip"}}));
  PushServerMessage(GzipCompressedMessage(response));

  ASSERT_TRUE(PullServerInitialMetadata().ok());
  ServerToClientNextMessage message = PullServerMessage();
  ASSERT_TRUE(message.ok());
  ASSERT_TRUE(message.has_value());
  EXPECT_THAT(message.value(), HasMessagePayload(response));

  PushServerTrailingMetadata(ServerMetadataFromStatus(GRPC_STATUS_OK));
  EXPECT_TRUE(PullServerTrailingStatus().ok());
  WaitForAllPendingWork();
}

// A message tagged GRPC_WRITE_NO_COMPRESS must bypass compression even though
// the channel default is gzip: the server end sees the original bytes with the
// NO_COMPRESS flag preserved and no INTERNAL_COMPRESS flag.
FILTER_TEST(CompressionFilterTest, ClientFilterHonorsNoCompressFlag) {
  ASSERT_TRUE(CreateFilterChain<ClientCompressionFilter>(GzipArgs()).ok());
  const std::string request = CompressiblePayload();
  StartCallForFilter(NewClientMetadata());

  PushClientMessage(NewMessage(request, GRPC_WRITE_NO_COMPRESS));
  PushClientHalfClose();

  ASSERT_TRUE(PullClientInitialMetadata().ok());
  ClientToServerNextMessage message = PullClientMessage();
  ASSERT_TRUE(message.ok());
  ASSERT_TRUE(message.has_value());
  EXPECT_THAT(message.value(), HasMessagePayload(request));
  EXPECT_THAT(message.value(), HasMessageFlags(GRPC_WRITE_NO_COMPRESS));
  EXPECT_TRUE(PullClientHalfClose());

  PushServerTrailingMetadata(ServerMetadataFromStatus(GRPC_STATUS_OK));
  EXPECT_TRUE(PullServerTrailingStatus().ok());
  WaitForAllPendingWork();
}

// The server filter compresses server->client messages when the client
// advertises that it accepts the channel's default encoding.
FILTER_TEST(CompressionFilterTest, ServerFilterCompressesResponse) {
  ASSERT_TRUE(CreateFilterChain<ServerCompressionFilter>(GzipArgs()).ok());
  const std::string response = CompressiblePayload();
  StartCallForFilter(
      NewClientMetadata({{"grpc-accept-encoding", "identity,gzip"}}));

  ASSERT_TRUE(PullClientInitialMetadata().ok());
  PushServerInitialMetadata(NewServerMetadata());
  PushServerMessage(NewMessage(response));

  ValueOrFailure<std::optional<ServerMetadataHandle>> server_initial_metadata =
      PullServerInitialMetadata();
  ASSERT_TRUE(server_initial_metadata.ok());
  EXPECT_THAT(*server_initial_metadata,
              ::testing::Optional(::testing::Pointee(
                  HasMetadataKeyValue("grpc-encoding", "gzip"))));

  ServerToClientNextMessage message = PullServerMessage();
  ASSERT_TRUE(message.ok());
  ASSERT_TRUE(message.has_value());
  EXPECT_THAT(message.value(), HasMessageFlags(GRPC_WRITE_INTERNAL_COMPRESS));
  EXPECT_LT(message.value().payload()->Length(), response.size());

  PushServerTrailingMetadata(ServerMetadataFromStatus(GRPC_STATUS_OK));
  EXPECT_TRUE(PullServerTrailingStatus().ok());
  WaitForAllPendingWork();
}

// The server filter decompresses client->server messages: a compressed request
// tagged gzip arrives at the server end restored to its original bytes.
FILTER_TEST(CompressionFilterTest, ServerFilterDecompressesRequest) {
  ASSERT_TRUE(CreateFilterChain<ServerCompressionFilter>(GzipArgs()).ok());
  const std::string request = CompressiblePayload();
  StartCallForFilter(NewClientMetadata(
      {{"grpc-encoding", "gzip"}, {"grpc-accept-encoding", "identity,gzip"}}));

  PushClientMessage(GzipCompressedMessage(request));
  PushClientHalfClose();

  ASSERT_TRUE(PullClientInitialMetadata().ok());
  ClientToServerNextMessage message = PullClientMessage();
  ASSERT_TRUE(message.ok());
  ASSERT_TRUE(message.has_value());
  EXPECT_THAT(message.value(), HasMessagePayload(request));
  EXPECT_TRUE(PullClientHalfClose());

  PushServerTrailingMetadata(ServerMetadataFromStatus(GRPC_STATUS_OK));
  EXPECT_TRUE(PullServerTrailingStatus().ok());
  WaitForAllPendingWork();
}

// As above, but server->client: NO_COMPRESS bypasses compression even though
// the client accepts the channel's gzip default.
FILTER_TEST(CompressionFilterTest, ServerFilterHonorsNoCompressFlag) {
  ASSERT_TRUE(CreateFilterChain<ServerCompressionFilter>(GzipArgs()).ok());
  const std::string response = CompressiblePayload();
  StartCallForFilter(
      NewClientMetadata({{"grpc-accept-encoding", "identity,gzip"}}));

  ASSERT_TRUE(PullClientInitialMetadata().ok());
  PushServerInitialMetadata(NewServerMetadata());
  PushServerMessage(NewMessage(response, GRPC_WRITE_NO_COMPRESS));

  ASSERT_TRUE(PullServerInitialMetadata().ok());
  ServerToClientNextMessage message = PullServerMessage();
  ASSERT_TRUE(message.ok());
  ASSERT_TRUE(message.has_value());
  EXPECT_THAT(message.value(), HasMessagePayload(response));
  EXPECT_THAT(message.value(), HasMessageFlags(GRPC_WRITE_NO_COMPRESS));

  PushServerTrailingMetadata(ServerMetadataFromStatus(GRPC_STATUS_OK));
  EXPECT_TRUE(PullServerTrailingStatus().ok());
  WaitForAllPendingWork();
}

// The server filter enforces the receive message size limit on the
// (post-decompression) payload.
FILTER_TEST(CompressionFilterTest, ServerFilterRejectsOversizeMessage) {
  ASSERT_TRUE(CreateFilterChain<ServerCompressionFilter>(
                  GzipArgs().Set(GRPC_ARG_MAX_RECEIVE_MESSAGE_LENGTH, 10))
                  .ok());
  StartCallForFilter(NewClientMetadata());

  PushClientMessage(NewMessage(CompressiblePayload()));
  PushClientHalfClose();

  ASSERT_TRUE(PullClientInitialMetadata().ok());
  // The filter rejected the message, so the pull fails rather than delivering.
  EXPECT_FALSE(PullClientMessage().ok());

  EXPECT_EQ(PullServerTrailingStatus().code(),
            absl::StatusCode::kResourceExhausted);

  WaitForAllPendingWork();
}

// The client filter enforces the receive message size limit set via the service
// config (maxResponseMessageBytes) on the post-decompression payload.
FILTER_TEST(CompressionFilterTest,
            ClientFilterRejectsOversizeResponseFromServiceConfig) {
  ASSERT_TRUE(CreateFilterChain<ClientCompressionFilter>(GzipArgs()).ok());
  SetMessageSizeServiceConfig(R"("maxResponseMessageBytes": 10)");
  StartCallForFilter(MakeClientInitialMetadata());

  PushClientHalfClose();
  ASSERT_TRUE(PullClientInitialMetadata().ok());
  PushServerInitialMetadata(NewServerMetadata({{"grpc-encoding", "gzip"}}));
  PushServerMessage(GzipCompressedMessage(CompressiblePayload()));

  ASSERT_TRUE(PullServerInitialMetadata().ok());
  // The decompressed payload (1000 bytes) exceeds the 10-byte limit.
  EXPECT_FALSE(PullServerMessage().ok());

  EXPECT_EQ(PullServerTrailingStatus().code(),
            absl::StatusCode::kResourceExhausted);

  WaitForAllPendingWork();
}

// Negative scenario: the server rejects the call with a trailers-only error
// response. The filter carries no message here; the test asserts the failure
// propagates cleanly through it so the client observes the server's status.
FILTER_TEST(CompressionFilterTest, ServerAbortsCallWithErrorStatus) {
  ASSERT_TRUE(CreateFilterChain<ClientCompressionFilter>(GzipArgs()).ok());
  StartCallForFilter(NewClientMetadata());

  ASSERT_TRUE(PullClientInitialMetadata().ok());
  PushServerTrailingMetadata(
      ServerMetadataFromStatus(GRPC_STATUS_UNAVAILABLE, "server aborted"));

  EXPECT_EQ(PullServerTrailingStatus(),
            absl::UnavailableError("server aborted"));

  WaitForAllPendingWork();
}

}  // namespace grpc_core
