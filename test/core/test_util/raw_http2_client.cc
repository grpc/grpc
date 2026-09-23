//
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
//

#include "test/core/test_util/raw_http2_client.h"

#include <grpc/event_engine/event_engine.h>
#include <grpc/event_engine/slice.h>
#include <grpc/event_engine/slice_buffer.h>
#include <grpc/status.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "src/core/call/metadata_batch.h"
#include "src/core/ext/transport/chttp2/transport/frame.h"
#include "src/core/ext/transport/chttp2/transport/hpack_parser.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/event_engine/channel_args_endpoint_config.h"
#include "src/core/lib/event_engine/tcp_socket_utils.h"
#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/lib/resource_quota/resource_quota.h"
#include "src/core/lib/slice/slice.h"
#include "src/core/util/grpc_check.h"
#include "src/core/util/notification.h"
#include "src/core/util/ref_counted_ptr.h"
#include "test/core/test_util/resolve_localhost_ip46.h"
#include "test/core/test_util/test_config.h"
#include "absl/random/bit_gen_ref.h"
#include "absl/random/random.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "absl/time/time.h"

namespace grpc_core {
namespace testing {
namespace {

using ::grpc_event_engine::experimental::EventEngine;

constexpr int kTimeoutSeconds = 10;

// HTTP/2 frame types.
constexpr uint8_t kData = 0x0;
constexpr uint8_t kHeaders = 0x1;
constexpr uint8_t kRstStream = 0x3;
constexpr uint8_t kSettings = 0x4;
constexpr uint8_t kPing = 0x6;
constexpr uint8_t kGoaway = 0x7;
constexpr uint8_t kContinuation = 0x9;

// HTTP/2 frame flags.
constexpr uint8_t kEndStream = 0x1;
constexpr uint8_t kAck = 0x1;
constexpr uint8_t kEndHeaders = 0x4;
constexpr uint8_t kPadded = 0x8;
constexpr uint8_t kPriority = 0x20;

absl::Duration Timeout() {
  return absl::Seconds(kTimeoutSeconds * grpc_test_slowdown_factor());
}

absl::Status ClientError(absl::string_view message) {
  return absl::InternalError(absl::StrCat("RawHttp2Client: ", message));
}

void AppendUint32(uint32_t value, std::string* out) {
  out->push_back(static_cast<char>((value >> 24) & 0xff));
  out->push_back(static_cast<char>((value >> 16) & 0xff));
  out->push_back(static_cast<char>((value >> 8) & 0xff));
  out->push_back(static_cast<char>(value & 0xff));
}

// Encodes a "literal header field without indexing - new name" entry with
// no Huffman coding. Keys and values must be shorter than 127 bytes.
void AppendHpackLiteral(absl::string_view key, absl::string_view value,
                        std::string* out) {
  GRPC_CHECK_LT(key.size(), 127u);
  GRPC_CHECK_LT(value.size(), 127u);
  out->push_back('\0');
  out->push_back(static_cast<char>(key.size()));
  out->append(key);
  out->push_back(static_cast<char>(value.size()));
  out->append(value);
}

std::string Frame(uint8_t type, uint8_t flags, uint32_t stream_id,
                  absl::string_view payload) {
  std::string frame(9, '\0');
  Http2FrameHeader{static_cast<uint32_t>(payload.size()), type, flags,
                   stream_id}
      .Serialize(reinterpret_cast<uint8_t*>(frame.data()));
  frame.append(payload);
  return frame;
}

void ParseHeaderBlock(absl::string_view block, uint32_t stream_id,
                      bool end_stream, HPackParser* parser,
                      grpc_metadata_batch* metadata) {
  ExecCtx exec_ctx;
  parser->BeginFrame(
      metadata, /*metadata_size_soft_limit=*/65536,
      /*metadata_size_hard_limit=*/65536,
      end_stream ? HPackParser::Boundary::EndOfStream
                 : HPackParser::Boundary::EndOfHeaders,
      HPackParser::Priority::None,
      HPackParser::LogInfo{stream_id, HPackParser::LogInfo::kDontKnow,
                           /*is_client=*/true});
  Slice slice = Slice::FromCopiedString(block);
  absl::BitGen bitgen;
  GRPC_CHECK_OK(parser->Parse(slice.c_slice(), /*is_last=*/true,
                              absl::BitGenRef(bitgen),
                              /*call_tracer=*/nullptr));
  parser->FinishFrame();
}

absl::Status StatusFromTrailers(const grpc_metadata_batch& trailers) {
  std::optional<grpc_status_code> code = trailers.get(GrpcStatusMetadata());
  if (!code.has_value()) return ClientError("trailers have no grpc-status");
  const Slice* message = trailers.get_pointer(GrpcMessageMetadata());
  return absl::Status(static_cast<absl::StatusCode>(*code),
                      message == nullptr ? "" : message->as_string_view());
}

// State for one endpoint Read or Write. It is shared with the callback so
// that it stays alive if we stop waiting because of a timeout.
struct IoState {
  grpc_event_engine::experimental::SliceBuffer buffer;
  absl::Status status;
  Notification done;
};

}  // namespace

RawHttp2Client::RawHttp2Client(int port)
    : event_engine_(grpc_event_engine::experimental::GetDefaultEventEngine()) {
  auto addr =
      grpc_event_engine::experimental::URIToResolvedAddress(LocalIpUri(port));
  GRPC_CHECK_OK(addr);
  // The posix endpoint requires a resource quota in the endpoint config.
  RefCountedPtr<ResourceQuota> resource_quota = ResourceQuota::Default();
  Notification connected;
  absl::StatusOr<std::unique_ptr<EventEngine::Endpoint>> endpoint;
  event_engine_->Connect(
      [&](absl::StatusOr<std::unique_ptr<EventEngine::Endpoint>> ep) {
        endpoint = std::move(ep);
        connected.Notify();
      },
      *addr,
      grpc_event_engine::experimental::ChannelArgsEndpointConfig(
          ChannelArgs().Set(GRPC_ARG_RESOURCE_QUOTA, resource_quota)),
      resource_quota->memory_quota()->CreateMemoryAllocator(
          "raw_http2_client"),
      absl::ToChronoMilliseconds(Timeout()));
  connected.WaitForNotification();
  GRPC_CHECK_OK(endpoint);
  endpoint_ = std::move(*endpoint);
}

absl::Status RawHttp2Client::SendUnaryRequest(
    absl::string_view path, const std::vector<Header>& headers,
    absl::string_view serialized_request) {
  GRPC_CHECK(!std::exchange(request_sent_, true));
  std::string header_block;
  AppendHpackLiteral(":method", "POST", &header_block);
  AppendHpackLiteral(":scheme", "http", &header_block);
  AppendHpackLiteral(":path", path, &header_block);
  for (const Header& header : headers) {
    AppendHpackLiteral(header.key, header.value, &header_block);
  }
  AppendHpackLiteral("content-type", "application/grpc", &header_block);
  AppendHpackLiteral("te", "trailers", &header_block);
  // gRPC length-prefixed message: 1-byte compressed flag, 4-byte length.
  std::string grpc_message(1, '\0');
  AppendUint32(serialized_request.size(), &grpc_message);
  grpc_message.append(serialized_request);
  std::string out = "PRI * HTTP/2.0\r\n\r\nSM\r\n\r\n";
  out += Frame(kSettings, 0, 0, "");
  out += Frame(kHeaders, kEndHeaders, 1, header_block);
  out += Frame(kData, kEndStream, 1, grpc_message);
  if (!Write(out)) return ClientError("write failed");
  // Server HEADERS frames must all go through the same HPACK parser so
  // that its dynamic table stays in sync with the server's encoder.
  HPackParser hpack_parser;
  // Header block fragments of a HEADERS frame and its CONTINUATIONs.
  std::string pending_block;
  uint8_t pending_flags = 0;
  uint32_t pending_stream_id = 0;
  while (true) {
    std::string frame_header;
    if (!ReadExact(9, &frame_header)) {
      return ClientError("connection closed or timed out");
    }
    const Http2FrameHeader header = Http2FrameHeader::Parse(
        reinterpret_cast<const uint8_t*>(frame_header.data()));
    std::string payload;
    if (!ReadExact(header.length, &payload)) {
      return ClientError("connection closed or timed out");
    }
    if (header.type == kSettings && (header.flags & kAck) == 0) {
      if (!Write(Frame(kSettings, kAck, 0, ""))) {
        return ClientError("write failed");
      }
    } else if (header.type == kPing && (header.flags & kAck) == 0) {
      if (!Write(Frame(kPing, kAck, 0, payload))) {
        return ClientError("write failed");
      }
    } else if (header.type == kGoaway) {
      return ClientError("got GOAWAY");
    } else if (header.stream_id == 1 && header.type == kRstStream) {
      return ClientError("got RST_STREAM");
    } else if (header.type == kHeaders || header.type == kContinuation) {
      if (header.type == kHeaders) {
        // The gRPC server does not send padding or priority.
        GRPC_CHECK_EQ(header.flags & (kPadded | kPriority), 0);
        pending_block.clear();
        pending_flags = header.flags;
        pending_stream_id = header.stream_id;
      }
      pending_block += payload;
      if ((header.flags & kEndHeaders) == 0) continue;
      grpc_metadata_batch metadata;
      ParseHeaderBlock(pending_block, pending_stream_id,
                       (pending_flags & kEndStream) != 0, &hpack_parser,
                       &metadata);
      if (pending_stream_id == 1 && (pending_flags & kEndStream) != 0) {
        return StatusFromTrailers(metadata);
      }
    }
  }
}

bool RawHttp2Client::Write(absl::string_view data) {
  auto state = std::make_shared<IoState>();
  state->buffer.Append(
      grpc_event_engine::experimental::Slice::FromCopiedString(data));
  if (endpoint_->Write(
          [state](absl::Status status) {
            state->status = std::move(status);
            state->done.Notify();
          },
          &state->buffer, EventEngine::Endpoint::WriteArgs())) {
    return true;
  }
  return state->done.WaitForNotificationWithTimeout(Timeout()) &&
         state->status.ok();
}

bool RawHttp2Client::ReadExact(size_t n, std::string* out) {
  while (read_buffer_.size() < n) {
    auto state = std::make_shared<IoState>();
    if (!endpoint_->Read(
            [state](absl::Status status) {
              state->status = std::move(status);
              state->done.Notify();
            },
            &state->buffer, EventEngine::Endpoint::ReadArgs())) {
      if (!state->done.WaitForNotificationWithTimeout(Timeout()) ||
          !state->status.ok()) {
        return false;
      }
    }
    const size_t length = state->buffer.Length();
    if (length == 0) return false;
    std::string chunk(length, '\0');
    state->buffer.MoveFirstNBytesIntoBuffer(length, chunk.data());
    read_buffer_ += chunk;
  }
  *out = read_buffer_.substr(0, n);
  read_buffer_.erase(0, n);
  return true;
}

}  // namespace testing
}  // namespace grpc_core
