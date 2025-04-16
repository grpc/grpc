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
#include <optional>

#include "absl/strings/str_cat.h"
#include "src/core/util/match.h"

namespace grpc_core {

RequestBuffer::Buffering::Buffering() {}

RequestBuffer::RequestBuffer() : state_(absl::in_place_type_t<Buffering>()) {}

ValueOrFailure<size_t> RequestBuffer::PushClientInitialMetadata(
    ClientMetadataHandle md) {
  MutexLock lock(&mu_);
  if (std::get_if<Cancelled>(&state_)) return Failure{};
  auto& buffering = std::get<Buffering>(state_);
  CHECK_EQ(buffering.initial_metadata.get(), nullptr);
  buffering.initial_metadata = std::move(md);
  buffering.buffered += buffering.initial_metadata->TransportSize();
  WakeupAsyncAllPullers();
  return buffering.buffered;
}

Poll<ValueOrFailure<size_t>> RequestBuffer::PollPushMessage(
    MessageHandle& message) {
  MutexLock lock(&mu_);
  if (std::get_if<Cancelled>(&state_)) return Failure{};
  size_t buffered = 0;
  if (auto* buffering = std::get_if<Buffering>(&state_)) {
    if (winner_ != nullptr) return PendingPush();
    buffering->buffered += message->payload()->Length();
    buffered = buffering->buffered;
    buffering->messages.push_back(std::move(message));
  } else {
    auto& streaming = std::get<Streaming>(state_);
    CHECK_EQ(streaming.end_of_stream, false);
    if (streaming.message != nullptr) {
      return PendingPush();
    }
    streaming.message = std::move(message);
  }
  WakeupAsyncAllPullers();
  return buffered;
}

StatusFlag RequestBuffer::FinishSends() {
  MutexLock lock(&mu_);
  if (std::get_if<Cancelled>(&state_)) return Failure{};
  if (auto* buffering = std::get_if<Buffering>(&state_)) {
    Buffered buffered(std::move(buffering->initial_metadata),
                      std::move(buffering->messages));
    state_.emplace<Buffered>(std::move(buffered));
  } else {
    auto& streaming = std::get<Streaming>(state_);
    CHECK_EQ(streaming.end_of_stream, false);
    streaming.end_of_stream = true;
  }
  WakeupAsyncAllPullers();
  return Success{};
}

void RequestBuffer::Cancel(absl::Status error) {
  MutexLock lock(&mu_);
  if (std::holds_alternative<Cancelled>(state_)) return;
  state_.emplace<Cancelled>(std::move(error));
  WakeupAsyncAllPullers();
}

void RequestBuffer::Commit(Reader* winner) {
  MutexLock lock(&mu_);
  CHECK_EQ(winner_, nullptr);
  winner_ = winner;
  if (auto* buffering = std::get_if<Buffering>(&state_)) {
    if (buffering->initial_metadata != nullptr &&
        winner->message_index_ == buffering->messages.size() &&
        winner->pulled_client_initial_metadata_) {
      state_.emplace<Streaming>();
    }
  } else if (auto* buffered = std::get_if<Buffered>(&state_)) {
    CHECK_NE(buffered->initial_metadata.get(), nullptr);
    if (winner->message_index_ == buffered->messages.size()) {
      state_.emplace<Streaming>().end_of_stream = true;
    }
  }
  WakeupAsyncAllPullersExcept(winner);
}

void RequestBuffer::WakeupAsyncAllPullersExcept(Reader* except_reader) {
  for (auto wakeup_reader : readers_) {
    if (wakeup_reader == except_reader) continue;
    wakeup_reader->pull_waker_.WakeupAsync();
  }
}

Poll<ValueOrFailure<ClientMetadataHandle>>
RequestBuffer::Reader::PollPullClientInitialMetadata() {
  MutexLock lock(&buffer_->mu_);
  if (buffer_->winner_ != nullptr && buffer_->winner_ != this) {
    error_ = absl::CancelledError("Another call was chosen");
    return Failure{};
  }
  if (auto* buffering = std::get_if<Buffering>(&buffer_->state_)) {
    if (buffering->initial_metadata.get() == nullptr) {
      return buffer_->PendingPull(this);
    }
    pulled_client_initial_metadata_ = true;
    auto result = ClaimObject(buffering->initial_metadata);
    buffer_->MaybeSwitchToStreaming();
    return std::move(result);
  }
  if (auto* buffered = std::get_if<Buffered>(&buffer_->state_)) {
    pulled_client_initial_metadata_ = true;
    return ClaimObject(buffered->initial_metadata);
  }
  error_ = std::get<Cancelled>(buffer_->state_).error;
  return Failure{};
}

Poll<ValueOrFailure<std::optional<MessageHandle>>>
RequestBuffer::Reader::PollPullMessage() {
  ReleasableMutexLock lock(&buffer_->mu_);
  if (buffer_->winner_ != nullptr && buffer_->winner_ != this) {
    error_ = absl::CancelledError("Another call was chosen");
    return Failure{};
  }
  if (auto* buffering = std::get_if<Buffering>(&buffer_->state_)) {
    if (message_index_ == buffering->messages.size()) {
      return buffer_->PendingPull(this);
    }
    const auto idx = message_index_;
    auto result = ClaimObject(buffering->messages[idx]);
    ++message_index_;
    buffer_->MaybeSwitchToStreaming();
    return std::move(result);
  }
  if (auto* buffered = std::get_if<Buffered>(&buffer_->state_)) {
    if (message_index_ == buffered->messages.size()) return std::nullopt;
    const auto idx = message_index_;
    ++message_index_;
    return ClaimObject(buffered->messages[idx]);
  }
  if (auto* streaming = std::get_if<Streaming>(&buffer_->state_)) {
    if (streaming->message == nullptr) {
      if (streaming->end_of_stream) return std::nullopt;
      return buffer_->PendingPull(this);
    }
    auto msg = std::move(streaming->message);
    auto waker = std::move(buffer_->push_waker_);
    lock.Release();
    waker.Wakeup();
    return std::move(msg);
  }
  error_ = std::get<Cancelled>(buffer_->state_).error;
  return Failure{};
}

std::string RequestBuffer::DebugString(Reader* caller)
    ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu_) {
  return absl::StrCat(
      "have_winner=",
      (winner_ == nullptr ? "no" : (winner_ == caller ? "this" : "other")),
      " num_readers=", readers_.size(),
      " push_waker=", push_waker_.DebugString(),
      Match(
          state_,
          [](const Buffering& buffering) {
            return absl::StrCat(
                " buffering initial_metadata=",
                (buffering.initial_metadata != nullptr
                     ? buffering.initial_metadata->DebugString()
                     : "null"),
                " messages=[",
                absl::StrJoin(
                    buffering.messages, ",",
                    [](std::string* output, const MessageHandle& hdl) {
                      absl::StrAppend(output, hdl->DebugString());
                    }),
                "] buffered=", buffering.buffered);
          },
          [](const Buffered& buffered) {
            return absl::StrCat(
                " buffered initial_metadata=",
                (buffered.initial_metadata != nullptr
                     ? buffered.initial_metadata->DebugString()
                     : "null"),
                " messages=[",
                absl::StrJoin(
                    buffered.messages, ",",
                    [](std::string* output, const MessageHandle& hdl) {
                      absl::StrAppend(
                          output, hdl != nullptr ? hdl->DebugString() : "null");
                    }),
                "]");
          },
          [](const Streaming& streaming) {
            return absl::StrCat(
                " streaming message=",
                (streaming.message != nullptr ? streaming.message->DebugString()
                                              : "null"),
                " end_of_stream=", streaming.end_of_stream);
          },
          [](const Cancelled& cancelled) {
            return absl::StrCat(" cancelled error=",
                                cancelled.error.ToString());
          }));
}
}  // namespace grpc_core
