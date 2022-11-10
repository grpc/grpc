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

#ifndef GRPC_EVENT_ENGINE_PROMISE_ENDPOINT_H
#define GRPC_EVENT_ENGINE_PROMISE_ENDPOINT_H

#include <grpc/support/port_platform.h>

#include <stddef.h>
#include <stdint.h>

#include <memory>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/types/optional.h"

#include <grpc/event_engine/event_engine.h>
#include <grpc/event_engine/slice.h>
#include <grpc/event_engine/slice_buffer.h>
#include <grpc/slice_buffer.h>
#include <grpc/support/log.h>

#include "src/core/lib/promise/poll.h"
#include "src/core/lib/slice/slice.h"
#include "src/core/lib/slice/slice_buffer.h"

namespace grpc {

namespace internal {

class PromiseEndpoint {
 public:
  PromiseEndpoint(
      std::unique_ptr<grpc_event_engine::experimental::EventEngine::Endpoint>
          endpoint,
      grpc_core::SliceBuffer already_received);
  ~PromiseEndpoint();

  auto Write(grpc_core::SliceBuffer data) {
    /// Previous write result has not been polled.
    GPR_ASSERT(!write_result_.has_value());

    /// TODO: Is there a better way to convert?
    grpc_slice_buffer_swap(data.c_slice_buffer(),
                           write_buffer_.c_slice_buffer());

    endpoint_->Write([this](absl::Status status) { write_result_ = status; },
                     &write_buffer_, nullptr /* uses default arguments */);

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

  auto Read(size_t num_bytes) {
    /// Previous read result has not been polled.
    GPR_ASSERT(!read_result_.has_value());

    /// Should not have pending reads
    GPR_ASSERT(pending_read_buffer_.Count() == 0u);

    if (read_buffer_.Count() < num_bytes) {
      /// TODO: handle potential integer overflow
      /// TODO: evaluate the lifespan of `read_args`
      const struct grpc_event_engine::experimental::EventEngine::Endpoint::
          ReadArgs read_args = {
              static_cast<int64_t>(num_bytes - read_buffer_.Count())};

      temporary_read_buffer_.Clear();  /// may be redundant
      endpoint_->Read([this](absl::Status status) { read_result_ = status; },
                      &temporary_read_buffer_, &read_args);
    } else {
      read_result_ = absl::OkStatus();
    }

    return [this, num_bytes]()
               -> grpc_core::Poll<absl::StatusOr<grpc_core::SliceBuffer>> {
      if (!read_result_.has_value()) {
        return grpc_core::Pending();
      } else if (!read_result_->ok()) {
        /// drops pending data
        pending_read_buffer_.Clear();

        const absl::Status ret = *read_result_;
        read_result_.reset();

        return ret;
      } else if (read_buffer_.Count() + pending_read_buffer_.Count() +
                     temporary_read_buffer_.Count() <
                 num_bytes) {
        while (temporary_read_buffer_.Count() > 0u) {
          pending_read_buffer_.Append(temporary_read_buffer_.TakeFirst());
        }

        read_result_.reset();

        /// Makes another read request since the buffer does not have enough
        /// data.
        /// TODO: handle potential integer overflow
        /// TODO: evaluate the lifespan of `read_args`
        const struct grpc_event_engine::experimental::EventEngine::Endpoint::
            ReadArgs read_args = {
                static_cast<int64_t>(num_bytes - read_buffer_.Count() -
                                     pending_read_buffer_.Count())};

        temporary_read_buffer_.Clear();  /// may be redundant
        endpoint_->Read([this](absl::Status status) { read_result_ = status; },
                        &temporary_read_buffer_, &read_args);

        /// TODO: It seems to be a bad idea to make subsequent read requests
        /// only on polling. I would assume this would be a rare case given that
        /// the `read_hint_bytes` should take some effect.
        return grpc_core::Pending();
      } else {
        while (pending_read_buffer_.Count() > 0u) {
          grpc_slice_buffer_add(read_buffer_.c_slice_buffer(),
                                pending_read_buffer_.TakeFirst().c_slice());
        }

        while (temporary_read_buffer_.Count() > 0u) {
          grpc_slice_buffer_add(read_buffer_.c_slice_buffer(),
                                temporary_read_buffer_.TakeFirst().c_slice());
        }

        grpc_core::SliceBuffer ret;
        grpc_slice_buffer_move_first(read_buffer_.c_slice_buffer(), num_bytes,
                                     ret.c_slice_buffer());

        read_result_.reset();
        return ret;
      }
    };
  }

  auto ReadSlice(size_t num_bytes) {
    /// Previous read result has not been polled.
    GPR_ASSERT(!read_result_.has_value());

    if (read_buffer_.Count() == 0u) {
      /// TODO: handle potential integer overflow
      /// TODO: evaluate the lifespan of `read_args`
      const struct grpc_event_engine::experimental::EventEngine::Endpoint::
          ReadArgs read_args = {static_cast<int64_t>(num_bytes)};

      temporary_read_buffer_.Clear();  /// may be redundant
      endpoint_->Read([this](absl::Status status) { read_result_ = status; },
                      &temporary_read_buffer_, &read_args);
    } else {
      read_result_ = absl::OkStatus();
    }

    return [this]() -> grpc_core::Poll<absl::StatusOr<grpc_core::Slice>> {
      if (!read_result_.has_value()) {
        return grpc_core::Pending();
      } else if (!read_result_->ok()) {
        const auto ret = *read_result_;
        read_result_.reset();

        return ret;
      } else {
        while (temporary_read_buffer_.Count() > 0u) {
          grpc_slice_buffer_add(read_buffer_.c_slice_buffer(),
                                temporary_read_buffer_.TakeFirst().c_slice());
        }

        read_result_.reset();
        return read_buffer_.TakeFirst();
      }
    };
  }

  auto ReadByte() {
    /// Previous read result has not been polled.
    GPR_ASSERT(!read_result_.has_value());

    if (read_buffer_.Count() == 0u) {
      temporary_read_buffer_.Clear();  /// may be redundant
      endpoint_->Read([this](absl::Status status) { read_result_ = status; },
                      &temporary_read_buffer_, nullptr);
    } else {
      read_result_ = absl::OkStatus();
    }

    return [this]() -> grpc_core::Poll<absl::StatusOr<uint8_t>> {
      if (!read_result_.has_value()) {
        return grpc_core::Pending();
      } else if (!read_result_->ok()) {
        const auto ret = *read_result_;
        read_result_.reset();

        return ret;
      } else {
        while (temporary_read_buffer_.Count() > 0u) {
          grpc_slice_buffer_add(read_buffer_.c_slice_buffer(),
                                temporary_read_buffer_.TakeFirst().c_slice());
        }

        uint8_t ret = 0u;
        read_buffer_.MoveFirstNBytesIntoBuffer(1, &ret);

        return ret;
      }
    };
  }

  const grpc_event_engine::experimental::EventEngine::ResolvedAddress&
  GetPeerAddress() const;
  const grpc_event_engine::experimental::EventEngine::ResolvedAddress&
  GetLocalAddress() const;

 private:
  std::unique_ptr<grpc_event_engine::experimental::EventEngine::Endpoint>
      endpoint_;

  /// data for writes
  /// TODO: handle race condition
  grpc_event_engine::experimental::SliceBuffer write_buffer_;
  absl::optional<absl::Status> write_result_;

  /// data for reads
  /// TODO: handle race condition
  grpc_core::SliceBuffer read_buffer_;
  grpc_event_engine::experimental::SliceBuffer pending_read_buffer_;
  grpc_event_engine::experimental::SliceBuffer temporary_read_buffer_;
  absl::optional<absl::Status> read_result_;
};

}  // namespace internal

}  // namespace grpc

#endif
