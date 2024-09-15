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

#include "src/core/lib/transport/call_spine.h"
#include "src/core/lib/transport/message.h"
#include "src/core/lib/transport/metadata.h"

namespace grpc_core {

class RequestBuffer {
 public:
  class Reader {
   public:
    explicit Reader(RequestBuffer* buffer) ABSL_LOCKS_EXCLUDED(buffer->mu_)
        : buffer_(buffer) {
      buffer->AddReader(this);
    }
    ~Reader() ABSL_LOCKS_EXCLUDED(buffer_->mu_) { buffer_->RemoveReader(this); }

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
    friend class RequestBuffer;

    Poll<ValueOrFailure<ClientMetadataHandle>> PollPullClientInitialMetadata();
    Poll<ValueOrFailure<absl::optional<MessageHandle>>> PollPullMessage();

    template <typename T>
    T ClaimObject(T& object) ABSL_EXCLUSIVE_LOCKS_REQUIRED(buffer_->mu_) {
      if (buffer_->winner_ == this) return std::move(object);
      return CopyObject(object);
    }

    ClientMetadataHandle CopyObject(const ClientMetadataHandle& md) {
      return Arena::MakePooled<ClientMetadata>(md->Copy());
    }

    MessageHandle CopyObject(const MessageHandle& msg) {
      return Arena::MakePooled<Message>(msg->payload()->Copy(), msg->flags());
    }

    RequestBuffer* const buffer_;
    bool pulled_client_initial_metadata_ = false;
    size_t message_index_ = 0;
    absl::Status error_;
    Waker pull_waker_;
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
  Pending PendingPull(Reader* reader) ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu_) {
    reader->pull_waker_ = Activity::current()->MakeOwningWaker();
    return Pending{};
  }
  Pending PendingPush() ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu_) {
    push_waker_ = Activity::current()->MakeOwningWaker();
    return Pending{};
  }

  void WakeupAsyncAllPullers() ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu_) {
    WakeupAsyncAllPullersExcept(nullptr);
  }
  void WakeupAsyncAllPullersExcept(Reader* except_reader)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu_);

  void AddReader(Reader* reader) ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu_) {
    readers_.insert(reader);
  }

  void RemoveReader(Reader* reader) ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu_) {
    readers_.erase(reader);
  }

  Mutex mu_;
  Reader* winner_ ABSL_GUARDED_BY(mu_){nullptr};
  State state_ ABSL_GUARDED_BY(mu_){Buffering{}};
  // TODO(ctiller): change this to an intrusively linked list to avoid
  // allocations.
  absl::flat_hash_set<Reader*> readers_ ABSL_GUARDED_BY(mu_);
  Waker push_waker_ ABSL_GUARDED_BY(mu_);
};

}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_CALL_REQUEST_BUFFER_H
