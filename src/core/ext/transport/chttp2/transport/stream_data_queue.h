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

#ifndef GRPC_SRC_CORE_EXT_TRANSPORT_CHTTP2_TRANSPORT_STREAM_DATA_QUEUE_H
#define GRPC_SRC_CORE_EXT_TRANSPORT_CHTTP2_TRANSPORT_STREAM_DATA_QUEUE_H

#include <algorithm>
#include <optional>
#include <queue>
#include <variant>
#include <vector>

#include "src/core/call/message.h"
#include "src/core/ext/transport/chttp2/transport/frame.h"
#include "src/core/ext/transport/chttp2/transport/header_assembler.h"
#include "src/core/ext/transport/chttp2/transport/message_assembler.h"
#include "src/core/ext/transport/chttp2/transport/transport_common.h"
#include "src/core/lib/promise/poll.h"
#include "src/core/util/grpc_check.h"
#include "absl/log/log.h"

namespace grpc_core {
namespace http2 {

#define GRPC_STREAM_DATA_QUEUE_DEBUG VLOG(2)

// SimpleQueue is a NOT thread safe.
// Note: SimpleQueue is a single producer single consumer queue.
template <typename T>
class SimpleQueue {
 public:
  explicit SimpleQueue(const uint32_t max_tokens) : max_tokens_(max_tokens) {}
  SimpleQueue(SimpleQueue&& rhs) = delete;
  SimpleQueue& operator=(SimpleQueue&& rhs) = delete;
  SimpleQueue(const SimpleQueue&) = delete;
  SimpleQueue& operator=(const SimpleQueue&) = delete;

  // A promise that resolves when the data is enqueued.
  // If tokens_consumed_ is 0 or the new tokens fit within max_tokens_, then
  // allow the enqueue to go through. Otherwise, return pending. Here, we are
  // using tokens_consumed over queue_.empty() because there can be enqueues
  // with tokens = 0. Enqueues with tokens = 0 are primarily for sending
  // metadata as flow control does not apply to them. This function is NOT
  // thread safe.
  Poll<bool> Enqueue(T& data, const uint32_t tokens) {
    return PollEnqueue(data, tokens);
  }

  absl::StatusOr<bool> ImmediateEnqueue(T data, const uint32_t tokens) {
    return ImmediateEnqueueInternal(std::move(data), tokens);
  }

  // Sync function to dequeue the next entry. Returns nullopt if the queue is
  // empty or if the front of the queue has more tokens than
  // allowed_dequeue_tokens. When allow_oversized_dequeue parameter is set to
  // true, it allows an item to be dequeued even if its token cost is greater
  // than allowed_dequeue_tokens. It does not cause the item itself to be
  // partially dequeued; either the entire item is returned or nullopt is
  // returned. This function is NOT thread safe.
  std::optional<T> Dequeue(const uint32_t allowed_dequeue_tokens,
                           const bool allow_oversized_dequeue) {
    return DequeueInternal(allowed_dequeue_tokens, allow_oversized_dequeue);
  }

  // Dequeues the next entry immediately ignoring the tokens. If the queue is
  // empty, returns nullopt. This function is NOT thread safe.
  std::optional<T> ImmediateDequeue() {
    return DequeueInternal(std::numeric_limits<uint32_t>::max(), true);
  }

  // Returns true if the queue is empty. This function is NOT thread safe.
  bool IsEmpty() const { return queue_.empty(); }
  // Clears the queue. This function is NOT thread safe.
  void Clear() { std::queue<Entry>().swap(queue_); }

  inline std::optional<uint32_t> GetNextEntryTokens() const {
    return queue_.empty() ? std::nullopt
                          : std::make_optional(queue_.front().tokens);
  }

 private:
  Poll<bool> PollEnqueue(T& data, const uint32_t tokens) {
    GRPC_STREAM_DATA_QUEUE_DEBUG << "Enqueueing data. Data tokens: " << tokens;
    const uint32_t max_tokens_consumed_threshold =
        max_tokens_ >= tokens ? max_tokens_ - tokens : 0;
    if (tokens_consumed_ == 0 ||
        tokens_consumed_ <= max_tokens_consumed_threshold) {
      tokens_consumed_ += tokens;
      queue_.emplace(Entry{std::move(data), tokens});
      GRPC_STREAM_DATA_QUEUE_DEBUG
          << "Enqueue successful. Data tokens: " << tokens
          << " Current tokens consumed: " << tokens_consumed_;
      return /*became_non_empty=*/(queue_.size() == 1);
    }

    GRPC_STREAM_DATA_QUEUE_DEBUG
        << "Token threshold reached. Data tokens: " << tokens
        << " Tokens consumed: " << tokens_consumed_
        << " Max tokens: " << max_tokens_;
    waker_ = GetContext<Activity>()->MakeNonOwningWaker();
    return Pending{};
  }

  inline absl::StatusOr<bool> ImmediateEnqueueInternal(T data,
                                                       const uint32_t tokens) {
    GRPC_DCHECK_LE(tokens_consumed_,
                   std::numeric_limits<uint32_t>::max() - tokens);
    if (tokens_consumed_ > std::numeric_limits<uint32_t>::max() - tokens) {
      return absl::InternalError("Tokens consumed overflowed.");
    }
    tokens_consumed_ += tokens;
    queue_.emplace(Entry{std::move(data), tokens});
    GRPC_STREAM_DATA_QUEUE_DEBUG
        << "Immediate enqueue successful. Data tokens: " << tokens
        << " Current tokens consumed: " << tokens_consumed_;
    return /*became_non_empty*/ (queue_.size() == 1);
  }

  std::optional<T> DequeueInternal(const uint32_t allowed_dequeue_tokens,
                                   const bool allow_oversized_dequeue) {
    if (queue_.empty() || (queue_.front().tokens > allowed_dequeue_tokens &&
                           !allow_oversized_dequeue)) {
      GRPC_STREAM_DATA_QUEUE_DEBUG
          << "Dequeueing data. Queue size: " << queue_.size()
          << " Max allowed dequeue tokens: " << allowed_dequeue_tokens
          << " Front tokens: "
          << (!queue_.empty() ? std::to_string(queue_.front().tokens)
                              : std::string("NA"))
          << " Allow oversized dequeue: " << allow_oversized_dequeue;
      return std::nullopt;
    }

    auto entry = std::move(queue_.front());
    queue_.pop();
    tokens_consumed_ -= entry.tokens;
    auto waker = std::move(waker_);
    GRPC_STREAM_DATA_QUEUE_DEBUG
        << "Dequeue successful. Data tokens released: " << entry.tokens
        << " Current tokens consumed: " << tokens_consumed_;

    // TODO(akshitpatel) : [PH2][P2] : Investigate a mechanism to only wake up
    // if the sender will be able to send more data. There is a high chance that
    // this queue is revamped soon and so not spending time on optimization
    // right now.
    waker.Wakeup();
    return std::move(entry.data);
  }

  struct Entry {
    T data;
    uint32_t tokens;
  };

  std::queue<Entry> queue_;
  // The maximum number of tokens that can be enqueued. This limit is used to
  // exert back pressure on the sender. If the sender tries to enqueue more
  // tokens than this limit, the enqueue promise will not resolve until the
  // required number of tokens are consumed by the receiver. There is an
  // exception to this rule: if the sender tries to enqueue an item when the
  // queue has 0 tokens, the enqueue will always go through regardless of the
  // number of tokens.
  const uint64_t max_tokens_;
  // The number of tokens that have been enqueued in the queue but not yet
  // dequeued.
  uint64_t tokens_consumed_ = 0;
  Waker waker_;
};

// StreamDataQueue is a thread safe.
// Note: StreamDataQueue is a single producer single
// consumer queue.
template <typename MetadataHandle>
class StreamDataQueue : public RefCounted<StreamDataQueue<MetadataHandle>> {
 public:
  explicit StreamDataQueue(const bool is_client, const uint32_t queue_size,
                           bool allow_true_binary_metadata)
      : stream_id_(0),
        is_client_(is_client),
        queue_(queue_size),
        initial_metadata_disassembler_(/*is_trailing_metadata=*/false,
                                       allow_true_binary_metadata),
        trailing_metadata_disassembler_(/*is_trailing_metadata=*/true,
                                        allow_true_binary_metadata) {};
  ~StreamDataQueue() = default;

  StreamDataQueue(StreamDataQueue&& rhs) = delete;
  StreamDataQueue& operator=(StreamDataQueue&& rhs) = delete;
  StreamDataQueue(const StreamDataQueue&) = delete;
  StreamDataQueue& operator=(const StreamDataQueue&) = delete;

  void SetStreamId(const uint32_t stream_id) {
    GRPC_DCHECK_EQ(stream_id_, 0u);
    GRPC_DCHECK_NE(stream_id, 0u);
    stream_id_ = stream_id;
    initial_metadata_disassembler_.SetStreamId(stream_id);
    trailing_metadata_disassembler_.SetStreamId(stream_id);
  }

  //////////////////////////////////////////////////////////////////////////////
  // Enqueue Helpers
  // These enqueue helpers are based on following assumptions:
  // 1. The ordering of initial metadata, messages and trailing metadata is
  //    taken care by the Callv-3 stack.
  // 2. Initial metadata MUST be enqueued before the first message.
  // 3. Initial metadata and trailing metadata are both optional. A server can
  //    skip initial metadata and a client will never send trailing metadata.
  // 4. A server will never send half close.
  // 5. Trailing metadata/HalfClose/ResetStream MUST be enqueued at most once
  //    per stream.
  // 6. After trailing metadata/HalfClose only ResetStream MAY be enqueued.
  // 7. The ResetStream MUST be the final thing that is queued.
  // 8. Currently initial metadata is not expected to be enqueued with
  //    end_stream set. If the stream needs to be half closed, the client should
  //    enqueue a half close message.

  struct EnqueueResult {
    bool became_writable;
    WritableStreamPriority priority;
  };

  // Enqueue Initial Metadata.
  // 1. MUST be called at most once.
  // 2. This MUST be called before any messages are enqueued.
  // 3. MUST not be called after trailing metadata is enqueued.
  // 4. This function is thread safe.
  absl::StatusOr<EnqueueResult> EnqueueInitialMetadata(
      MetadataHandle&& metadata) {
    MutexLock lock(&mu_);
    GRPC_DCHECK(!is_initial_metadata_queued_);
    GRPC_DCHECK(!is_trailing_metadata_or_half_close_queued_);
    GRPC_DCHECK(metadata != nullptr);
    GRPC_DCHECK(reset_stream_state_ == RstStreamState::kNotQueued);

    is_initial_metadata_queued_ = true;
    absl::StatusOr<bool> result = queue_.ImmediateEnqueue(
        QueueEntry{InitialMetadataType{std::move(metadata)}}, /*tokens=*/0);
    if (GPR_UNLIKELY(!result.ok())) {
      GRPC_STREAM_DATA_QUEUE_DEBUG
          << "Immediate enqueueing initial metadata for stream " << stream_id_
          << " failed with status: " << result.status();
      return result.status();
    }
    return UpdateWritableStateAndPriorityEnqueueLocked(
        /*became_non_empty*/ result.value(), WritableStreamPriority::kDefault);
  }

  // Enqueue Trailing Metadata.
  // 1. MUST be called at most once.
  // 2. MUST be called only for a server.
  // 3. This function is thread safe.
  absl::StatusOr<EnqueueResult> EnqueueTrailingMetadata(
      MetadataHandle&& metadata) {
    MutexLock lock(&mu_);
    GRPC_DCHECK(metadata != nullptr);
    GRPC_DCHECK(!is_client_);
    GRPC_DCHECK(!is_trailing_metadata_or_half_close_queued_);

    if (GPR_UNLIKELY(IsEnqueueClosed())) {
      GRPC_STREAM_DATA_QUEUE_DEBUG << "Enqueue closed for stream "
                                   << stream_id_;
      return EnqueueResult{/*became_writable=*/false,
                           WritableStreamPriority::kStreamClosed};
    }

    is_trailing_metadata_or_half_close_queued_ = true;
    absl::StatusOr<bool> result = queue_.ImmediateEnqueue(
        QueueEntry{TrailingMetadataType{std::move(metadata)}}, /*tokens=*/0);
    if (GPR_UNLIKELY(!result.ok())) {
      GRPC_STREAM_DATA_QUEUE_DEBUG
          << "Immediate enqueueing trailing metadata for stream " << stream_id_
          << " failed with status: " << result.status();
      return result.status();
    }
    return UpdateWritableStateAndPriorityEnqueueLocked(
        /*became_non_empty*/ result.value(),
        WritableStreamPriority::kStreamClosed);
  }

  // Returns a promise that resolves when the message is enqueued. There may be
  // delays in queueing the message if data queue is full.
  // 1. MUST be called after initial metadata is enqueued.
  // 2. MUST not be called after trailing metadata is enqueued.
  // 3. This function is thread safe.
  auto EnqueueMessage(MessageHandle&& message) {
    GRPC_DCHECK(is_initial_metadata_queued_);
    GRPC_DCHECK(message != nullptr);
    GRPC_DCHECK_LE(
        message->payload()->Length(),
        std::numeric_limits<uint32_t>::max() - kGrpcHeaderSizeInBytes);
    GRPC_DCHECK(!is_trailing_metadata_or_half_close_queued_);

    const uint32_t tokens =
        message->payload()->Length() + kGrpcHeaderSizeInBytes;
    return [self = this->Ref(), entry = QueueEntry{std::move(message)},
            tokens]() mutable -> Poll<absl::StatusOr<EnqueueResult>> {
      MutexLock lock(&self->mu_);
      if (GPR_UNLIKELY(self->IsEnqueueClosed())) {
        GRPC_STREAM_DATA_QUEUE_DEBUG << "Enqueue closed for stream "
                                     << self->stream_id_;
        return EnqueueResult{/*became_writable=*/false,
                             WritableStreamPriority::kStreamClosed};
      }
      Poll<bool> result = self->queue_.Enqueue(entry, tokens);
      if (result.ready()) {
        GRPC_STREAM_DATA_QUEUE_DEBUG << "Enqueued message for stream "
                                     << self->stream_id_
                                     << " with tokens: " << tokens
                                     << "became_non_empty: " << result.value();

        return self->UpdateWritableStateAndPriorityEnqueueLocked(
            /*became_non_empty=*/result.value(),
            WritableStreamPriority::kDefault);
      }
      return Pending{};
    };
  }

  // Enqueue Half Closed.
  // 1. MUST be called at most once.
  // 2. MUST be called only for a client.
  // 3. This function is thread safe.
  absl::StatusOr<EnqueueResult> EnqueueHalfClosed() {
    MutexLock lock(&mu_);
    GRPC_DCHECK(is_initial_metadata_queued_);
    GRPC_DCHECK(is_client_);

    if (GPR_UNLIKELY(IsEnqueueClosed() ||
                     is_trailing_metadata_or_half_close_queued_)) {
      GRPC_STREAM_DATA_QUEUE_DEBUG
          << "Enqueue closed or trailing metadata/half close queued for stream "
          << stream_id_ << " is_trailing_metadata_or_half_close_queued_ = "
          << is_trailing_metadata_or_half_close_queued_;
      return EnqueueResult{/*became_writable=*/false,
                           WritableStreamPriority::kStreamClosed};
    }

    is_trailing_metadata_or_half_close_queued_ = true;
    absl::StatusOr<bool> result =
        queue_.ImmediateEnqueue(QueueEntry{HalfClosed{}}, /*tokens=*/0);
    if (GPR_UNLIKELY(!result.ok())) {
      GRPC_STREAM_DATA_QUEUE_DEBUG
          << "Immediate enqueueing half closed for stream " << stream_id_
          << " failed with status: " << result.status();
      return result.status();
    }
    return UpdateWritableStateAndPriorityEnqueueLocked(
        /*became_non_empty*/ result.value(),
        WritableStreamPriority::kStreamClosed);
  }

  // Enqueue Reset Stream.
  // 1. MUST be called at most once.
  // 3. This function is thread safe.
  absl::StatusOr<EnqueueResult> EnqueueResetStream(const uint32_t error_code) {
    MutexLock lock(&mu_);
    GRPC_DCHECK(is_initial_metadata_queued_);

    // This can happen when the transport tries to close the stream and the
    // stream is cancelled from the call stack.
    if (GPR_UNLIKELY(IsEnqueueClosed())) {
      GRPC_STREAM_DATA_QUEUE_DEBUG << "Enqueue closed for stream "
                                   << stream_id_;
      return EnqueueResult{/*became_writable=*/false,
                           WritableStreamPriority::kStreamClosed};
    }

    GRPC_STREAM_DATA_QUEUE_DEBUG
        << "Immediate enqueueing reset stream for stream " << stream_id_
        << " with error code: " << error_code;
    reset_stream_state_ = RstStreamState::kQueued;
    reset_stream_error_code_ = error_code;

    // became_non_empty is set to true if the queue is empty because we are not
    // enqueueing reset stream to the queue. In this case, if the queue is
    // empty, enqueuing reset stream to StreamDataQueue will make the stream
    // writable.
    return UpdateWritableStateAndPriorityEnqueueLocked(
        /*became_non_empty*/ queue_.IsEmpty(),
        WritableStreamPriority::kStreamClosed);
  }

  //////////////////////////////////////////////////////////////////////////////
  // Dequeue Helpers

  static constexpr uint8_t kResetStreamDequeued = 0x1;
  static constexpr uint8_t kHalfCloseDequeued = 0x2;
  static constexpr uint8_t kInitialMetadataDequeued = 0x4;

  struct DequeueResult {
    std::vector<Http2Frame> frames;
    bool is_writable;
    WritableStreamPriority priority;
    // Maybe not be extremely accurate but should be good enough for our
    // purposes.
    size_t total_bytes_consumed = 0u;
    size_t flow_control_tokens_consumed = 0u;
    // Bitmask of the dequeue flags.
    uint8_t flags = 0u;

    // Returns true if the reset stream was dequeued.
    bool ResetStreamDequeued() const {
      return (flags & kResetStreamDequeued) != 0u;
    }

    // Returns true if the half close was dequeued.
    bool HalfCloseDequeued() const {
      return (flags & kHalfCloseDequeued) != 0u;
    }

    // Returns true if the initial metadata was dequeued.
    bool InitialMetadataDequeued() const {
      return (flags & kInitialMetadataDequeued) != 0u;
    }
  };

  // TODO(akshitpatel) : [PH2][P4] : Measure the performance of this function
  // and optimize it if needed.

  // This function is deliberately a synchronous call. The caller of this
  // function should not be blocked till we have enough data to return. This is
  // because the caller needs to dequeue frames from multiple streams in a
  // single cycle. The goal here is to return as much data as possible in one go
  // with max_tokens as the upper limit. General idea: Out goal here is to push
  // as much data as possible with max_tokens as the upper limit. Though we will
  // not prefer sending incomplete messages. We handle the scenario of
  // incomplete messages in the following way:
  // 1. If we have sent x full messages and available flow control tokens cannot
  //    accommodate x+1 message, we will not dequeue the x+1st message and
  //    create frames for x messages.
  // 2. If the flow control tokens cannot accommodate first message, we
  //    dequeue the first message from the queue and create frames for
  //    the partial first message (sum of payload of all returned frames <=
  //    max_fc_tokens).
  // This function is thread safe.
  DequeueResult DequeueFrames(const uint32_t max_fc_tokens,
                              const uint32_t max_frame_length,
                              const uint32_t stream_fc_tokens,
                              HPackCompressor& encoder,
                              const bool can_send_reset_stream) {
    MutexLock lock(&mu_);
    GRPC_STREAM_DATA_QUEUE_DEBUG
        << "Dequeueing frames for stream " << stream_id_
        << " Max fc tokens: " << max_fc_tokens
        << " Max frame length: " << max_frame_length
        << " Message disassembler buffered length: "
        << message_disassembler_.GetBufferedLength()
        << " Can send reset stream: " << can_send_reset_stream
        << " Reset stream state: " << static_cast<uint8_t>(reset_stream_state_);
    GRPC_DCHECK_GT(stream_id_, 0u)
        << "Stream id must be set before dequeueing frames.";

    // If a reset stream is queued, we do not want to send any more frames. Any
    // metadata enqueued has not reached HPACK encoder, so it is safe to drop
    // all frames.
    if (std::optional<DequeueResult> result =
            HandleResetStreamLocked(can_send_reset_stream)) {
      return std::move(*result);
    }

    HandleDequeue handle_dequeue(max_fc_tokens, max_frame_length, encoder,
                                 *this);
    while (message_disassembler_.GetBufferedLength() <= max_fc_tokens) {
      const uint32_t tokens_to_dequeue =
          max_fc_tokens - message_disassembler_.GetBufferedLength();
      std::optional<QueueEntry> queue_entry =
          queue_.Dequeue(tokens_to_dequeue, /*allow_oversized_dequeue*/ (
                             message_disassembler_.GetBufferedLength() == 0 &&
                             tokens_to_dequeue > 0));
      if (!queue_entry.has_value()) {
        // Nothing more to dequeue right now.
        GRPC_STREAM_DATA_QUEUE_DEBUG << "No more data to dequeue";
        break;
      }
      std::visit(handle_dequeue, std::move(*queue_entry));
    }

    GRPC_DCHECK_GE(stream_fc_tokens,
                   handle_dequeue.GetFlowControlTokensConsumed());

    return DequeueResult{
        handle_dequeue.GetFrames(),
        UpdateWritableStateDequeueLocked(
            stream_fc_tokens - handle_dequeue.GetFlowControlTokensConsumed()),
        priority_,
        handle_dequeue.GetTotalBytesConsumed(),
        handle_dequeue.GetFlowControlTokensConsumed(),
        handle_dequeue.GetDequeueFlags()};
  }

  // TODO(tjagtap) : [PH2][P1][FlowControl] : Call this while processing
  // window update frame.
  // Needs to be invoked when the peer sends stream flow control window update.
  // stream_fc_tokens represents the stream flow control (delta) window +
  // intial_window_size.
  bool ReceivedFlowControlWindowUpdate(const uint32_t stream_fc_tokens) {
    MutexLock lock(&mu_);
    GRPC_STREAM_DATA_QUEUE_DEBUG
        << "Received flow control window update for stream " << stream_id_
        << " stream_fc_tokens: " << stream_fc_tokens;
    return UpdateWritableStateDequeueLocked(stream_fc_tokens);
  }

  // Returns true if the queue is empty. This function is thread safe.
  bool TestOnlyIsEmpty() {
    MutexLock lock(&mu_);
    return queue_.IsEmpty();
  }

 private:
  struct InitialMetadataType {
    MetadataHandle metadata;
  };
  struct TrailingMetadataType {
    MetadataHandle metadata;
  };
  struct HalfClosed {};
  using QueueEntry = std::variant<InitialMetadataType, TrailingMetadataType,
                                  MessageHandle, HalfClosed>;

  class HandleDequeue {
   public:
    HandleDequeue(const uint32_t max_tokens, const uint32_t max_frame_length,
                  HPackCompressor& encoder, StreamDataQueue& queue)
        : queue_(queue),
          max_frame_length_(max_frame_length),
          max_tokens_available_(max_tokens),
          flow_control_tokens_consumed_(0),
          encoder_(encoder) {}

    void operator()(InitialMetadataType initial_metadata) {
      GRPC_STREAM_DATA_QUEUE_DEBUG << "Preparing initial metadata for sending";
      queue_.initial_metadata_disassembler_.PrepareForSending(
          std::move(initial_metadata.metadata), encoder_);
      dequeue_flags_ |= kInitialMetadataDequeued;
      MaybeAppendInitialMetadataFrames();
    }

    void operator()(TrailingMetadataType trailing_metadata) {
      GRPC_STREAM_DATA_QUEUE_DEBUG << "Preparing trailing metadata for sending";
      queue_.trailing_metadata_disassembler_.PrepareForSending(
          std::move(trailing_metadata.metadata), encoder_);
    }

    void operator()(MessageHandle message) {
      GRPC_STREAM_DATA_QUEUE_DEBUG << "Preparing message for sending";
      queue_.message_disassembler_.PrepareBatchedMessageForSending(
          std::move(message));
    }

    void operator()(GRPC_UNUSED HalfClosed half_closed) {
      GRPC_STREAM_DATA_QUEUE_DEBUG << "Preparing end of stream for sending";
      dequeue_flags_ |= kHalfCloseDequeued;
    }

    std::vector<Http2Frame> GetFrames() {
      // TODO(akshitpatel) : [PH2][P3] : There is a second option here. We can
      //  only append messages here. Additionally, when Trailing
      //  Metadata/HalfClose/ResetStream is dequeued, we can first flush the
      //  buffered messages and then append the respective frames. This will
      //  ensure that we do not break the ordering of the queue.

      // Order of appending frames is important. There may be scenarios where a
      // reset stream frames is appended after HalfClose or Trailing Metadata.
      MaybeAppendMessageFrames();
      MaybeAppendEndOfStreamFrame();
      MaybeAppendTrailingMetadataFrames();
      return std::move(frames_);
    }

    size_t GetTotalBytesConsumed() const { return total_bytes_consumed_; }
    size_t GetFlowControlTokensConsumed() const {
      return flow_control_tokens_consumed_;
    }
    uint8_t GetDequeueFlags() const { return dequeue_flags_; }

   private:
    inline void MaybeAppendInitialMetadataFrames() {
      while (queue_.initial_metadata_disassembler_.HasMoreData()) {
        GRPC_DCHECK(!(dequeue_flags_ & kHalfCloseDequeued));
        GRPC_DCHECK(!(dequeue_flags_ & kResetStreamDequeued));
        // TODO(akshitpatel) : [PH2][P2] : I do not think we need this.
        // HasMoreData() should be enough.
        bool is_end_headers = false;
        AppendFrame(queue_.initial_metadata_disassembler_.GetNextFrame(
            max_frame_length_, is_end_headers));
      }
    }

    inline void MaybeAppendTrailingMetadataFrames() {
      while (queue_.trailing_metadata_disassembler_.HasMoreData()) {
        GRPC_DCHECK(!(dequeue_flags_ & kHalfCloseDequeued));
        GRPC_DCHECK_EQ(queue_.message_disassembler_.GetBufferedLength(), 0u);
        GRPC_DCHECK_EQ(
            queue_.initial_metadata_disassembler_.GetBufferedLength(), 0u);
        // TODO(akshitpatel) : [PH2][P2] : I do not think we need this.
        // HasMoreData() should be enough.
        bool is_end_headers = false;
        AppendFrame(queue_.trailing_metadata_disassembler_.GetNextFrame(
            max_frame_length_, is_end_headers));
      }
    }

    inline void MaybeAppendEndOfStreamFrame() {
      if (dequeue_flags_ & kHalfCloseDequeued) {
        GRPC_DCHECK_EQ(queue_.message_disassembler_.GetBufferedLength(), 0u);
        GRPC_DCHECK_EQ(
            queue_.initial_metadata_disassembler_.GetBufferedLength(), 0u);
        GRPC_DCHECK_EQ(
            queue_.trailing_metadata_disassembler_.GetBufferedLength(), 0u);
        AppendFrame(Http2DataFrame{/*stream_id=*/queue_.stream_id_,
                                   /*end_stream=*/true,
                                   /*payload=*/SliceBuffer()});
      }
    }

    inline void MaybeAppendMessageFrames() {
      while (queue_.message_disassembler_.GetBufferedLength() > 0 &&
             (max_tokens_available_ - flow_control_tokens_consumed_) > 0) {
        GRPC_DCHECK_EQ(
            queue_.initial_metadata_disassembler_.GetBufferedLength(), 0u);
        Http2DataFrame frame = queue_.message_disassembler_.GenerateNextFrame(
            queue_.stream_id_,
            std::min(max_tokens_available_ - flow_control_tokens_consumed_,
                     max_frame_length_));
        flow_control_tokens_consumed_ += frame.payload.Length();
        GRPC_STREAM_DATA_QUEUE_DEBUG
            << "Appending message frame with length " << frame.payload.Length()
            << " Consumed tokens: " << flow_control_tokens_consumed_
            << " Max tokens: " << max_tokens_available_;
        AppendFrame(std::move(frame));
      }
    }

    inline void MaybeAppendResetStreamFrame() {
      if (dequeue_flags_ & kResetStreamDequeued) {
        // TODO(akshitpatel) : [PH2][P2] : Consider if we can send reset stream
        // frame without flushing all the messages enqueued until now.
        GRPC_DCHECK_EQ(queue_.message_disassembler_.GetBufferedLength(), 0u);
        GRPC_DCHECK_EQ(
            queue_.initial_metadata_disassembler_.GetBufferedLength(), 0u);
        GRPC_DCHECK_EQ(
            queue_.trailing_metadata_disassembler_.GetBufferedLength(), 0u);
        AppendFrame(Http2RstStreamFrame{queue_.stream_id_, error_code_});
      }
    }

    inline void AppendFrame(Http2Frame&& frame) {
      total_bytes_consumed_ += GetFrameMemoryUsage(frame);
      frames_.emplace_back(std::move(frame));
    }

    StreamDataQueue& queue_;
    const uint32_t max_frame_length_;
    const uint32_t max_tokens_available_;
    uint32_t flow_control_tokens_consumed_;
    uint32_t error_code_ = static_cast<uint32_t>(Http2ErrorCode::kNoError);
    std::vector<Http2Frame> frames_;
    HPackCompressor& encoder_;
    size_t total_bytes_consumed_ = 0u;
    uint8_t dequeue_flags_ = 0u;
  };

  // Updates the writable state and priority of the stream. MUST only be called
  // from the enqueue functions.
  // became_non_empty: True if the queue was empty and became non-empty as a
  //                   result of this enqueue operation.
  // priority: The new priority of the stream after this enqueue operation.
  // Returns the result of the enqueue operation
  //
  // High level flow:
  // Priority is simply updated to the new priority.
  // Writable state is updated as follows:
  // 1. If the stream was not writable before and became non-empty as a result
  //    of this enqueue operation, then the stream is marked as writable.
  // 2. If the stream was already writable before, it remains writable.
  // 3. The case where the stream was not writable before and the queue already
  //    contained data implies that the stream is blocked on stream flow control
  //    tokens. When the transport receives stream flow control window update,
  //    the stream is marked as writable.
  // For enqueue operations there is no easy way to query
  // stream_flow_control_tokens. So it is assumed that flow control tokens are
  // always available for an enqueue operation. This can cause a stream to be
  // marked as writable when it is not but this will correct itself in the next
  // dequeue operation (which returns an accurate is_writable).
  EnqueueResult UpdateWritableStateAndPriorityEnqueueLocked(
      const bool became_non_empty, const WritableStreamPriority priority)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu_) {
    // Update priority.
    priority_ = priority;

    // Update writable state.
    if (!is_writable_ && became_non_empty) {
      is_writable_ = true;
      GRPC_STREAM_DATA_QUEUE_DEBUG
          << "UpdateWritableStateLocked for stream id: " << stream_id_
          << " became writable with priority: "
          << GetWritableStreamPriorityString(priority_);
      return EnqueueResult{/*became_writable=*/true, priority_};
    }

    GRPC_STREAM_DATA_QUEUE_DEBUG
        << "UpdateWritableStateAndPriorityEnqueueLocked for stream id: "
        << stream_id_
        << " with priority: " << GetWritableStreamPriorityString(priority_)
        << " is_writable: " << is_writable_;
    return EnqueueResult{/*became_writable=*/false, priority_};
  }

  // Updates the writable state of the stream. Returns true if the
  // stream became writable.
  // Writable state is updated as follows:
  // 1. If the next message to dequeue is a grpc message, then the stream is
  //    writable if and only if we have available stream flow control tokens.
  // 2. If the next message to dequeue is not a grpc message, then the stream is
  //    writable if and only if the queue is not empty.
  // Unlike UpdateWritableStateAndPriorityEnqueueLocked, this function the
  // become_writable returned by this function is `accurate` as it considers
  // the whether the stream has bytes to write and the flow control tokens
  // available.
  bool UpdateWritableStateDequeueLocked(
      const uint32_t available_stream_fc_tokens)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu_) {
    // The stream is writable if the queue is not empty. If the next bytes to
    // write are part of a gRPC message, then check if we have stream flow
    // control tokens.
    is_writable_ = (!queue_.IsEmpty());

    // Next bytes to write are part of a gRPC message.
    if (message_disassembler_.GetBufferedLength() > 0 ||
        IsNextQueueEntryMessage()) {
      is_writable_ = (available_stream_fc_tokens > 0);
    }

    GRPC_STREAM_DATA_QUEUE_DEBUG
        << "UpdateWritableStateLocked for stream id: " << stream_id_
        << " with priority: " << GetWritableStreamPriorityString(priority_)
        << " is_writable: " << is_writable_;
    return is_writable_;
  }

  inline bool IsNextQueueEntryMessage() const {
    return (!queue_.IsEmpty() && queue_.GetNextEntryTokens().value() > 0);
  }

  // Handles the case where a reset stream is queued.
  // If a reset stream is queued or has been dequeued, this function returns a
  // DequeueResult. Otherwise, it returns std::nullopt.
  // This function must be called with mu_ held.
  std::optional<DequeueResult> HandleResetStreamLocked(
      const bool can_send_reset_stream) ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu_) {
    switch (reset_stream_state_) {
      case RstStreamState::kDequeued:
        GRPC_STREAM_DATA_QUEUE_DEBUG
            << "Reset stream is already dequeued for stream " << stream_id_
            << ". Returning empty frames.";
        GRPC_DCHECK(queue_.IsEmpty());
        is_writable_ = false;
        return DequeueResult{
            std::vector<Http2Frame>(),           is_writable_, priority_,
            /*total_bytes_consumed=*/0u,
            /*flow_control_tokens_consumed=*/0u, /*flags=*/0u};
      case RstStreamState::kQueued: {
        GRPC_STREAM_DATA_QUEUE_DEBUG
            << "Reset stream is queued. Skipping all frames (if any) for "
               "dequeuing "
            << stream_id_;
        is_writable_ = false;
        std::vector<Http2Frame> frames;
        uint8_t flags = 0u;
        if (can_send_reset_stream) {
          frames.emplace_back(
              Http2RstStreamFrame{stream_id_, reset_stream_error_code_});
          flags = kResetStreamDequeued;
        }
        queue_.Clear();
        reset_stream_state_ = RstStreamState::kDequeued;
        return DequeueResult{std::move(frames),
                             is_writable_,
                             priority_,
                             /*total_bytes_consumed=*/0u,
                             /*flow_control_tokens_consumed=*/0u,
                             flags};
      }
      case RstStreamState::kNotQueued:
        return std::nullopt;
      default:
        GRPC_CHECK(false) << "Invalid reset stream state: "
                          << static_cast<uint8_t>(reset_stream_state_);
    }
  }

  bool IsEnqueueClosed() const ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu_) {
    switch (reset_stream_state_) {
      case RstStreamState::kNotQueued:
        return false;
      case RstStreamState::kQueued:
      case RstStreamState::kDequeued:
        // This can happen when the transport tries to close the stream and the
        // stream is cancelled from the call stack.
        GRPC_STREAM_DATA_QUEUE_DEBUG
            << "Reset stream already queued for stream " << stream_id_;
        return true;
      default:
        GRPC_CHECK(false) << "Invalid reset stream state: "
                          << static_cast<uint8_t>(reset_stream_state_);
    }

    GPR_UNREACHABLE_CODE("Invalid reset stream state");
  }

  uint32_t stream_id_;
  const bool is_client_;

  enum class RstStreamState : uint8_t {
    kNotQueued = 0,
    kQueued,
    kDequeued,
  };

  // Accessed only during enqueue.
  bool is_initial_metadata_queued_ = false;
  bool is_trailing_metadata_or_half_close_queued_ = false;

  // Access both during enqueue and dequeue.
  Mutex mu_;
  // This variable tracks whether the stream is writable. 'Writable' represents
  // that the stream has bytes to send and the stream has flow control tokens
  // (if needed) to send them. This variable also has 1-1 correspondence with
  // whether the stream is in the list of writable streams in the transport.
  bool is_writable_ ABSL_GUARDED_BY(mu_) = false;
  RstStreamState reset_stream_state_ ABSL_GUARDED_BY(mu_) =
      RstStreamState::kNotQueued;
  SimpleQueue<QueueEntry> queue_;
  WritableStreamPriority priority_ ABSL_GUARDED_BY(mu_) =
      WritableStreamPriority::kDefault;
  uint32_t reset_stream_error_code_ ABSL_GUARDED_BY(mu_) =
      static_cast<uint32_t>(Http2ErrorCode::kNoError);

  // Accessed only during dequeue.
  HeaderDisassembler initial_metadata_disassembler_;
  HeaderDisassembler trailing_metadata_disassembler_;
  GrpcMessageDisassembler message_disassembler_;
};

}  // namespace http2
}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_EXT_TRANSPORT_CHTTP2_TRANSPORT_STREAM_DATA_QUEUE_H
