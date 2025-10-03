//
//
// Copyright 2025 gRPC authors.
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
//

#ifndef GRPC_SRC_CORE_EXT_TRANSPORT_CHTTP2_TRANSPORT_STREAM_H
#define GRPC_SRC_CORE_EXT_TRANSPORT_CHTTP2_TRANSPORT_STREAM_H

#include <cstdint>

#include "src/core/call/call_spine.h"
#include "src/core/ext/transport/chttp2/transport/flow_control.h"
#include "src/core/ext/transport/chttp2/transport/stream_data_queue.h"
#include "src/core/util/ref_counted_ptr.h"

namespace grpc_core {
namespace http2 {

#define GRPC_HTTP2_STREAM_LOG VLOG(2)

// TODO(akshitpatel) : [PH2][P4] : Choose appropriate size later.
constexpr uint32_t kStreamQueueSize = /*1 MB*/ 1024u * 1024u;

enum class HttpStreamState : uint8_t {
  // https://www.rfc-editor.org/rfc/rfc9113.html#name-stream-states
  kIdle,
  kOpen,
  kHalfClosedLocal,
  kHalfClosedRemote,
  kClosed,
};

// Managing the streams
struct Stream : public RefCounted<Stream> {
  explicit Stream(CallHandler call, const uint32_t stream_id1,
                  bool allow_true_binary_metadata_peer,
                  bool allow_true_binary_metadata_acked,
                  chttp2::TransportFlowControl& transport_flow_control)
      : call(std::move(call)),
        is_write_closed(false),
        stream_state(HttpStreamState::kIdle),
        stream_id(stream_id1),
        header_assembler(stream_id1, allow_true_binary_metadata_acked),
        did_push_initial_metadata(false),
        did_push_trailing_metadata(false),
        data_queue(MakeRefCounted<StreamDataQueue<ClientMetadataHandle>>(
            /*is_client*/ true, /*stream_id*/ stream_id1,
            /*queue_size*/ kStreamQueueSize, allow_true_binary_metadata_peer)),
        flow_control(&transport_flow_control) {}

  ////////////////////////////////////////////////////////////////////////////
  // Data Queue Helpers
  // All enqueue methods are called from the call party.

  auto EnqueueInitialMetadata(ClientMetadataHandle&& metadata) {
    GRPC_HTTP2_STREAM_LOG
        << "Http2ClientTransport::Stream::EnqueueInitialMetadata stream_id="
        << stream_id;
    return data_queue->EnqueueInitialMetadata(std::move(metadata));
  }

  auto EnqueueTrailingMetadata(ClientMetadataHandle&& metadata) {
    GRPC_HTTP2_STREAM_LOG
        << "Http2ClientTransport::Stream::EnqueueTrailingMetadata stream_id="
        << stream_id;
    return data_queue->EnqueueTrailingMetadata(std::move(metadata));
  }

  auto EnqueueMessage(MessageHandle&& message) {
    GRPC_HTTP2_STREAM_LOG
        << "Http2ClientTransport::Stream::EnqueueMessage stream_id="
        << stream_id << " with payload size = " << message->payload()->Length();
    return data_queue->EnqueueMessage(std::move(message));
  }

  auto EnqueueHalfClosed() {
    GRPC_HTTP2_STREAM_LOG
        << "Http2ClientTransport::Stream::EnqueueHalfClosed stream_id="
        << stream_id;
    return data_queue->EnqueueHalfClosed();
  }

  auto EnqueueResetStream(const uint32_t error_code) {
    GRPC_HTTP2_STREAM_LOG
        << "Http2ClientTransport::Stream::EnqueueResetStream stream_id="
        << stream_id << " with error_code = " << error_code;
    return data_queue->EnqueueResetStream(error_code);
  }

  // Called from the transport party
  auto DequeueFrames(const uint32_t transport_tokens,
                     const uint32_t max_frame_length,
                     HPackCompressor& encoder) {
    HttpStreamState state = GetStreamState();
    // Reset stream MUST not be sent if the stream is idle or closed.
    return data_queue->DequeueFrames(transport_tokens, max_frame_length,
                                     encoder,
                                     /*can_send_reset_stream=*/
                                     !(state == HttpStreamState::kIdle ||
                                       state == HttpStreamState::kClosed));
  }

  ////////////////////////////////////////////////////////////////////////////
  // Stream State Management
  // All state management helpers MUST be called from the transport party.

  // Modify the stream state
  // The possible stream transitions are as follows:
  // kIdle -> kOpen
  // kOpen -> kClosed/kHalfClosedLocal/kHalfClosedRemote
  // kHalfClosedLocal/kHalfClosedRemote -> kClosed
  // kClosed -> kClosed
  void SentInitialMetadata() {
    GRPC_DCHECK(stream_state == HttpStreamState::kIdle);
    stream_state = HttpStreamState::kOpen;
  }

  void MarkHalfClosedLocal() {
    switch (stream_state) {
      case HttpStreamState::kIdle:
        GRPC_DCHECK(false) << "MarkHalfClosedLocal called for an idle stream";
        break;
      case HttpStreamState::kOpen:
        GRPC_HTTP2_STREAM_LOG
            << "Http2ClientTransport::Stream::MarkHalfClosedLocal stream_id="
            << stream_id << " transitioning to kHalfClosedLocal";
        stream_state = HttpStreamState::kHalfClosedLocal;
        break;
      case HttpStreamState::kHalfClosedRemote:
        GRPC_HTTP2_STREAM_LOG
            << "Http2ClientTransport::Stream::MarkHalfClosedLocal stream_id="
            << stream_id << " transitioning to kClosed";
        stream_state = HttpStreamState::kClosed;
        break;
      case HttpStreamState::kHalfClosedLocal:
        break;
      case HttpStreamState::kClosed:
        GRPC_HTTP2_STREAM_LOG
            << "Http2ClientTransport::Stream::MarkHalfClosedLocal stream_id="
            << stream_id << " already closed";
        break;
    }
  }

  void MarkHalfClosedRemote() {
    switch (stream_state) {
      case HttpStreamState::kIdle:
        GRPC_DCHECK(false) << "MarkHalfClosedRemote called for an idle stream";
        break;
      case HttpStreamState::kOpen:
        GRPC_HTTP2_STREAM_LOG
            << "Http2ClientTransport::Stream::MarkHalfClosedRemote stream_id="
            << stream_id << " transitioning to kHalfClosedRemote";
        stream_state = HttpStreamState::kHalfClosedRemote;
        break;
      case HttpStreamState::kHalfClosedLocal:
        GRPC_HTTP2_STREAM_LOG
            << "Http2ClientTransport::Stream::MarkHalfClosedRemote stream_id="
            << stream_id << " transitioning to kClosed";
        stream_state = HttpStreamState::kClosed;
        break;
      case HttpStreamState::kHalfClosedRemote:
        break;
      case HttpStreamState::kClosed:
        GRPC_HTTP2_STREAM_LOG
            << "Http2ClientTransport::Stream::MarkHalfClosedRemote stream_id="
            << stream_id << " already closed";
        break;
    }
  }

  inline HttpStreamState GetStreamState() const { return stream_state; }
  inline uint32_t GetStreamId() const { return stream_id; }

  inline bool IsClosedForWrites() const { return is_write_closed; }
  inline void SetWriteClosed() { is_write_closed = true; }

  CallHandler call;
  // This flag is kept separate from the stream_state as the stream_state
  // is inline with the HTTP2 spec, whereas this flag is an implementation
  // detail of the PH2 transport. As far as PH2 is concerned, if a stream is
  // closed for writes, it will not send any more frames on that stream.
  // Similarly if a stream is closed for reads(this is achieved by removing the
  // stream from the transport map), then all the frames read on that stream
  // will be dropped.
  bool is_write_closed;
  // This MUST be accessed from the transport party.
  HttpStreamState stream_state;
  const uint32_t stream_id;
  GrpcMessageAssembler assembler;
  HeaderAssembler header_assembler;
  // TODO(akshitpatel) : [PH2][P2] : StreamQ should maintain a flag that
  // tracks if the half close has been sent for this stream. This flag is used
  // to notify the mixer that this stream is closed for
  // writes(HalfClosedLocal). When the mixer dequeues the last message for
  // the streamQ, it will mark the stream as closed for writes and send a
  // frame with end_stream or set the end_stream flag in the last data
  // frame being sent out. This is done as the stream state should not
  // transition to HalfClosedLocal till the end_stream frame is sent.
  bool did_push_initial_metadata;
  bool did_push_trailing_metadata;
  RefCountedPtr<StreamDataQueue<ClientMetadataHandle>> data_queue;
  chttp2::StreamFlowControl flow_control;
};

}  // namespace http2
}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_EXT_TRANSPORT_CHTTP2_TRANSPORT_STREAM_H
