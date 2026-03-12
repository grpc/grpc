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

#include <grpc/support/port_platform.h>

#include <atomic>
#include <cstdint>
#include <string>
#include <utility>
#include <variant>

#include "src/core/call/call_spine.h"
#include "src/core/call/message.h"
#include "src/core/call/metadata.h"
#include "src/core/ext/transport/chttp2/transport/flow_control.h"
#include "src/core/ext/transport/chttp2/transport/frame.h"
#include "src/core/ext/transport/chttp2/transport/header_assembler.h"
#include "src/core/ext/transport/chttp2/transport/hpack_encoder.h"
#include "src/core/ext/transport/chttp2/transport/http2_status.h"
#include "src/core/ext/transport/chttp2/transport/message_assembler.h"
#include "src/core/ext/transport/chttp2/transport/stream_data_queue.h"
#include "src/core/ext/transport/chttp2/transport/write_cycle.h"
#include "src/core/util/grpc_check.h"
#include "src/core/util/ref_counted.h"
#include "src/core/util/ref_counted_ptr.h"
#include "absl/log/log.h"

namespace grpc_core {
namespace http2 {

#define GRPC_HTTP2_STREAM_LOG VLOG(2)

// TODO(akshitpatel) : [PH2][P4] : Choose appropriate size later.
constexpr uint32_t kStreamQueueSize = /*1 MB*/ 1024u * 1024u;
constexpr uint32_t kInvalidStreamId = 0u;

enum class HttpStreamState : uint8_t {
  // https://www.rfc-editor.org/rfc/rfc9113.html#name-stream-states
  kIdle,
  kOpen,
  kHalfClosedLocal,
  kHalfClosedRemote,
  kClosed,
};

// Managing the streams
class Stream : public RefCounted<Stream> {
 public:
  explicit Stream(CallHandler call_handler,
                  chttp2::TransportFlowControl& transport_flow_control)
      : header_assembler_(/*is_client*/ true),
        flow_control_(&transport_flow_control),
        call_(std::move(call_handler)),
        is_write_closed_(false),
        stream_id_(kInvalidStreamId),
        stream_state_(HttpStreamState::kIdle),
        did_receive_initial_metadata_(false),
        did_receive_trailing_metadata_(false),
        did_push_server_trailing_metadata_(false),
        data_queue_(MakeRefCounted<StreamDataQueue<ClientMetadataHandle>>(
            std::get<CallHandler>(call_).arena(), /*is_client*/ true,
            /*queue_size*/ kStreamQueueSize)) {}

  explicit Stream(CallInitiator call_initiator,
                  chttp2::TransportFlowControl& transport_flow_control)
      : header_assembler_(/*is_client*/ false),
        flow_control_(&transport_flow_control),
        call_(std::move(call_initiator)),
        is_write_closed_(false),
        stream_id_(kInvalidStreamId),
        stream_state_(HttpStreamState::kIdle),
        did_receive_initial_metadata_(false),
        did_receive_trailing_metadata_(false),
        did_push_server_trailing_metadata_(false),
        data_queue_(MakeRefCounted<StreamDataQueue<ClientMetadataHandle>>(
            std::get<CallInitiator>(call_).arena(), /*is_client*/ false,
            /*queue_size*/ kStreamQueueSize)) {}

  Stream(const Stream&) = delete;
  Stream(Stream&&) = delete;
  Stream& operator=(const Stream&) = delete;
  Stream& operator=(Stream&&) = delete;

  // TODO(akshitpatel) : [PH2][P4] : SetStreamId can be avoided if we pass the
  // stream id as a parameter to the dequeue function. The only downside here
  // is that we will be creating two new disassemblers for every dequeue call.
  // The upside is that we save 8 bytes per call. Decide based on benchmark
  // results.
  void InitializeStream(const uint32_t stream_id,
                        const bool allow_true_binary_metadata_peer,
                        const bool allow_true_binary_metadata_acked) {
    GRPC_DCHECK_NE(stream_id, 0u);
    GRPC_DCHECK_EQ(this->stream_id_, 0u);
    GRPC_HTTP2_STREAM_LOG << "Stream::InitializeStream stream_id=" << stream_id;
    if (GPR_LIKELY(this->stream_id_ == 0)) {
      this->stream_id_ = stream_id;
      header_assembler_.InitializeStream(stream_id,
                                         allow_true_binary_metadata_acked);
      data_queue_->SetStreamId(stream_id, allow_true_binary_metadata_peer);
    }
  }

  ////////////////////////////////////////////////////////////////////////////
  // Data Queue Helpers
  // All enqueue methods are called from the call party.

  auto EnqueueInitialMetadata(ClientMetadataHandle&& metadata) {
    GRPC_HTTP2_STREAM_LOG << "Stream::EnqueueInitialMetadata";
    return data_queue_->EnqueueInitialMetadata(std::move(metadata));
  }

  auto EnqueueTrailingMetadata(ClientMetadataHandle&& metadata) {
    GRPC_HTTP2_STREAM_LOG << "Stream::EnqueueTrailingMetadata";
    return data_queue_->EnqueueTrailingMetadata(std::move(metadata));
  }

  auto EnqueueMessage(MessageHandle&& message) {
    GRPC_HTTP2_STREAM_LOG << "Stream::EnqueueMessage"
                          << " with payload size = "
                          << message->payload()->Length()
                          << " and flags = " << message->flags();
    return data_queue_->EnqueueMessage(std::move(message));
  }

  auto EnqueueHalfClosed() {
    GRPC_HTTP2_STREAM_LOG << "Stream::EnqueueHalfClosed";
    return data_queue_->EnqueueHalfClosed();
  }

  auto EnqueueResetStream(const uint32_t error_code) {
    GRPC_HTTP2_STREAM_LOG << "Stream::EnqueueResetStream"
                          << " with error_code = " << error_code;
    return data_queue_->EnqueueResetStream(error_code);
  }

  // Called from the transport party
  auto DequeueFrames(const uint32_t tokens,
                     const uint32_t stream_flow_control_tokens,
                     const uint32_t max_frame_length, HPackCompressor& encoder,
                     FrameSender& frame_sender) {
    HttpStreamState state = stream_state_;
    // Reset stream MUST not be sent if the stream is idle or closed.
    return data_queue_->DequeueFrames(tokens, max_frame_length,
                                      stream_flow_control_tokens, encoder,
                                      frame_sender,
                                      /*can_send_reset_stream=*/
                                      !(state == HttpStreamState::kIdle ||
                                        state == HttpStreamState::kClosed));
  }

  auto UpdateStreamWritability(const int64_t stream_fc_tokens) {
    return data_queue_->ReceivedFlowControlWindowUpdate(stream_fc_tokens);
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
    GRPC_DCHECK(stream_state_ == HttpStreamState::kIdle);
    stream_state_ = HttpStreamState::kOpen;
  }

  void MarkHalfClosedLocal() {
    switch (stream_state_) {
      case HttpStreamState::kIdle:
        GRPC_DCHECK(false) << "MarkHalfClosedLocal called for an idle stream";
        break;
      case HttpStreamState::kOpen:
        GRPC_HTTP2_STREAM_LOG
            << "Stream::MarkHalfClosedLocal stream_id=" << stream_id_
            << " transitioning to kHalfClosedLocal";
        stream_state_ = HttpStreamState::kHalfClosedLocal;
        break;
      case HttpStreamState::kHalfClosedRemote:
        GRPC_HTTP2_STREAM_LOG
            << "Stream::MarkHalfClosedLocal stream_id=" << stream_id_
            << " transitioning to kClosed";
        stream_state_ = HttpStreamState::kClosed;
        break;
      case HttpStreamState::kHalfClosedLocal:
        break;
      case HttpStreamState::kClosed:
        GRPC_HTTP2_STREAM_LOG
            << "Stream::MarkHalfClosedLocal stream_id=" << stream_id_
            << " already closed";
        break;
    }
  }

  void MarkHalfClosedRemote() {
    switch (stream_state_) {
      case HttpStreamState::kIdle:
        GRPC_DCHECK(false) << "MarkHalfClosedRemote called for an idle stream";
        break;
      case HttpStreamState::kOpen:
        GRPC_HTTP2_STREAM_LOG
            << "Stream::MarkHalfClosedRemote stream_id=" << stream_id_
            << " transitioning to kHalfClosedRemote";
        stream_state_ = HttpStreamState::kHalfClosedRemote;
        break;
      case HttpStreamState::kHalfClosedLocal:
        GRPC_HTTP2_STREAM_LOG
            << "Stream::MarkHalfClosedRemote stream_id=" << stream_id_
            << " transitioning to kClosed";
        stream_state_ = HttpStreamState::kClosed;
        break;
      case HttpStreamState::kHalfClosedRemote:
        break;
      case HttpStreamState::kClosed:
        GRPC_HTTP2_STREAM_LOG
            << "Stream::MarkHalfClosedRemote stream_id=" << stream_id_
            << " already closed";
        break;
    }
  }

  inline bool IsStreamIdle() const {
    return stream_state_ == HttpStreamState::kIdle;
  }
  inline bool IsStreamHalfClosedRemote() const {
    return stream_state_ == HttpStreamState::kHalfClosedRemote;
  }
  inline bool IsHalfClosedLocal() const {
    return stream_state_ == HttpStreamState::kHalfClosedLocal;
  }
  inline bool IsStreamClosed() const {
    return stream_state_ == HttpStreamState::kClosed;
  }

  inline uint32_t GetStreamId() const { return stream_id_; }

  inline bool IsClosedForWrites() const {
    return is_write_closed_.load(std::memory_order_relaxed);
  }

  inline void SetWriteClosed() {
    is_write_closed_.store(true, std::memory_order_relaxed);
  }

  inline bool CanSendWindowUpdateFrames() const {
    return stream_state_ == HttpStreamState::kOpen ||
           stream_state_ == HttpStreamState::kHalfClosedLocal;
  }

  inline Http2Status CanStreamReceiveDataFrames() const {
    if (IsStreamHalfClosedRemote()) {
      return Http2Status::Http2StreamError(
          Http2ErrorCode::kStreamClosed,
          std::string(RFC9113::kHalfClosedRemoteState));
    }
    if (!IsInitialMetadataReceived() || IsTrailingMetadataReceived()) {
      return Http2Status::Http2StreamError(
          Http2ErrorCode::kStreamClosed,
          std::string(GrpcErrors::kOutOfOrderDataFrame));
    }
    return Http2Status::Ok();
  }

  void MaybePushServerTrailingMetadata(ServerMetadataHandle&& metadata) {
    GRPC_DCHECK(std::holds_alternative<CallHandler>(call_));
    GRPC_HTTP2_STREAM_LOG << "Stream::MaybePushServerTrailingMetadata "
                             "stream_id="
                          << stream_id_
                          << " metadata=" << metadata->DebugString()
                          << " did_push_server_trailing_metadata="
                          << did_push_server_trailing_metadata_;

    if (!did_push_server_trailing_metadata_) {
      did_push_server_trailing_metadata_ = true;
      GetCallHandler().SpawnPushServerTrailingMetadata(std::move(metadata));
    }
  }

  GPR_ATTRIBUTE_ALWAYS_INLINE_FUNCTION bool IsInitialMetadataReceived() const {
    return did_receive_initial_metadata_;
  }

  GPR_ATTRIBUTE_ALWAYS_INLINE_FUNCTION void SetInitialMetadataReceived() {
    did_receive_initial_metadata_ = true;
  }

  GPR_ATTRIBUTE_ALWAYS_INLINE_FUNCTION bool IsTrailingMetadataReceived() const {
    return did_receive_trailing_metadata_;
  }

  GPR_ATTRIBUTE_ALWAYS_INLINE_FUNCTION void SetTrailingMetadataReceived() {
    did_receive_trailing_metadata_ = true;
  }

  GPR_ATTRIBUTE_ALWAYS_INLINE_FUNCTION CallHandler& GetCallHandler() {
    return std::get<CallHandler>(call_);
  }

  GPR_ATTRIBUTE_ALWAYS_INLINE_FUNCTION CallInitiator& GetCallInitiator() {
    return std::get<CallInitiator>(call_);
  }

  GPR_ATTRIBUTE_ALWAYS_INLINE_FUNCTION GrpcMessageAssembler&
  GetGrpcMessageAssembler() {
    return assembler_;
  }

  GPR_ATTRIBUTE_ALWAYS_INLINE_FUNCTION HeaderAssembler& GetHeaderAssembler() {
    return header_assembler_;
  }

  GPR_ATTRIBUTE_ALWAYS_INLINE_FUNCTION chttp2::StreamFlowControl&
  GetStreamFlowControl() {
    return flow_control_;
  }

 private:
  GrpcMessageAssembler assembler_;
  HeaderAssembler header_assembler_;
  chttp2::StreamFlowControl flow_control_;
  std::variant<CallInitiator, CallHandler> call_;

  // This flag is kept separate from the stream_state as the stream_state
  // is inline with the HTTP2 spec, whereas this flag is an implementation
  // detail of the PH2 transport. As far as PH2 is concerned, if a stream is
  // closed for writes, it will not send any more frames on that stream.
  // Similarly if a stream is closed for reads(this is achieved by removing the
  // stream from the transport map), then all the frames read on that stream
  // will be dropped.
  std::atomic<bool> is_write_closed_;
  uint32_t stream_id_;

  // This MUST be accessed from the transport party.
  HttpStreamState stream_state_;
  bool did_receive_initial_metadata_;
  bool did_receive_trailing_metadata_;
  bool did_push_server_trailing_metadata_;
  // TODO(akshitpatel) : [PH2][P0][Server] : This would need to change to
  // accomodate ServerMetadataHandle for the server side.
  RefCountedPtr<StreamDataQueue<ClientMetadataHandle>> data_queue_;
};

}  // namespace http2
}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_EXT_TRANSPORT_CHTTP2_TRANSPORT_STREAM_H
