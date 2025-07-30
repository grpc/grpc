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

namespace grpc_core {
namespace http2 {

#define GRPC_STREAM_DATA_QUEUE_DEBUG VLOG(2)

template <typename T>
class SimpleQueue {
 public:
  struct EnqueueResult {
    absl::Status status;
    bool became_non_empty;
  };

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
  // metadata as flow control does not apply to them.
  auto Enqueue(T& data, const uint32_t tokens) {
    return PollEnqueue(data, tokens);
  }

  // Sync function to dequeue the next entry. Returns nullopt if the queue is
  // empty or if the front of the queue has more tokens than
  // allowed_dequeue_tokens. When allow_oversized_dequeue parameter is set to
  // true, it allows an item to be dequeued even if its token cost is greater
  // than allowed_dequeue_tokens. It does not cause the item itself to be
  // partially dequeued; either the entire item is returned or nullopt is
  // returned.
  std::optional<T> Dequeue(const uint32_t allowed_dequeue_tokens,
                           const bool allow_oversized_dequeue) {
    return DequeueInternal(allowed_dequeue_tokens, allow_oversized_dequeue);
  }

  // Dequeues the next entry immediately ignoring the tokens. If the queue is
  // empty, returns nullopt.
  std::optional<T> ImmediateDequeue() {
    return DequeueInternal(std::numeric_limits<uint32_t>::max(), true);
  }

  bool TestOnlyIsEmpty() const { return IsEmpty(); }

 private:
  Poll<EnqueueResult> PollEnqueue(T& data, const uint32_t tokens) {
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
      return EnqueueResult{absl::OkStatus(),
                           /*became_non_empty=*/queue_.size() == 1};
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

template <typename MetadataHandle>
class StreamDataQueue : public RefCounted<StreamDataQueue<MetadataHandle>> {
 public:
  explicit StreamDataQueue(const bool is_client, const uint32_t stream_id,
                           const uint32_t queue_size)
      : stream_id_(stream_id),
        is_client_(is_client),
        queue_(queue_size),
        initial_metadata_disassembler_(stream_id,
                                       /*is_trailing_metadata=*/false),
        trailing_metadata_disassembler_(stream_id,
                                        /*is_trailing_metadata=*/true) {};
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
  // 5. The trailing metadata/HalfClose/ResetStream MUST be the final thing that
  //    is queued.
  // 6. Initial Metadata and Trailing Metadata MUST be queued at most once per
  //    stream.
  // 7. Currently initial metadata is not expected to be enqueued with
  //    end_stream set. If the stream needs to be half closed, the client should
  //    enqueue a half close message.

  // Enqueue Initial Metadata.
  // 1. MUST be called at most once.
  // 2. This MUST be called before any messages are enqueued.
  // 3. MUST not be called after trailing metadata is enqueued.
  auto EnqueueInitialMetadata(MetadataHandle metadata) {
    DCHECK(!is_initial_metadata_queued_);
    DCHECK(!is_trailing_metadata_queued_);
    DCHECK(metadata != nullptr);
    DCHECK(!is_end_stream_);

    is_initial_metadata_queued_ = true;
    return [self = this->Ref(),
            entry = QueueEntry{InitialMetadataType{
                std::move(metadata)}}]() mutable -> Poll<absl::Status> {
      MutexLock lock(&self->mu_);
      auto result = self->queue_.Enqueue(entry, /*tokens=*/0);
      if (result.ready()) {
        GRPC_STREAM_DATA_QUEUE_DEBUG << "Enqueued initial metadata for stream "
                                     << self->stream_id_;
        // TODO(akshitpatel) : [PH2][P2] : Add the logic to set the is_writable
        // flag.
        return result.value().status;
      }
      return Pending{};
    };
  }

  // Enqueue Trailing Metadata.
  // 1. MUST be called at most once.
  // 2. No other enqueue functions can be called after this.
  auto EnqueueTrailingMetadata(MetadataHandle metadata) {
    DCHECK(metadata != nullptr);
    DCHECK(!is_end_stream_);
    DCHECK(!is_client_);

    is_trailing_metadata_queued_ = true;
    is_end_stream_ = true;
    return [self = this->Ref(),
            entry = QueueEntry{TrailingMetadataType{
                std::move(metadata)}}]() mutable -> Poll<absl::Status> {
      MutexLock lock(&self->mu_);
      auto result = self->queue_.Enqueue(entry, /*tokens=*/0);
      if (result.ready()) {
        GRPC_STREAM_DATA_QUEUE_DEBUG << "Enqueued trailing metadata for stream "
                                     << self->stream_id_;
        // TODO(akshitpatel) : [PH2][P2] : Add the logic to set the is_writable
        // flag.
        return result.value().status;
      }
      return Pending{};
    };
  }

  // Returns a promise that resolves when the message is enqueued. There may be
  // delays in queueing the message if data queue is full.
  // 1. MUST be called after initial metadata is enqueued.
  // 2. MUST not be called after trailing metadata is enqueued.
  auto EnqueueMessage(MessageHandle message) {
    DCHECK(is_initial_metadata_queued_);
    DCHECK(message != nullptr);
    DCHECK(!is_end_stream_);
    DCHECK_LE(message->payload()->Length(),
              std::numeric_limits<uint32_t>::max() - kGrpcHeaderSizeInBytes);

    const uint32_t tokens =
        message->payload()->Length() + kGrpcHeaderSizeInBytes;
    return [self = this->Ref(), entry = QueueEntry{std::move(message)},
            tokens]() mutable -> Poll<absl::Status> {
      MutexLock lock(&self->mu_);
      auto result = self->queue_.Enqueue(entry, tokens);
      if (result.ready()) {
        GRPC_STREAM_DATA_QUEUE_DEBUG << "Enqueued message for stream "
                                     << self->stream_id_;
        // TODO(akshitpatel) : [PH2][P2] : Add the logic to set the is_writable
        // flag.
        return result.value().status;
      }
      return Pending{};
    };
  }

  auto EnqueueHalfClosed() {
    DCHECK(is_initial_metadata_queued_);
    DCHECK(is_client_);
    DCHECK(!is_end_stream_);

    is_end_stream_ = true;
    return [self = this->Ref(),
            entry = QueueEntry{HalfClosed{}}]() mutable -> Poll<absl::Status> {
      MutexLock lock(&self->mu_);
      auto result = self->queue_.Enqueue(entry, /*tokens=*/0);
      if (result.ready()) {
        GRPC_STREAM_DATA_QUEUE_DEBUG << "Marked stream " << self->stream_id_
                                     << " as half closed";
        // TODO(akshitpatel) : [PH2][P2] : Add the logic to set the is_writable
        // flag.
        return result.value().status;
      }
      return Pending{};
    };
  }

  auto EnqueueResetStream(uint32_t error_code) {
    DCHECK(is_initial_metadata_queued_);
    DCHECK(!is_end_stream_);

    is_end_stream_ = true;
    return [self = this->Ref(),
            entry = QueueEntry{
                ResetStream{error_code}}]() mutable -> Poll<absl::Status> {
      MutexLock lock(&self->mu_);
      auto result = self->queue_.Enqueue(entry, /*tokens=*/0);
      if (result.ready()) {
        GRPC_STREAM_DATA_QUEUE_DEBUG << "Enqueued reset stream for stream "
                                     << self->stream_id_;
        // TODO(akshitpatel) : [PH2][P2] : Add the logic to set the is_writable
        // flag.
        return result.value().status;
      }
      return Pending{};
    };
  }

  //////////////////////////////////////////////////////////////////////////////
  // Dequeue Helpers
  std::vector<Http2Frame> DequeueFrames(uint32_t max_tokens,
                                        uint32_t max_frame_length);

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
  const uint32_t stream_id_;
  const bool is_client_;

  // Accessed only during enqueue.
  bool is_initial_metadata_queued_ = false;
  bool is_trailing_metadata_queued_ = false;
  bool is_end_stream_ = false;

  // Access both during enqueue and dequeue.
  Mutex mu_;
  SimpleQueue<QueueEntry> queue_;

  // Accessed only during dequeue.
  HeaderDisassembler initial_metadata_disassembler_;
  HeaderDisassembler trailing_metadata_disassembler_;
};

}  // namespace http2
}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_EXT_TRANSPORT_CHTTP2_TRANSPORT_STREAM_DATA_QUEUE_H
