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

#ifndef GRPC_CORE_LIB_TRANSPORT_PROMISE_ENDPOINT_H
#define GRPC_CORE_LIB_TRANSPORT_PROMISE_ENDPOINT_H

#include <grpc/support/port_platform.h>

#include <stddef.h>
#include <stdint.h>

#include <functional>
#include <memory>
#include <utility>

#include "absl/base/thread_annotations.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/types/optional.h"

#include <grpc/event_engine/event_engine.h>
#include <grpc/event_engine/slice.h>
#include <grpc/event_engine/slice_buffer.h>
#include <grpc/slice_buffer.h>
#include <grpc/support/log.h>

#include "src/core/lib/gprpp/sync.h"
#include "src/core/lib/promise/activity.h"
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
    grpc_core::MutexLock lock(&write_mutex_);

    /// Previous write result has not been polled.
    GPR_ASSERT(!write_result_.has_value());

    /// TODO: Is there a better way to convert?
    grpc_slice_buffer_swap(data.c_slice_buffer(),
                           write_buffer_.c_slice_buffer());

    const std::function<void(absl::Status)> write_callback =
        [this](absl::Status status) {
          grpc_core::MutexLock lock(&write_mutex_);
          write_result_ = status;
          write_waker_.Wakeup();
        };
    endpoint_->Write(write_callback, &write_buffer_,
                     nullptr /* uses default arguments */);

    return [this]() -> grpc_core::Poll<absl::Status> {
      grpc_core::MutexLock lock(&write_mutex_);
      if (!write_result_.has_value()) {
        write_waker_ = grpc_core::Activity::current()->MakeNonOwningWaker();
        return grpc_core::Pending();
      } else {
        const auto ret = *write_result_;
        write_result_.reset();
        return ret;
      }
    };
  }

  auto Read(size_t num_bytes) {
    grpc_core::MutexLock lock(&read_mutex_);

    /// Previous read result has not been polled.
    GPR_ASSERT(!read_result_.has_value());

    /// Should not have pending reads
    GPR_ASSERT(pending_read_buffer_.Count() == 0u);

    if (read_buffer_.Count() < num_bytes) {
      const std::function<void(absl::Status)> read_callback =
          [this, num_bytes, &read_callback](absl::Status status) {
            if (!status.ok()) {
              pending_read_buffer_.Clear();
              temporary_read_buffer_.Clear();

              grpc_core::MutexLock lock(&read_mutex_);
              read_result_ = status;
              read_waker_.Wakeup();
            } else {
              while (temporary_read_buffer_.Count() > 0u) {
                pending_read_buffer_.Append(temporary_read_buffer_.TakeFirst());
              }

              if (read_buffer_.Count() + pending_read_buffer_.Count() <
                  num_bytes) {
                /// A further read is needed.
                endpoint_->Read(read_callback, &temporary_read_buffer_,
                                nullptr /* uses default arguments */);
              } else {
                while (pending_read_buffer_.Count() > 0u) {
                  grpc_slice_buffer_add(
                      read_buffer_.c_slice_buffer(),
                      pending_read_buffer_.TakeFirst().c_slice());
                }

                grpc_core::MutexLock lock(&read_mutex_);
                read_result_ = status;
                read_waker_.Wakeup();
              }
            }
          };
      temporary_read_buffer_.Clear();  /// may be redundant
      endpoint_->Read(read_callback, &temporary_read_buffer_,
                      nullptr /* uses default arguments */);
    } else {
      read_result_ = absl::OkStatus();
    }

    return [this, num_bytes]()
               -> grpc_core::Poll<absl::StatusOr<grpc_core::SliceBuffer>> {
      grpc_core::MutexLock lock(&read_mutex_);
      if (!read_result_.has_value()) {
        read_waker_ = grpc_core::Activity::current()->MakeNonOwningWaker();
        return grpc_core::Pending();
      } else if (!read_result_->ok()) {
        const absl::Status ret = *read_result_;
        read_result_.reset();

        return ret;
      } else {
        grpc_core::SliceBuffer ret;
        grpc_slice_buffer_move_first(read_buffer_.c_slice_buffer(), num_bytes,
                                     ret.c_slice_buffer());

        read_result_.reset();
        return ret;
      }
    };
  }

  auto ReadSlice(size_t num_bytes) {
    grpc_core::MutexLock lock(&read_mutex_);

    /// Previous read result has not been polled.
    GPR_ASSERT(!read_result_.has_value());

    if (read_buffer_.Count() < num_bytes) {
      /// TODO: handle potential integer overflow
      /// TODO: evaluate the lifespan of `read_args`
      const struct grpc_event_engine::experimental::EventEngine::Endpoint::
          ReadArgs read_args = {static_cast<int64_t>(num_bytes)};

      const std::function<void(absl::Status)> read_callback =
          [this, num_bytes, read_args, &read_callback](absl::Status status) {
            if (!status.ok()) {
              pending_read_buffer_.Clear();
              temporary_read_buffer_.Clear();

              grpc_core::MutexLock lock(&read_mutex_);
              read_result_ = status;
              read_waker_.Wakeup();
            } else {
              while (temporary_read_buffer_.Count() > 0u) {
                pending_read_buffer_.Append(temporary_read_buffer_.TakeFirst());
              }

              if (read_buffer_.Count() + pending_read_buffer_.Count() <
                  num_bytes) {
                /// A further read is needed.
                endpoint_->Read(read_callback, &temporary_read_buffer_,
                                &read_args);
              } else {
                while (pending_read_buffer_.Count() > 0u) {
                  grpc_slice_buffer_add(
                      read_buffer_.c_slice_buffer(),
                      pending_read_buffer_.TakeFirst().c_slice());
                }

                grpc_core::MutexLock lock(&read_mutex_);
                read_result_ = status;
                read_waker_.Wakeup();
              }
            }
          };

      temporary_read_buffer_.Clear();  /// may be redundant
      endpoint_->Read(read_callback, &temporary_read_buffer_, &read_args);
    } else {
      read_result_ = absl::OkStatus();
    }

    return [this,
            num_bytes]() -> grpc_core::Poll<absl::StatusOr<grpc_core::Slice>> {
      grpc_core::MutexLock lock(&read_mutex_);
      if (!read_result_.has_value()) {
        read_waker_ = grpc_core::Activity::current()->MakeNonOwningWaker();
        return grpc_core::Pending();
      } else if (!read_result_->ok()) {
        const auto ret = *read_result_;
        read_result_.reset();

        return ret;
      } else if (read_buffer_.RefSlice(0).size() == num_bytes) {
        return read_buffer_.TakeFirst();
      } else {
        grpc_core::MutableSlice ret =
            grpc_core::MutableSlice::CreateUninitialized(num_bytes);
        read_buffer_.MoveFirstNBytesIntoBuffer(num_bytes, ret.data());

        return grpc_core::Slice(std::move(ret));
      }
    };
  }

  auto ReadByte() {
    grpc_core::MutexLock lock(&read_mutex_);

    /// Previous read result has not been polled.
    GPR_ASSERT(!read_result_.has_value());

    if (read_buffer_.Count() == 0u) {
      const std::function<void(absl::Status)> read_callback =
          [this](absl::Status status) {
            grpc_core::MutexLock lock(&read_mutex_);
            if (!status.ok()) {
              pending_read_buffer_.Clear();
              temporary_read_buffer_.Clear();

              read_result_ = status;
            } else {
              while (temporary_read_buffer_.Count() > 0u) {
                grpc_slice_buffer_add(
                    read_buffer_.c_slice_buffer(),
                    temporary_read_buffer_.TakeFirst().c_slice());
              }

              read_result_ = status;
            }
            read_waker_.Wakeup();
          };

      temporary_read_buffer_.Clear();  /// may be redundant
      endpoint_->Read(read_callback, &temporary_read_buffer_, nullptr);
    } else {
      read_result_ = absl::OkStatus();
    }

    return [this]() -> grpc_core::Poll<absl::StatusOr<uint8_t>> {
      grpc_core::MutexLock lock(&read_mutex_);
      if (!read_result_.has_value()) {
        read_waker_ = grpc_core::Activity::current()->MakeNonOwningWaker();
        return grpc_core::Pending();
      } else if (!read_result_->ok()) {
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
  GetPeerAddress() const;
  const grpc_event_engine::experimental::EventEngine::ResolvedAddress&
  GetLocalAddress() const;

 private:
  std::unique_ptr<grpc_event_engine::experimental::EventEngine::Endpoint>
      endpoint_;

  /// data for writes
  grpc_core::Mutex write_mutex_;
  grpc_event_engine::experimental::SliceBuffer write_buffer_;
  absl::optional<absl::Status> write_result_ ABSL_GUARDED_BY(write_mutex_);
  grpc_core::Waker write_waker_ ABSL_GUARDED_BY(write_mutex_);

  /// data for reads
  grpc_core::Mutex read_mutex_;
  grpc_core::SliceBuffer read_buffer_;
  grpc_event_engine::experimental::SliceBuffer pending_read_buffer_;
  grpc_event_engine::experimental::SliceBuffer temporary_read_buffer_;
  absl::optional<absl::Status> read_result_ ABSL_GUARDED_BY(read_mutex_);
  grpc_core::Waker read_waker_ ABSL_GUARDED_BY(read_mutex_);
};

}  // namespace internal

}  // namespace grpc

#endif  // GRPC_CORE_LIB_TRANSPORT_PROMISE_ENDPOINT_H
