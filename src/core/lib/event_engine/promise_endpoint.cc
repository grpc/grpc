/*
 * Copyright 2022 gRPC authors.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <cstdint>

#include <grpc/event_engine/promise_endpoint.h>
#include <grpc/support/log.h>

namespace grpc {

namespace experimental {

PromiseEndpoint::PromiseEndpoint(
    std::unique_ptr<grpc_event_engine::experimental::EventEngine::Endpoint>
        endpoint,
    grpc_event_engine::experimental::SliceBuffer already_received)
    : endpoint_(std::move(endpoint)),
      read_buffer_(std::move(already_received)) {
  GPR_ASSERT(endpoint != nullptr);
}

PromiseEndpoint::~PromiseEndpoint() {
  /// Last write result has not been polled.
  GPR_ASSERT(!write_result_.has_value());
  /// Last read result has not been polled.
  GPR_ASSERT(!read_result_.has_value());
}

grpc_core::Promise<absl::Status> PromiseEndpoint::Write(
    grpc_event_engine::experimental::SliceBuffer data) {
  /// Previous write result has not been polled.
  GPR_ASSERT(!write_result_.has_value());

  write_buffer_ = std::move(data);
  /// TODO: handle possible overflow?
  /// TODO: evaluate the lifespan of `write_args`
  /// TODO: better choice for the `max_frame_size`?
  const struct grpc_event_engine::experimental::EventEngine::Endpoint::WriteArgs
      write_args = {nullptr, static_cast<int64_t>(write_buffer_.Length())};
  endpoint_->Write(
      [this](absl::Status status) -> void { write_result_ = status; },
      &write_buffer_, &write_args);

  return [this]() -> grpc_core::Poll<absl::Status> {
    if (!write_result_.has_value()) {
      return grpc_core::Pending();
    } else {
      const auto ret = *write_result_;
      write_result_.reset();
      return ret;
    }
  };
}

grpc_core::Promise<absl::StatusOr<grpc_event_engine::experimental::SliceBuffer>>
PromiseEndpoint::Read(size_t num_bytes) {
  /// Previous read result has not been polled.
  GPR_ASSERT(!read_result_.has_value());

  if (read_buffer_.Length() < num_bytes) {
    const size_t remaining_size = num_bytes - read_buffer_.Length();
    /// TODO: handle possible overflow?
    /// TODO: evaluate the lifespan of `read_args`
    const struct grpc_event_engine::experimental::EventEngine::Endpoint::
        ReadArgs read_args = {static_cast<int64_t>(remaining_size)};

    endpoint_->Read([this](absl::Status status) { read_result_ = status; },
                    &current_read_buffer_, &read_args);
  } else {
    read_result_ = absl::OkStatus();
  }

  return [this, num_bytes]()
             -> grpc_core::Poll<
                 absl::StatusOr<grpc_event_engine::experimental::SliceBuffer>> {
    if (!read_result_.has_value()) {
      return grpc_core::Pending();
    } else if (!read_result_->ok()) {
      /// drops any potentially collapsed data
      current_read_buffer_.Clear();

      const auto ret = *read_result_;
      read_result_.reset();

      return ret;
    } else {
      grpc_event_engine::experimental::SliceBuffer ret;

      read_buffer_.MoveFirstNBytesIntoSliceBuffer(
          std::min(num_bytes, read_buffer_.Length()), ret);
      if (ret.Length() < num_bytes) {
        current_read_buffer_.MoveFirstNBytesIntoSliceBuffer(
            num_bytes - ret.Length(), ret);
      }
      current_read_buffer_.MoveFirstNBytesIntoSliceBuffer(
          current_read_buffer_.Length(), read_buffer_);
      GPR_ASSERT(current_read_buffer_.Length() == 0u);

      read_result_.reset();
      return ret;
    }
  };
}

grpc_core::Promise<absl::StatusOr<grpc_event_engine::experimental::Slice>>
PromiseEndpoint::ReadSlice(size_t /* length */) {
  /// Placeholder
  /// TODO: Not yet implemented
  return []() -> grpc_core::Poll<
                  absl::StatusOr<grpc_event_engine::experimental::Slice>> {
    return absl::OkStatus();
  };
}

grpc_core::Promise<absl::StatusOr<uint8_t>> PromiseEndpoint::ReadByte() {
  /// Previous read result has not been polled.
  GPR_ASSERT(!read_result_.has_value());

  if (read_buffer_.Count() == 0u) {
    constexpr struct grpc_event_engine::experimental::EventEngine::Endpoint::
        ReadArgs read_args = {1};

    endpoint_->Read([this](absl::Status status) { read_result_ = status; },
                    &read_buffer_, &read_args);
  } else {
    read_result_ = absl::OkStatus();
  }

  return [this]() -> grpc_core::Poll<absl::StatusOr<uint8_t>> {
    if (!read_result_.has_value()) {
      return grpc_core::Pending();
    } else if (!read_result_->ok()) {
      /// drops any potentially collapsed data
      read_buffer_.Clear();

      const auto ret = *read_result_;
      read_result_.reset();

      return ret;
    } else {
      uint8_t ret = 0u;
      read_buffer_.MoveFirstNBytesIntoBuffer(1, &ret);

      return ret;
    }
  };
}

const grpc_event_engine::experimental::EventEngine::ResolvedAddress&
PromiseEndpoint::GetPeerAddress() const {
  return endpoint_->GetPeerAddress();
}

const grpc_event_engine::experimental::EventEngine::ResolvedAddress&
PromiseEndpoint::GetLocalAddress() const {
  return endpoint_->GetLocalAddress();
}

}  // namespace experimental

}  // namespace grpc
