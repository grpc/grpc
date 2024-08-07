// Copyright 2024 gRPC authors.
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

#ifndef GRPC_SRC_CORE_CALL_REQUEST_BUFFER_H
#define GRPC_SRC_CORE_CALL_REQUEST_BUFFER_H

#include <utility>

#include "src/core/lib/promise/wait_set.h"
#include "src/core/lib/transport/call_spine.h"
#include "src/core/lib/transport/message.h"
#include "src/core/lib/transport/metadata.h"

namespace grpc_core {

class RequestBuffer {
 public:
  class Reader {
   public:
    explicit Reader(RequestBuffer* buffer) : buffer_(buffer) {}
    ~Reader();

    Reader(const Reader&) = delete;
    Reader& operator=(const Reader&) = delete;

    GRPC_MUST_USE_RESULT auto PullClientInitialMetadata() {
      return [this]() { return PollPullClientInitialMetadata(); };
    }
    GRPC_MUST_USE_RESULT auto PullMessage() {
      return [this]() { return PollPullMessage(); };
    }

    absl::Status TakeError() { return std::move(error_); }

   private:
    Poll<ValueOrFailure<ClientMetadataHandle>> PollPullClientInitialMetadata();
    Poll<ValueOrFailure<absl::optional<MessageHandle>>> PollPullMessage();

    template <typename T>
    T ClaimObject(T& object) {
      if (buffer_->winner_ == this) return std::move(object);
      return object->Copy();
    }

    RequestBuffer* const buffer_;
    size_t message_index_ = 0;
    absl::Status error_;
  };

  StatusFlag PushClientInitialMetadata(ClientMetadataHandle md);
  // Resolves to a ValueOrFailure<size_t> where the size_t is the amount of data
  // buffered (or 0 if we're in streaming mode).
  GRPC_MUST_USE_RESULT auto PushMessage(MessageHandle message) {
    return [this, message = std::move(message)]() mutable {
      return PollPushMessage(message);
    };
  }
  StatusFlag FinishSends();
  void Cancel(absl::Status error = absl::CancelledError());

  void SwitchToStreaming(Reader* winner);

 private:
  struct Buffering {
    ClientMetadataHandle initial_metadata;
    absl::InlinedVector<MessageHandle, 1> messages;
    size_t buffered = 0;
  };
  struct Buffered {
    Buffered(ClientMetadataHandle md,
             absl::InlinedVector<MessageHandle, 1> msgs)
        : initial_metadata(std::move(md)), messages(std::move(msgs)) {}
    ClientMetadataHandle initial_metadata;
    absl::InlinedVector<MessageHandle, 1> messages;
  };
  struct Streaming {
    MessageHandle message;
    bool end_of_stream;
  };
  struct Cancelled {
    explicit Cancelled(absl::Status error) : error(std::move(error)) {}
    absl::Status error;
  };
  using State = absl::variant<Buffering, Buffered, Streaming, Cancelled>;

  Poll<ValueOrFailure<size_t>> PollPushMessage(MessageHandle& message);
  Pending PendingPull() ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu_) {
    return pull_waiters_.AddPending(Activity::current()->MakeOwningWaker());
  }
  Pending PendingPush() ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu_) {
    push_waker_ = Activity::current()->MakeOwningWaker();
    return Pending();
  }

  Mutex mu_;
  Reader* winner_ ABSL_GUARDED_BY(mu_){nullptr};
  State state_ ABSL_GUARDED_BY(mu_){Buffering{}};
  WaitSet pull_waiters_ ABSL_GUARDED_BY(mu_);
  Waker push_waker_ ABSL_GUARDED_BY(mu_);
};

}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_CALL_REQUEST_BUFFER_H
