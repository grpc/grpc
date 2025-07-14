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
class Center : public RefCounted<Center<T>> {
 public:
  explicit Center(const uint32_t max_tokens) : max_tokens_(max_tokens) {}

  // Returns a promise that resolves when the data is enqueued.
  // It is expected that calls to this function are not done in parallel. At
  // most one call to this function should be pending at a time.
  auto Enqueue(T data, uint32_t tokens) {
    return [self = this->Ref(), tokens,
            data = std::move(data)]() mutable -> Poll<absl::Status> {
      MutexLock lock(&self->mu_);
      GRPC_STREAM_DATA_QUEUE_DEBUG << "Enqueueing data. Data tokens: "
                                   << tokens;
      // If tokens_consumed_ is 0 or the new tokens will fit within max_tokens_,
      // then allow the enqueue to go through. Otherwise, return pending. Here,
      // we are using tokens_consumed over queue_.empty() because there can
      // be enqueues with tokens = 0.
      if (self->tokens_consumed_ > 0 &&
          self->tokens_consumed_ > ((self->max_tokens_ >= tokens)
                                        ? self->max_tokens_ - tokens
                                        : 0)) {
        GRPC_STREAM_DATA_QUEUE_DEBUG
            << "Token threshold reached. Data tokens: " << tokens
            << " Tokens consumed: " << self->tokens_consumed_
            << " Max tokens: " << self->max_tokens_;
        self->waker_ = GetContext<Activity>()->MakeNonOwningWaker();
        return Pending{};
      }

      self->tokens_consumed_ += tokens;
      self->queue_.emplace(Entry{std::move(data), tokens});
      GRPC_STREAM_DATA_QUEUE_DEBUG
          << "Enqueue successful. Data tokens: " << tokens
          << " Current tokens consumed: " << self->tokens_consumed_;
      return absl::OkStatus();
    };
  }

  // Sync function to dequeue the next entry. Returns nullopt if the queue is
  // empty or if the front of the queue has more tokens than max_tokens.
  // When allow_oversized_dequeue parameter is set to true, it allows an item to
  // be dequeued even if its token cost is greater than max_tokens. It does not
  // cause the item itself to be partially dequeued; the entire item is always
  // returned.
  std::optional<T> Dequeue(uint32_t max_tokens, bool allow_oversized_dequeue) {
    ReleasableMutexLock lock(&mu_);
    if (queue_.empty() ||
        (queue_.front().tokens > max_tokens && !allow_oversized_dequeue)) {
      GRPC_STREAM_DATA_QUEUE_DEBUG
          << "Dequeueing data. Queue size: " << queue_.size()
          << " Max tokens: " << max_tokens << " Front tokens: "
          << (!queue_.empty() ? std::to_string(queue_.front().tokens)
                              : std::string("NA"))
          << " Allow oversized dequeue: " << allow_oversized_dequeue;
      return std::nullopt;
    }

    auto entry = std::move(queue_.front());
    queue_.pop();
    tokens_consumed_ -= entry.tokens;
    auto waker = std::move(waker_);
    lock.Release();

    // TODO(akshitpatel) : [PH2][P2] : Investigate a mechanism to only wake up
    // if the sender will be able to send more data. There is a high chance that
    // this queue is revamped soon and so not spending time on optimization
    // right now.
    waker.Wakeup();
    GRPC_STREAM_DATA_QUEUE_DEBUG
        << "Dequeue successful. Data tokens released: " << entry.tokens
        << " Current tokens consumed: " << tokens_consumed_;
    return std::move(entry.data);
  }

  bool IsEmpty() {
    MutexLock lock(&mu_);
    return (queue_.empty() && tokens_consumed_ == 0);
  }

 private:
  struct Entry {
    T data;
    uint32_t tokens;
  };
  Mutex mu_;
  std::queue<Entry> queue_ ABSL_GUARDED_BY(mu_);
  uint32_t max_tokens_ ABSL_GUARDED_BY(mu_);
  uint32_t tokens_consumed_ ABSL_GUARDED_BY(mu_) = 0;
  Waker waker_ ABSL_GUARDED_BY(mu_);
};

template <typename T>
class SimpleQueue {
 public:
  explicit SimpleQueue(uint32_t max_tokens)
      : center_(MakeRefCounted<Center<T>>(max_tokens)) {}

  SimpleQueue(SimpleQueue&& rhs) = default;
  SimpleQueue& operator=(SimpleQueue&& rhs) = default;
  SimpleQueue(const SimpleQueue&) = delete;
  SimpleQueue& operator=(const SimpleQueue&) = delete;

  auto Enqueue(T data, uint32_t tokens) {
    return center_->Enqueue(std::move(data), tokens);
  }

  std::optional<T> Dequeue(uint32_t max_tokens, bool allow_oversized_dequeue) {
    return center_->Dequeue(max_tokens, allow_oversized_dequeue);
  }

  // Dequeues the next entry immediately ignoring the tokens. If the queue is
  // empty, returns nullopt.
  std::optional<T> ImmediateDequeue() {
    return center_->Dequeue(std::numeric_limits<uint32_t>::max(), true);
  }

  bool TestOnlyIsEmpty() { return center_->IsEmpty(); }

 private:
  RefCountedPtr<Center<T>> center_;
};

class StreamDataQueue {
 public:
  ~StreamDataQueue() = default;

  StreamDataQueue(StreamDataQueue&& rhs) = delete;
  StreamDataQueue& operator=(StreamDataQueue&& rhs) = delete;
  StreamDataQueue(const StreamDataQueue&) = delete;
  StreamDataQueue& operator=(const StreamDataQueue&) = delete;

 private:
};

}  // namespace http2
}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_EXT_TRANSPORT_CHTTP2_TRANSPORT_STREAM_DATA_QUEUE_H
