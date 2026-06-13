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
#include "src/core/call/metadata_batch.h"
#include "src/core/ext/transport/chttp2/transport/flow_control.h"
#include "src/core/ext/transport/chttp2/transport/frame.h"
#include "src/core/ext/transport/chttp2/transport/hpack_encoder.h"
#include "src/core/ext/transport/chttp2/transport/http2_status.h"
#include "src/core/ext/transport/chttp2/transport/message_assembler.h"
#include "src/core/ext/transport/chttp2/transport/stream_data_queue.h"
#include "src/core/ext/transport/chttp2/transport/write_cycle.h"
#include "src/core/lib/resource_quota/arena.h"
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

// Managing the streams
class Stream : public RefCounted<Stream> {
 public:
  explicit Stream(CallHandler call_handler,
                  chttp2::TransportFlowControl& transport_flow_control)
      : flow_control_(&transport_flow_control),
        call_(std::move(call_handler)),
        started_(false),
        is_read_closed_(false),
        is_write_closed_(false),
        stream_id_(kInvalidStreamId),
        did_receive_initial_metadata_(false),
        did_receive_trailing_metadata_(false),
        did_push_server_trailing_metadata_(false),
        data_queue_(MakeRefCounted<StreamDataQueue<ClientMetadataHandle>>(
            std::get<CallHandler>(call_).arena(), /*is_client*/ true,
            /*queue_size*/ kStreamQueueSize)) {}

  explicit Stream(CallInitiator call_initiator,
                  chttp2::TransportFlowControl& transport_flow_control,
                  const uint32_t stream_id,
                  const bool allow_true_binary_metadata_peer)
      : flow_control_(&transport_flow_control),
        call_(std::move(call_initiator)),
        started_(true),
        is_read_closed_(false),
        is_write_closed_(false),
        stream_id_(stream_id),
        did_receive_initial_metadata_(false),
        did_receive_trailing_metadata_(false),
        did_push_server_trailing_metadata_(false),
        data_queue_(MakeRefCounted<StreamDataQueue<ClientMetadataHandle>>(
            std::get<CallInitiator>(call_).arena(), /*is_client*/ false,
            /*queue_size*/ kStreamQueueSize)) {
    InitializeStream(allow_true_binary_metadata_peer);
  }

  Stream(const Stream&) = delete;
  Stream(Stream&&) = delete;
  Stream& operator=(const Stream&) = delete;
  Stream& operator=(Stream&&) = delete;

  // TODO(akshitpatel) : [PH2][P4] : SetStreamId can be avoided if we pass the
  // stream id as a parameter to the dequeue function. The only downside here
  // is that we will be creating two new disassemblers for every dequeue call.
  // The upside is that we save 8 bytes per call. Decide based on benchmark
  // results.
  void InitializeClientStream(const uint32_t stream_id,
                              const bool allow_true_binary_metadata_peer) {
    GRPC_DCHECK(std::holds_alternative<CallHandler>(call_))
        << "Client Only Function";
    GRPC_DCHECK_NE(stream_id, 0u);
    GRPC_DCHECK_EQ(this->stream_id_, 0u);
    GRPC_HTTP2_STREAM_LOG << "Stream::InitializeClientStream stream_id="
                          << stream_id;
    if (GPR_LIKELY(this->stream_id_ == 0)) {
      this->stream_id_ = stream_id;
      InitializeStream(allow_true_binary_metadata_peer);
    }
  }

  ////////////////////////////////////////////////////////////////////////////
  // Data Queue Helpers
  // All enqueue methods are called from the call party.

  auto EnqueueInitialMetadata(Arena::PoolPtr<grpc_metadata_batch>&& metadata) {
    GRPC_HTTP2_STREAM_LOG << "Stream::EnqueueInitialMetadata";
    return data_queue_->EnqueueInitialMetadata(std::move(metadata));
  }

  // Only server can send trailing metadata in gRPC C++.
  auto EnqueueTrailingMetadata(ServerMetadataHandle&& metadata) {
    GRPC_HTTP2_STREAM_LOG << "Stream::EnqueueTrailingMetadata";
    GRPC_DCHECK(std::holds_alternative<CallInitiator>(call_))
        << "Only supported for server";
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
    GRPC_DCHECK(std::holds_alternative<CallHandler>(call_));
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
    // Reset stream MUST not be sent if the stream is idle or closed.
    return data_queue_->DequeueFrames(tokens, max_frame_length,
                                      stream_flow_control_tokens, encoder,
                                      frame_sender,
                                      /*can_send_reset_stream=*/
                                      !(IsStreamIdle() || IsStreamClosed()));
  }

  auto UpdateStreamWritability(const int64_t stream_fc_tokens) {
    return data_queue_->ReceivedFlowControlWindowUpdate(stream_fc_tokens);
  }

  ////////////////////////////////////////////////////////////////////////////
  // Stream State Management
  // All state management helpers, MUST be called from the transport party.

  // HTTP/2 stream states
  inline bool IsStreamIdle() const { return !started_; }
  inline bool IsOpen() const {
    return !IsStreamIdle() && !IsClosedForReads() && !IsClosedForWrites();
  }
  inline bool IsStreamClosed() const {
    return IsClosedForReads() && IsClosedForWrites();
  }
  inline bool IsStreamHalfClosedRemote() const {
    return IsClosedForReads() && !IsClosedForWrites();
  }
  inline bool IsHalfClosedLocal() const {
    return !IsClosedForReads() && IsClosedForWrites();
  }

  // gRPC stream state
  inline bool IsClosedForReads() const { return is_read_closed_; }
  inline bool IsClosedForWrites() const {
    return is_write_closed_.load(std::memory_order_relaxed);
  }

  // Setters called by transport/stream modules
  inline void SetStarted() { started_ = true; }
  inline void SetReadClosed() { is_read_closed_ = true; }
  inline void SetWriteClosed() {
    is_write_closed_.store(true, std::memory_order_relaxed);
  }

  inline uint32_t GetStreamId() const { return stream_id_; }

  inline bool CanSendWindowUpdateFrames() const {
    return IsOpen() || IsHalfClosedLocal();
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

  GPR_ATTRIBUTE_ALWAYS_INLINE_FUNCTION chttp2::StreamFlowControl&
  GetStreamFlowControl() {
    return flow_control_;
  }

 private:
  void InitializeStream(const bool allow_true_binary_metadata_peer) {
    GRPC_HTTP2_STREAM_LOG << "Stream::InitializeStream stream_id="
                          << stream_id_;
    GRPC_DCHECK_GE(stream_id_, 0u);
    data_queue_->SetStreamId(stream_id_, allow_true_binary_metadata_peer);
  }

  GrpcMessageAssembler assembler_;
  chttp2::StreamFlowControl flow_control_;
  std::variant<CallInitiator, CallHandler> call_;

  // The HTTP2 stream states are represented by the following flags:
  // - Idle (client only): !started_
  // - Open: started_ && !is_read_closed_ && !is_write_closed_
  // - Half-Closed Remote (reads closed): started_ && is_read_closed_ &&
  // !is_write_closed_
  // - Half-Closed Local (writes closed): started_ && !is_read_closed_ &&
  // is_write_closed_
  // - Closed: started_ && is_read_closed_ && is_write_closed_
  bool started_;
  bool is_read_closed_;
  std::atomic<bool> is_write_closed_;
  uint32_t stream_id_;
  bool did_receive_initial_metadata_;
  bool did_receive_trailing_metadata_;
  bool did_push_server_trailing_metadata_;
  // Change this if ClientMetadataHandle and ServerMetadataHandle are changed
  // to different types.
  RefCountedPtr<StreamDataQueue<Arena::PoolPtr<grpc_metadata_batch>>>
      data_queue_;
};

}  // namespace http2
}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_EXT_TRANSPORT_CHTTP2_TRANSPORT_STREAM_H
