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

#define STREAM_DATA_QUEUE_DEBUG VLOG(2)

template <typename T>
class SimpleQueue {
 public:
  explicit SimpleQueue(uint32_t max_tokens) : max_tokens_(max_tokens) {}
  ~SimpleQueue() = default;

  SimpleQueue(SimpleQueue&&) = default;
  SimpleQueue& operator=(SimpleQueue&&) = default;
  SimpleQueue(const SimpleQueue&) = delete;
  SimpleQueue& operator=(const SimpleQueue&) = delete;

  // Returns a promise that resolves when the data is enqueued.
  // It is expected that calls to this function are not done in parallel. At
  // most one call to this function should be pending at a time.
  auto Enqueue(T data, uint32_t tokens) {
    return
        [this, data = std::move(data), tokens]() mutable -> Poll<StatusFlag> {
          MutexLock lock(&mu_);
          STREAM_DATA_QUEUE_DEBUG << "Enqueueing data. Data tokens: " << tokens;
          if (!queue_.empty() &&
              tokens_consumed_ >
                  ((max_tokens_ >= tokens) ? max_tokens_ - tokens : 0)) {
            STREAM_DATA_QUEUE_DEBUG
                << "Token threshold reached. Data tokens: " << tokens
                << " Tokens consumed: " << tokens_consumed_
                << " Max tokens: " << max_tokens_;
            waker_ = GetContext<Activity>()->MakeNonOwningWaker();
            return Pending{};
          }

          tokens_consumed_ += tokens;
          queue_.emplace(Entry{std::move(data), tokens});
          STREAM_DATA_QUEUE_DEBUG
              << "Enqueue successful. Data tokens: " << tokens
              << " Current tokens consumed: " << tokens_consumed_;
          return Success{};
        };
  }

  // Sync function to dequeue the next entry. Returns nullopt if the queue is
  // empty.
  std::optional<T> Dequeue(uint32_t max_tokens, bool allow_partial_dequeue) {
    ReleasableMutexLock lock(&mu_);
    if (queue_.empty() ||
        (queue_.front().tokens > max_tokens && !allow_partial_dequeue)) {
      STREAM_DATA_QUEUE_DEBUG
          << "Dequeueing data. Queue size: " << queue_.size()
          << " Max tokens: " << max_tokens << " Front tokens: "
          << (!queue_.empty() ? std::to_string(queue_.front().tokens)
                              : std::string("NA"))
          << " Allow partial dequeue: " << allow_partial_dequeue;
      return std::nullopt;
    }

    auto entry = std::move(queue_.front());
    queue_.pop();
    tokens_consumed_ -= entry.tokens;
    auto waker = std::move(waker_);
    lock.Release();

    // TODO(akshitpatel) : [PH2][P0] : Investigate a mechanism to only wake up
    // if the sender will be able to send more data.
    waker.Wakeup();
    STREAM_DATA_QUEUE_DEBUG
        << "Dequeue successful. Data tokens released: " << entry.tokens
        << " Current tokens consumed: " << tokens_consumed_;
    return std::move(entry.data);
  }

  std::optional<T> Dequeue() {
    ReleasableMutexLock lock(&mu_);
    if (queue_.empty()) {
      return std::nullopt;
    }

    auto entry = std::move(queue_.front());
    queue_.pop();
    tokens_consumed_ -= entry.tokens;
    auto waker = std::move(waker_);
    lock.Release();

    waker.Wakeup();
    return std::move(entry.data);
  }

  bool TestOnlyIsEmpty() {
    MutexLock lock(&mu_);
    return queue_.empty();
  }

  std::optional<uint32_t> TestOnlyPeekTokens() {
    MutexLock lock(&mu_);
    if (queue_.empty()) {
      return std::nullopt;
    }

    return queue_.front().tokens;
  }

 private:
  struct Entry {
    T data;
    uint32_t tokens;
  };
  Mutex mu_;
  std::queue<Entry> queue_ ABSL_GUARDED_BY(mu_);
  uint32_t max_tokens_;
  uint32_t tokens_consumed_ = 0;
  // TODO(akshitpatel) : [PH2][P0] : Can we get away with IntraActivity waker?
  Waker waker_;
};

class StreamDataQueue {
 public:
  ~StreamDataQueue() = default;

  StreamDataQueue(StreamDataQueue&& rhs) = delete;
  StreamDataQueue& operator=(StreamDataQueue&& rhs) = delete;
  StreamDataQueue(const StreamDataQueue&) = delete;
  StreamDataQueue& operator=(const StreamDataQueue&) = delete;

 private:
  // TODO(akshitpatel) : [PH2][P1] Keep this either in the transport or here.
  // Not both places.
  GrpcMessageDisassembler disassembler;
  SimpleQueue<MessageHandle> msg_queue_;
};

}  // namespace http2
}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_EXT_TRANSPORT_CHTTP2_TRANSPORT_STREAM_DATA_QUEUE_H
