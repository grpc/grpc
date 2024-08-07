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

#include "src/core/call/request_buffer.h"

#include <cstdint>

#include "absl/types/optional.h"

namespace grpc_core {

StatusFlag RequestBuffer::PushClientInitialMetadata(ClientMetadataHandle md) {
  ReleasableMutexLock lock(&mu_);
  if (absl::get_if<Cancelled>(&state_)) return Failure{};
  auto& buffering = absl::get<Buffering>(state_);
  CHECK_EQ(buffering.initial_metadata.get(), nullptr);
  buffering.initial_metadata = std::move(md);
  auto wakeup = pull_waiters_.TakeWakeupSet();
  lock.Release();
  wakeup.Wakeup();
  return Success{};
}

Poll<ValueOrFailure<size_t>> RequestBuffer::PollPushMessage(
    MessageHandle& message) {
  ReleasableMutexLock lock(&mu_);
  if (absl::get_if<Cancelled>(&state_)) return Failure{};
  size_t buffered = 0;
  if (auto* buffering = absl::get_if<Buffering>(&state_)) {
    buffering->messages.push_back(std::move(message));
    buffering->buffered += message->payload()->Length();
    buffered = buffering->buffered;
  } else {
    auto& streaming = absl::get<Streaming>(state_);
    CHECK_EQ(streaming.end_of_stream, false);
    if (streaming.message != nullptr) {
      return PendingPush();
    }
    streaming.message = std::move(message);
  }
  auto wakeup = pull_waiters_.TakeWakeupSet();
  lock.Release();
  wakeup.Wakeup();
  return buffered;
}

StatusFlag RequestBuffer::FinishSends() {
  ReleasableMutexLock lock(&mu_);
  if (absl::get_if<Cancelled>(&state_)) return Failure{};
  if (auto* buffering = absl::get_if<Buffering>(&state_)) {
    state_.emplace<Buffered>(std::move(buffering->initial_metadata),
                             std::move(buffering->messages));
  } else {
    auto& streaming = absl::get<Streaming>(state_);
    CHECK_EQ(streaming.end_of_stream, false);
    streaming.end_of_stream = true;
  }
  auto wakeup = pull_waiters_.TakeWakeupSet();
  lock.Release();
  wakeup.Wakeup();
  return Success{};
}

void RequestBuffer::Cancel(absl::Status error) {
  ReleasableMutexLock lock(&mu_);
  if (absl::holds_alternative<Cancelled>(state_)) return;
  state_.emplace<Cancelled>(std::move(error));
  auto wakeup = pull_waiters_.TakeWakeupSet();
  lock.Release();
  wakeup.Wakeup();
}

void RequestBuffer::SwitchToStreaming(Reader* winner) {
  ReleasableMutexLock lock(&mu_);
  CHECK_EQ(winner_, nullptr);
  winner_ = winner;
  auto wakeup = pull_waiters_.TakeWakeupSet();
  lock.Release();
  wakeup.Wakeup();
}

Poll<ValueOrFailure<ClientMetadataHandle>>
RequestBuffer::Reader::PollPullClientInitialMetadata() {
  MutexLock lock(&buffer_->mu_);
  if (buffer_->winner_ != nullptr && buffer_->winner_ != this) {
    error_ = absl::CancelledError("Another call was chosen");
    return Failure{};
  }
  if (auto* buffering = absl::get_if<Buffering>(&buffer_->state_)) {
    if (buffering->initial_metadata.get() == nullptr) {
      return buffer_->PendingPull();
    }
    return ClaimObject(buffering->initial_metadata);
  }
  if (auto* buffered = absl::get_if<Buffered>(&buffer_->state_)) {
    return ClaimObject(buffered->initial_metadata);
  }
  error_ = absl::get<Cancelled>(buffer_->state_).error;
  return Failure{};
}

Poll<ValueOrFailure<absl::optional<MessageHandle>>>
RequestBuffer::Reader::PollPullMessage() {
  ReleasableMutexLock lock(&buffer_->mu_);
  if (buffer_->winner_ != nullptr && buffer_->winner_ != this) {
    error_ = absl::CancelledError("Another call was chosen");
    return Failure{};
  }
  if (auto* buffering = absl::get_if<Buffering>(&buffer_->state_)) {
    if (message_index_ == buffering->messages.size()) {
      return buffer_->PendingPull();
    }
    const auto idx = message_index_;
    ++message_index_;
    return ClaimObject(buffering->messages[idx]);
  }
  if (auto* buffered = absl::get_if<Buffered>(&buffer_->state_)) {
    if (message_index_ == buffered->messages.size()) return absl::nullopt;
    const auto idx = message_index_;
    ++message_index_;
    return ClaimObject(buffered->messages[idx]);
  }
  if (auto* streaming = absl::get_if<Streaming>(&buffer_->state_)) {
    if (streaming->message == nullptr) {
      if (streaming->end_of_stream) return absl::nullopt;
      return buffer_->PendingPull();
    }
    auto msg = std::move(streaming->message);
    auto waker = std::move(buffer_->push_waker_);
    lock.Release();
    waker.Wakeup();
    return msg;
  }
  error_ = absl::get<Cancelled>(buffer_->state_).error;
  return Failure{};
}

}  // namespace grpc_core
