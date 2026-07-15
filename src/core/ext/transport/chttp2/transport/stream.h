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
#include "absl/status/status.h"

namespace grpc_core {
namespace http2 {

#define GRPC_HTTP2_STREAM_LOG VLOG(2)

// TODO(akshitpatel) : [PH2][P4] : Choose appropriate size later.
constexpr uint32_t kStreamQueueSize = /*1 MB*/ 1024u * 1024u;
constexpr uint32_t kInvalidStreamId = 0u;

struct StreamStateChange {
  bool reads_became_closed = false;
  bool stream_became_closed = false;
  std::string DebugString() const {
    return absl::StrCat("reads_became_closed: ", reads_became_closed,
                        " stream_became_closed: ", stream_became_closed);
  }
};

class StreamState {
 public:
  explicit StreamState(const bool started) : started_(started) {}
  ~StreamState() = default;

  // StreamState is neither movable nor copyable.
  StreamState(const StreamState&) = delete;
  StreamState& operator=(const StreamState&) = delete;
  StreamState(StreamState&&) = delete;
  StreamState& operator=(StreamState&&) = delete;

  // State queries
  bool started() const { return started_; }
  bool reads_closed() const { return reads_closed_; }
  bool half_closed_local() const { return half_closed_local_; }
  bool writes_closed() const {
    return writes_closed_.load(std::memory_order_relaxed);
  }

  // A stream is closed when both reads and writes are closed.
  bool IsClosed() const { return reads_closed() && writes_closed(); }

  // A stream is idle if it has not started yet and has not been closed.
  // Note: HTTP/2 does not allow direct transitions from Idle to Half-Closed
  // states (it must go through Open first, which sets `started_` to true).
  // The only transition from Idle for an unstarted stream is to Closed
  // (e.g., via local cancellation). Thus, checking `!started() && !IsClosed()`
  // is sufficient to identify the Idle state.
  bool IsIdle() const { return !started() && !IsClosed(); }

  bool IsOpen() const {
    return started() && !reads_closed() && !writes_closed() &&
           !half_closed_local();
  }

  bool IsHalfClosedRemote() const { return reads_closed() && !writes_closed(); }

  bool IsHalfClosedLocal() const {
    return !reads_closed() && half_closed_local();
  }

  bool IsHalfClosedLocalSent() const { return half_closed_local(); }

  std::string DebugString() const {
    return absl::StrCat(
        "started:", started_, " reads_closed:", reads_closed_,
        " half_closed_local:", half_closed_local_,
        " writes_closed:", writes_closed_.load(std::memory_order_relaxed));
  }

  // Semantic state transitions
  void OnInitialMetadataSent() {
    GRPC_DCHECK(!started_);
    started_ = true;
  }

  StreamStateChange OnHalfCloseSent() {
    return RunTransition([this]() {
      half_closed_local_ = true;
      MaybeUpdateWritesClosed();
    });
  }

  StreamStateChange OnTrailingMetadataReceived() {
    return RunTransition([this]() {
      reads_closed_ = true;
      MaybeUpdateWritesClosed();
    });
  }

  StreamStateChange OnHalfCloseReceived() {
    return RunTransition([this]() { reads_closed_ = true; });
  }

  StreamStateChange OnResetReceived() {
    return RunTransition([this]() {
      reads_closed_ = true;
      half_closed_local_ = true;
      writes_closed_.store(true, std::memory_order_relaxed);
    });
  }

  StreamStateChange OnResetSent() {
    return RunTransition([this]() {
      reads_closed_ = true;
      half_closed_local_ = true;
      writes_closed_.store(true, std::memory_order_relaxed);
    });
  }

  StreamStateChange OnInitiateReset() {
    return RunTransition([this]() { reads_closed_ = true; });
  }

  // Force setters for fallback/init errors
  StreamStateChange ForceClose() {
    return RunTransition([this]() {
      reads_closed_ = true;
      half_closed_local_ = true;
      writes_closed_.store(true, std::memory_order_relaxed);
    });
  }

 private:
  template <typename F>
  StreamStateChange RunTransition(F&& transition_func) {
    const bool was_read_closed = reads_closed_;
    const bool was_closed = IsClosed();

    transition_func();
    GRPC_HTTP2_STREAM_LOG << "StreamState transition from "
                          << "{reads_closed: " << was_read_closed
                          << ", closed: " << was_closed << "}"
                          << " to " << DebugString();
    return {/*reads_became_closed=*/!was_read_closed && reads_closed(),
            /*stream_became_closed=*/!was_closed && IsClosed()};
  }

  void MaybeUpdateWritesClosed() {
    // Client: if we sent half close and peer closed reads, writes are closed.
    // Server: half_closed_local_ is never explicitly set, so this function is
    // mostly a no-op.
    if (reads_closed_ && half_closed_local_) {
      writes_closed_.store(true, std::memory_order_relaxed);
    }
  }

  // Started is set to true when initial metadata has been sent on the wire.
  // For client, this is when initial metadata has been sent on the wire. For
  // server, this is when initial metadata has been received from the wire.
  bool started_ = false;

  // Has peer sent a frame notifying that no more frames will be sent (trailing
  // metadata or RST or half close).
  bool reads_closed_ = false;

  // Has the transport sent a frame notifying peer that no more data frames will
  // be sent (half close). Note: Client can still send RST after this point
  // until writes are fully closed.
  bool half_closed_local_ = false;

  // Absolutely nothing can be sent for this stream now (after RST or after both
  // half_close and reads_closed for client).
  std::atomic<bool> writes_closed_{false};
};

// Managing the streams
class Stream : public RefCounted<Stream> {
 public:
  explicit Stream(CallHandler call_handler,
                  chttp2::TransportFlowControl& transport_flow_control)
      : flow_control_(&transport_flow_control),
        call_(std::move(call_handler)),
        state_(/*started=*/false),
        stream_id_(kInvalidStreamId),
        did_receive_initial_metadata_(false),
        did_receive_trailing_metadata_(false),
        did_push_server_trailing_metadata_(false),
        did_cancel_(false),
        data_queue_(MakeRefCounted<StreamDataQueue<ClientMetadataHandle>>(
            std::get<CallHandler>(call_).arena(), /*is_client*/ true,
            /*queue_size*/ kStreamQueueSize)) {}

  explicit Stream(CallInitiator call_initiator,
                  chttp2::TransportFlowControl& transport_flow_control,
                  const uint32_t stream_id,
                  const bool allow_true_binary_metadata_peer)
      : flow_control_(&transport_flow_control),
        call_(std::move(call_initiator)),
        state_(/*started=*/true),
        stream_id_(stream_id),
        did_receive_initial_metadata_(false),
        did_receive_trailing_metadata_(false),
        did_push_server_trailing_metadata_(false),
        did_cancel_(false),
        data_queue_(MakeRefCounted<StreamDataQueue<ClientMetadataHandle>>(
            std::get<CallInitiator>(call_).arena(), /*is_client*/ false,
            /*queue_size*/ kStreamQueueSize)) {
    InitializeStream(allow_true_binary_metadata_peer);
  }

  Stream(const Stream&) = delete;
  Stream(Stream&&) = delete;
  Stream& operator=(const Stream&) = delete;
  Stream& operator=(Stream&&) = delete;

  void InitializeClientStream(const uint32_t stream_id,
                              const bool allow_true_binary_metadata_peer) {
    GRPC_DCHECK(is_client()) << "Client Only Function";
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
    GRPC_DCHECK(is_client());
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
  bool IsStreamIdle() const { return state_.IsIdle(); }
  bool IsOpen() const { return state_.IsOpen(); }
  bool IsStreamClosed() const { return state_.IsClosed(); }
  bool IsStreamHalfClosedRemote() const { return state_.IsHalfClosedRemote(); }
  bool IsHalfClosedLocal() const { return state_.IsHalfClosedLocal(); }
  bool IsHalfClosedLocalSent() const { return state_.IsHalfClosedLocalSent(); }

  // gRPC stream state
  bool IsClosedForReads() const { return state_.reads_closed(); }
  bool IsClosedForWrites() const { return state_.writes_closed(); }

  // Semantic state transitions
  void OnInitialMetadataSent() {
    GRPC_DCHECK(is_client());
    state_.OnInitialMetadataSent();
  }

  StreamStateChange OnHalfCloseSent() {
    GRPC_DCHECK(is_client());
    return state_.OnHalfCloseSent();
  }

  StreamStateChange OnTrailingMetadataReceived(ServerMetadataHandle metadata) {
    SetTrailingMetadataReceived();
    const StreamStateChange change = state_.OnTrailingMetadataReceived();
    if (is_client()) {
      MaybePushServerTrailingMetadata(std::move(metadata));
    } else {
      // Server: custom HTTP2 implementations might send trailing metadata.
      // We just close reads and do nothing else (upper layers discard client
      // trailers).
      GRPC_HTTP2_STREAM_LOG << "Stream::OnTrailingMetadataReceived Server "
                               "stream_id="
                            << stream_id_
                            << " StreamStateChange=" << change.DebugString();
      if (change.reads_became_closed) {
        GetCallInitiator().SpawnFinishSends();
      }
    }
    return change;
  }

  StreamStateChange OnHalfCloseReceived() {
    GRPC_DCHECK(is_server());
    const StreamStateChange change = state_.OnHalfCloseReceived();
    if (change.reads_became_closed) {
      GetCallInitiator().SpawnFinishSends();
    }
    return change;
  }

  StreamStateChange OnResetReceived(absl::Status status) {
    const StreamStateChange change = state_.OnResetReceived();
    CancelCall(std::move(status));
    return change;
  }

  StreamStateChange OnResetSent() {
    const StreamStateChange change = state_.OnResetSent();

    // Fallback cancel. Safe even for normal completion as CallSpine would have
    // already processed trailing metadata, making Cancel a no-op.
    CancelCall(absl::CancelledError("RST stream sent"));
    return change;
  }

  StreamStateChange OnInitiateReset(absl::Status status) {
    const StreamStateChange change = state_.OnInitiateReset();
    CancelCall(std::move(status));
    return change;
  }

  StreamStateChange ForceClose(absl::Status status) {
    const StreamStateChange change = state_.ForceClose();
    CancelCall(std::move(status));
    return change;
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
    GRPC_DCHECK(is_client());
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

  GPR_ATTRIBUTE_ALWAYS_INLINE_FUNCTION const CallHandler& GetCallHandler()
      const {
    return std::get<CallHandler>(call_);
  }

  GPR_ATTRIBUTE_ALWAYS_INLINE_FUNCTION CallInitiator& GetCallInitiator() {
    return std::get<CallInitiator>(call_);
  }

  GPR_ATTRIBUTE_ALWAYS_INLINE_FUNCTION const CallInitiator& GetCallInitiator()
      const {
    return std::get<CallInitiator>(call_);
  }

  GPR_ATTRIBUTE_ALWAYS_INLINE_FUNCTION GrpcMessageAssembler&
  GetGrpcMessageAssembler() {
    return assembler_;
  }

  GPR_ATTRIBUTE_ALWAYS_INLINE_FUNCTION const GrpcMessageAssembler&
  GetGrpcMessageAssembler() const {
    return assembler_;
  }

  GPR_ATTRIBUTE_ALWAYS_INLINE_FUNCTION chttp2::StreamFlowControl&
  GetStreamFlowControl() {
    return flow_control_;
  }

  GPR_ATTRIBUTE_ALWAYS_INLINE_FUNCTION const chttp2::StreamFlowControl&
  GetStreamFlowControl() const {
    return flow_control_;
  }

  bool is_client() const { return std::holds_alternative<CallHandler>(call_); }
  bool is_server() const {
    return std::holds_alternative<CallInitiator>(call_);
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

  // This function is idempotent.
  void CancelCall(absl::Status&& status) {
    if (std::exchange(did_cancel_, true)) return;
    if (is_client()) {
      MaybePushServerTrailingMetadata(
          CancelledServerMetadataFromStatus(status));
    } else {
      GetCallInitiator().SpawnCancel(std::forward<absl::Status>(status));
    }
  }

  StreamState state_;
  uint32_t stream_id_;
  bool did_receive_initial_metadata_;
  bool did_receive_trailing_metadata_;
  bool did_push_server_trailing_metadata_;
  bool did_cancel_;
  // Change this if ClientMetadataHandle and ServerMetadataHandle are changed
  // to different types.
  RefCountedPtr<StreamDataQueue<Arena::PoolPtr<grpc_metadata_batch>>>
      data_queue_;
};

}  // namespace http2
}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_EXT_TRANSPORT_CHTTP2_TRANSPORT_STREAM_H
