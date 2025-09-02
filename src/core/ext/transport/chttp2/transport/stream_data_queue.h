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

#include <queue>

#include "absl/log/check.h"
#include "absl/log/log.h"
#include "src/core/ext/transport/chttp2/transport/header_assembler.h"
#include "src/core/ext/transport/chttp2/transport/message_assembler.h"
#include "src/core/ext/transport/chttp2/transport/writable_streams.h"

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
  Poll<absl::StatusOr<bool>> Enqueue(T& data, const uint32_t tokens) {
    return PollEnqueue(data, tokens);
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
  bool TestOnlyIsEmpty() const { return IsEmpty(); }

 private:
  Poll<absl::StatusOr<bool>> PollEnqueue(T& data, const uint32_t tokens) {
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
      return /*became_non_empty*/ (queue_.size() == 1);
    }

    GRPC_STREAM_DATA_QUEUE_DEBUG
        << "Token threshold reached. Data tokens: " << tokens
        << " Tokens consumed: " << tokens_consumed_
        << " Max tokens: " << max_tokens_;
    waker_ = GetContext<Activity>()->MakeNonOwningWaker();
    return Pending{};
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

  bool IsEmpty() const { return queue_.empty(); }

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
  uint32_t max_tokens_;
  // The number of tokens that have been enqueued in the queue but not yet
  // dequeued.
  uint32_t tokens_consumed_ = 0;
  Waker waker_;
};

// StreamDataQueue is a thread safe.
// Note: StreamDataQueue is a single producer single
// consumer queue.
template <typename MetadataHandle>
class StreamDataQueue : public RefCounted<StreamDataQueue<MetadataHandle>> {
 public:
  explicit StreamDataQueue(const bool is_client, const uint32_t stream_id,
                           const uint32_t queue_size,
                           bool allow_true_binary_metadata)
      : stream_id_(stream_id),
        is_client_(is_client),
        queue_(queue_size),
        initial_metadata_disassembler_(stream_id,
                                       /*is_trailing_metadata=*/false,
                                       allow_true_binary_metadata),
        trailing_metadata_disassembler_(stream_id,
                                        /*is_trailing_metadata=*/true,
                                        allow_true_binary_metadata) {};
  ~StreamDataQueue() = default;

  StreamDataQueue(StreamDataQueue&& rhs) = delete;
  StreamDataQueue& operator=(StreamDataQueue&& rhs) = delete;
  StreamDataQueue(const StreamDataQueue&) = delete;
  StreamDataQueue& operator=(const StreamDataQueue&) = delete;

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
    WritableStreams::StreamPriority priority;
  };

  // Enqueue Initial Metadata.
  // 1. MUST be called at most once.
  // 2. This MUST be called before any messages are enqueued.
  // 3. MUST not be called after trailing metadata is enqueued.
  // 4. This function is thread safe.
  auto EnqueueInitialMetadata(MetadataHandle&& metadata) {
    DCHECK(!is_initial_metadata_queued_);
    DCHECK(!is_trailing_metadata_or_half_close_queued_);
    DCHECK(metadata != nullptr);
    DCHECK(!is_reset_stream_queued_);

    is_initial_metadata_queued_ = true;
    return [self = this->Ref(),
            entry = QueueEntry{InitialMetadataType{std::move(
                metadata)}}]() mutable -> Poll<absl::StatusOr<EnqueueResult>> {
      MutexLock lock(&self->mu_);
      Poll<absl::StatusOr<bool>> result =
          self->queue_.Enqueue(entry, /*tokens=*/0);
      if (result.ready()) {
        GRPC_STREAM_DATA_QUEUE_DEBUG
            << "Enqueued initial metadata for stream " << self->stream_id_
            << " with status: " << result.value().status();
        if (result.value().ok()) {
          DCHECK(/*became_non_empty*/ result.value().value());
          return self->UpdateWritableStateLocked(
              /*became_non_empty*/ result.value().value(),
              WritableStreams::StreamPriority::kDefault);
        }
        return result.value().status();
      }
      return Pending{};
    };
  }

  // Enqueue Trailing Metadata.
  // 1. MUST be called at most once.
  // 2. MUST be called only for a server.
  // 3. This function is thread safe.
  auto EnqueueTrailingMetadata(MetadataHandle&& metadata) {
    DCHECK(metadata != nullptr);
    DCHECK(!is_reset_stream_queued_);
    DCHECK(!is_client_);
    DCHECK(!is_trailing_metadata_or_half_close_queued_);

    is_trailing_metadata_or_half_close_queued_ = true;
    return [self = this->Ref(),
            entry = QueueEntry{TrailingMetadataType{std::move(
                metadata)}}]() mutable -> Poll<absl::StatusOr<EnqueueResult>> {
      MutexLock lock(&self->mu_);
      Poll<absl::StatusOr<bool>> result =
          self->queue_.Enqueue(entry, /*tokens=*/0);
      if (result.ready()) {
        GRPC_STREAM_DATA_QUEUE_DEBUG
            << "Enqueued trailing metadata for stream " << self->stream_id_
            << " with status: " << result.value().status();
        if (result.value().ok()) {
          return self->UpdateWritableStateLocked(
              /*became_non_empty*/ result.value().value(),
              WritableStreams::StreamPriority::kStreamClosed);
        }
        return result.value().status();
      }
      return Pending{};
    };
  }

  // Returns a promise that resolves when the message is enqueued. There may be
  // delays in queueing the message if data queue is full.
  // 1. MUST be called after initial metadata is enqueued.
  // 2. MUST not be called after trailing metadata is enqueued.
  // 3. This function is thread safe.
  auto EnqueueMessage(MessageHandle&& message) {
    DCHECK(is_initial_metadata_queued_);
    DCHECK(message != nullptr);
    DCHECK(!is_reset_stream_queued_);
    DCHECK_LE(message->payload()->Length(),
              std::numeric_limits<uint32_t>::max() - kGrpcHeaderSizeInBytes);
    DCHECK(!is_trailing_metadata_or_half_close_queued_);

    const uint32_t tokens =
        message->payload()->Length() + kGrpcHeaderSizeInBytes;
    return [self = this->Ref(), entry = QueueEntry{std::move(message)},
            tokens]() mutable -> Poll<absl::StatusOr<EnqueueResult>> {
      MutexLock lock(&self->mu_);
      Poll<absl::StatusOr<bool>> result = self->queue_.Enqueue(entry, tokens);
      if (result.ready()) {
        GRPC_STREAM_DATA_QUEUE_DEBUG
            << "Enqueued message for stream " << self->stream_id_
            << " with status: " << result.value().status();
        // TODO(akshitpatel) : [PH2][P2] : Add check for flow control tokens.
        if (result.value().ok()) {
          return self->UpdateWritableStateLocked(
              /*became_non_empty*/ result.value().value(),
              WritableStreams::StreamPriority::kDefault);
        }
        return result.value().status();
      }
      return Pending{};
    };
  }

  // Enqueue Half Closed.
  // 1. MUST be called at most once.
  // 2. MUST be called only for a client.
  // 3. This function is thread safe.
  auto EnqueueHalfClosed() {
    DCHECK(is_initial_metadata_queued_);
    DCHECK(is_client_);
    DCHECK(!is_reset_stream_queued_);
    DCHECK(!is_trailing_metadata_or_half_close_queued_);

    is_trailing_metadata_or_half_close_queued_ = true;
    return [self = this->Ref(), entry = QueueEntry{HalfClosed{}}]() mutable
               -> Poll<absl::StatusOr<EnqueueResult>> {
      MutexLock lock(&self->mu_);
      Poll<absl::StatusOr<bool>> result =
          self->queue_.Enqueue(entry, /*tokens=*/0);
      if (result.ready()) {
        GRPC_STREAM_DATA_QUEUE_DEBUG
            << "Marking stream " << self->stream_id_ << " as half closed"
            << " with status: " << result.value().status();
        if (result.value().ok()) {
          return self->UpdateWritableStateLocked(
              /*became_non_empty*/ result.value().value(),
              WritableStreams::StreamPriority::kStreamClosed);
        }
        return result.value().status();
      }
      return Pending{};
    };
  }

  // Enqueue Reset Stream.
  // 1. MUST be called at most once.
  // 3. This function is thread safe.
  auto EnqueueResetStream(uint32_t error_code) {
    DCHECK(is_initial_metadata_queued_);
    DCHECK(!is_reset_stream_queued_);

    is_reset_stream_queued_ = true;
    return [self = this->Ref(),
            entry = QueueEntry{ResetStream{
                error_code}}]() mutable -> Poll<absl::StatusOr<EnqueueResult>> {
      MutexLock lock(&self->mu_);
      Poll<absl::StatusOr<bool>> result =
          self->queue_.Enqueue(entry, /*tokens=*/0);
      if (result.ready()) {
        GRPC_STREAM_DATA_QUEUE_DEBUG
            << "Enqueueing reset stream for stream " << self->stream_id_
            << " with status: " << result.value().status();
        if (result.value().ok()) {
          return self->UpdateWritableStateLocked(
              /*became_non_empty*/ result.value().value(),
              WritableStreams::StreamPriority::kStreamClosed);
        }
        return result.value().status();
      }
      return Pending{};
    };
  }

  //////////////////////////////////////////////////////////////////////////////
  // Dequeue Helpers

  // TODO(akshitpatel) : [PH2][P2] : Decide on whether it is needed to return
  // the number of tokens consumed by one call of this function.
  struct DequeueResult {
    std::vector<Http2Frame> frames;
    bool is_writable;
    WritableStreams::StreamPriority priority;
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
  absl::StatusOr<DequeueResult> DequeueFrames(const uint32_t max_fc_tokens,
                                              const uint32_t max_frame_length,
                                              HPackCompressor& encoder) {
    MutexLock lock(&mu_);
    GRPC_STREAM_DATA_QUEUE_DEBUG << "Dequeueing frames for stream "
                                 << stream_id_
                                 << " Max fc tokens: " << max_fc_tokens
                                 << " Max frame length: " << max_frame_length
                                 << " Message disassembler buffered length: "
                                 << message_disassembler_.GetBufferedLength();

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

    // TODO(akshitpatel) : [PH2][P2] : Add a check for flow control tokens.
    is_writable_ = false;
    GRPC_STREAM_DATA_QUEUE_DEBUG << "Stream id: " << stream_id_
                                 << " writable state changed to "
                                 << is_writable_;
    return DequeueResult{handle_dequeue.GetFrames(), is_writable_, priority_};
  }

  // Returns true if the queue is empty. This function is thread safe.
  bool TestOnlyIsEmpty() {
    MutexLock lock(&mu_);
    return queue_.TestOnlyIsEmpty();
  }

 private:
  struct InitialMetadataType {
    MetadataHandle metadata;
  };
  struct TrailingMetadataType {
    MetadataHandle metadata;
  };
  struct HalfClosed {};
  struct ResetStream {
    uint32_t error_code;
  };
  using QueueEntry = std::variant<InitialMetadataType, TrailingMetadataType,
                                  MessageHandle, HalfClosed, ResetStream>;

  class HandleDequeue {
   public:
    HandleDequeue(uint32_t max_tokens, uint32_t max_frame_length,
                  HPackCompressor& encoder, StreamDataQueue& queue)
        : queue_(queue),
          max_frame_length_(max_frame_length),
          max_fc_tokens_(max_tokens),
          fc_tokens_available_(max_tokens),
          encoder_(encoder) {}

    void operator()(InitialMetadataType initial_metadata) {
      GRPC_STREAM_DATA_QUEUE_DEBUG << "Preparing initial metadata for sending";
      queue_.initial_metadata_disassembler_.PrepareForSending(
          std::move(initial_metadata.metadata), encoder_);
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
      is_half_closed_ = true;
    }

    void operator()(ResetStream reset_stream) {
      GRPC_STREAM_DATA_QUEUE_DEBUG << "Preparing reset stream for sending";
      is_reset_stream_ = true;
      error_code_ = reset_stream.error_code;
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
      MaybeAppendResetStreamFrame();
      return std::move(frames_);
    }

   private:
    inline void MaybeAppendInitialMetadataFrames() {
      while (queue_.initial_metadata_disassembler_.HasMoreData()) {
        DCHECK(!is_half_closed_);
        DCHECK(!is_reset_stream_);
        // TODO(akshitpatel) : [PH2][P2] : I do not think we need this.
        // HasMoreData() should be enough.
        bool is_end_headers = false;
        frames_.emplace_back(queue_.initial_metadata_disassembler_.GetNextFrame(
            max_frame_length_, is_end_headers));
      }
    }

    inline void MaybeAppendTrailingMetadataFrames() {
      while (queue_.trailing_metadata_disassembler_.HasMoreData()) {
        DCHECK(!is_half_closed_);
        DCHECK_EQ(queue_.message_disassembler_.GetBufferedLength(), 0u);
        DCHECK_EQ(queue_.initial_metadata_disassembler_.GetBufferedLength(),
                  0u);
        // TODO(akshitpatel) : [PH2][P2] : I do not think we need this.
        // HasMoreData() should be enough.
        bool is_end_headers = false;
        frames_.emplace_back(
            queue_.trailing_metadata_disassembler_.GetNextFrame(
                max_frame_length_, is_end_headers));
      }
    }

    inline void MaybeAppendEndOfStreamFrame() {
      if (is_half_closed_) {
        DCHECK_EQ(queue_.message_disassembler_.GetBufferedLength(), 0u);
        DCHECK_EQ(queue_.initial_metadata_disassembler_.GetBufferedLength(),
                  0u);
        DCHECK_EQ(queue_.trailing_metadata_disassembler_.GetBufferedLength(),
                  0u);
        frames_.emplace_back(Http2DataFrame{/*stream_id=*/queue_.stream_id_,
                                            /*end_stream=*/true,
                                            /*payload=*/SliceBuffer()});
      }
    }

    inline void MaybeAppendMessageFrames() {
      while (queue_.message_disassembler_.GetBufferedLength() > 0 &&
             fc_tokens_available_ > 0) {
        DCHECK_EQ(queue_.initial_metadata_disassembler_.GetBufferedLength(),
                  0u);
        Http2DataFrame frame = queue_.message_disassembler_.GenerateNextFrame(
            queue_.stream_id_,
            std::min(fc_tokens_available_, max_frame_length_));
        fc_tokens_available_ -= frame.payload.Length();
        GRPC_STREAM_DATA_QUEUE_DEBUG
            << "Appending message frame with length " << frame.payload.Length()
            << "Available tokens: " << fc_tokens_available_;
        frames_.emplace_back(std::move(frame));
      }
    }

    inline void MaybeAppendResetStreamFrame() {
      if (is_reset_stream_) {
        // TODO(akshitpatel) : [PH2][P2] : Consider if we can send reset stream
        // frame without flushing all the messages enqueued until now.
        DCHECK_EQ(queue_.message_disassembler_.GetBufferedLength(), 0u);
        DCHECK_EQ(queue_.initial_metadata_disassembler_.GetBufferedLength(),
                  0u);
        DCHECK_EQ(queue_.trailing_metadata_disassembler_.GetBufferedLength(),
                  0u);
        frames_.emplace_back(
            Http2RstStreamFrame{queue_.stream_id_, error_code_});
      }
    }

    StreamDataQueue& queue_;
    const uint32_t max_frame_length_;
    const uint32_t max_fc_tokens_;
    uint32_t fc_tokens_available_;
    bool is_half_closed_ = false;
    bool is_reset_stream_ = false;
    uint32_t error_code_ = static_cast<uint32_t>(Http2ErrorCode::kNoError);
    std::vector<Http2Frame> frames_;
    HPackCompressor& encoder_;
  };

  // Updates the stream priority. Also sets the writable state to true if the
  // stream has become writable. Returns if the stream became writable and
  // updated priority. It is expected that the caller will hold the lock on the
  // queue when calling this function.
  EnqueueResult UpdateWritableStateLocked(
      const bool became_non_empty,
      const WritableStreams::StreamPriority priority)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu_) {
    priority_ = priority;
    if (!is_writable_ && became_non_empty) {
      is_writable_ = true;
      GRPC_STREAM_DATA_QUEUE_DEBUG
          << "UpdateWritableStateLocked for stream id: " << stream_id_
          << " became writable with priority: "
          << WritableStreams::GetPriorityString(priority_);
      return EnqueueResult{/*became_writable=*/true, priority_};
    }

    GRPC_STREAM_DATA_QUEUE_DEBUG
        << "UpdateWritableStateLocked for stream id: " << stream_id_
        << " with priority: " << WritableStreams::GetPriorityString(priority_)
        << " is_writable: " << is_writable_;
    return EnqueueResult{/*became_writable=*/false, priority_};
  }

  const uint32_t stream_id_;
  const bool is_client_;

  // Accessed only during enqueue.
  bool is_initial_metadata_queued_ = false;
  bool is_trailing_metadata_or_half_close_queued_ = false;
  bool is_reset_stream_queued_ = false;

  // Access both during enqueue and dequeue.
  Mutex mu_;
  bool is_writable_ ABSL_GUARDED_BY(mu_) = false;
  SimpleQueue<QueueEntry> queue_;
  WritableStreams::StreamPriority priority_ ABSL_GUARDED_BY(mu_) =
      WritableStreams::StreamPriority::kDefault;

  // Accessed only during dequeue.
  HeaderDisassembler initial_metadata_disassembler_;
  HeaderDisassembler trailing_metadata_disassembler_;
  GrpcMessageDisassembler message_disassembler_;
};

}  // namespace http2
}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_EXT_TRANSPORT_CHTTP2_TRANSPORT_STREAM_DATA_QUEUE_H
