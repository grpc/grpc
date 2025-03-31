// Copyright 2025 The gRPC Authors
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

#ifndef GRPC_SRC_CORE_LIB_EVENT_ENGINE_POSIX_ENGINE_FILE_DESCRIPTOR_COLLECTION_H
#define GRPC_SRC_CORE_LIB_EVENT_ENGINE_POSIX_ENGINE_FILE_DESCRIPTOR_COLLECTION_H

#include <atomic>
#include <unordered_set>
#include <variant>

#include "absl/status/status.h"
#include "absl/strings/str_format.h"
#include "src/core/util/strerror.h"
#include "src/core/util/sync.h"

namespace grpc_event_engine::experimental {

template <typename T>
class PosixErrorOr {
 public:
  PosixErrorOr() = default;
  PosixErrorOr(const PosixErrorOr& other) = default;
  PosixErrorOr(PosixErrorOr&& other) = default;
  PosixErrorOr& operator=(const PosixErrorOr& other) = default;
  PosixErrorOr& operator=(PosixErrorOr&& other) = default;

  static PosixErrorOr Error(int code) { return PosixErrorOr(PosixError{code}); }

  static PosixErrorOr WrongGeneration() {
    return PosixErrorOr(WrongGenerationError());
  }

  explicit PosixErrorOr(T value) : value_(std::move(value)) {}

  bool ok() const { return std::holds_alternative<T>(value_); }

  int code() const {
    const PosixError* error = std::get_if<PosixError>(&value_);
    CHECK_NE(error, nullptr);
    return error->code;
  }

  bool IsPosixError() const {
    return std::holds_alternative<PosixError>(value_);
  }

  bool IsPosixError(int code) const {
    const PosixError* error = std::get_if<PosixError>(&value_);
    return error != nullptr && code == error->code;
  }

  bool IsWrongGenerationError() const {
    return std::holds_alternative<WrongGenerationError>(value_);
  }

  T* operator->() { return &std::get<T>(value_); }
  T& operator*() { return std::get<T>(value_); }
  T value() const {
    CHECK(ok());
    return std::get<T>(value_);
  }

  std::string StrError() const {
    if (ok()) {
      return "ok";
    } else if (IsWrongGenerationError()) {
      return "wrong generation";
    } else {
      return grpc_core::StrError(code());
    }
  }

 private:
  struct PosixError {
    int code;
  };
  struct WrongGenerationError {};

  explicit PosixErrorOr(std::variant<PosixError, WrongGenerationError, T> error)
      : value_(std::move(error)) {}

  std::variant<PosixError, WrongGenerationError, T> value_;
};

class FileDescriptor {
 public:
  constexpr FileDescriptor() = default;
#if GRPC_ENABLE_FORK_SUPPORT
  constexpr FileDescriptor(int fd, int generation)
      : fd_(fd), generation_(generation) {};
#else   // GRPC_ENABLE_FORK_SUPPORT
  constexpr FileDescriptor(int fd, int /* generation */) : fd_(fd) {};
#endif  // GRPC_ENABLE_FORK_SUPPORT

  bool ready() const { return fd_ > 0; }
  // Escape for iomgr and tests. Not to be used elsewhere
  int iomgr_fd() const { return fd_; }
  // For logging/debug purposes - may include generation in the future, do not
  // rely on it for Posix calls!
  int debug_fd() const { return fd_; }

  constexpr static FileDescriptor Invalid() { return FileDescriptor(-1, 0); }

#if GRPC_ENABLE_FORK_SUPPORT
  int generation() const { return generation_; }
  template <typename Sink>
  friend void AbslStringify(Sink& sink, FileDescriptor fd) {
    sink.Append(
        absl::StrFormat("FD(%d), generation: %d", fd.fd_, fd.generation_));
  }
#else   // GRPC_ENABLE_FORK_SUPPORT
  int generation() const { return 0; }
  template <typename Sink>
  friend void AbslStringify(Sink& sink, FileDescriptor fd) {
    sink.Append(absl::StrFormat("FD(%d)", fd.fd_));
  }
#endif  // GRPC_ENABLE_FORK_SUPPORT

 private:
  int fd() const { return fd_; }
  // Can get raw fd!
  friend class FileDescriptorCollection;
  friend class EventEnginePosixInterface;

  int fd_ = 0;
#if GRPC_ENABLE_FORK_SUPPORT
  int generation_ = 0;
#endif  // GRPC_ENABLE_FORK_SUPPORT
};

enum class OperationResultKind {
  kSuccess,          // Operation does not return a file descriptor and
                     // return value was >= 0. native_result holds the
                     // original return value.
  kError,            // Check native_result and errno for details
  kWrongGeneration,  // System call was not performed because file
                     // descriptor belongs to the wrong generation.
};

template <typename Sink>
void AbslStringify(Sink& sink, OperationResultKind kind) {
  sink.Append(kind == OperationResultKind::kSuccess ? "(Success)"
              : kind == OperationResultKind::kError ? "(Error)"
                                                    : "(Wrong Generation)");
}

// Result of the factory call. kWrongGeneration may happen in the call to
// Accept*
class PosixResult {
 public:
  constexpr PosixResult() = default;
  explicit constexpr PosixResult(OperationResultKind kind, int errno_value)
      : kind_(kind), errno_value_(errno_value) {}

  virtual ~PosixResult() = default;

  absl::Status status() const {
    switch (kind_) {
      case OperationResultKind::kSuccess:
        return absl::OkStatus();
      case OperationResultKind::kError:
        return absl::ErrnoToStatus(errno_value_, "");
      case OperationResultKind::kWrongGeneration:
        return absl::InternalError(
            "File descriptor is from the wrong generation");
    }
  }

  virtual bool ok() const { return kind_ == OperationResultKind::kSuccess; }

  bool IsPosixError(int err) const {
    return kind_ == OperationResultKind::kError && errno_value_ == err;
  }

  OperationResultKind kind() const { return kind_; }
  int errno_value() const { return errno_value_; }

 private:
  OperationResultKind kind_ = OperationResultKind::kSuccess;
  // errno value on call completion, in order to reduce the race conditions
  // from relying on global variable.
  int errno_value_ = 0;
};

class FileDescriptorCollection {
 public:
  // Encodes a file descriptor (fd) and its generation into a single integer,
  // required by some libraries (e.g., Ares).
  // Formula: `fd + ((generation & kGenerationMask) << kIntFdBits)`.
  //
  // Use ToInteger/FromInteger for conversion.
  //
  // LIMITATIONS:
  // 1. Fails (with an assertion) if fd > 28 bits. However, POSIX assigns
  //    the lowest available fd, making 2^28 (~268M) open fds highly impractical
  //    due to kernel resource usage.
  // 2. Only uses the lower 3 bits of generation, risking collisions (e.g., gen
  //    9 accepts gen 1).
  static constexpr int kIntFdBits = 28;
  static constexpr int kGenerationMask = 0x7;  // 3 bits

  FileDescriptor Add(int fd);
  bool Remove(const FileDescriptor& fd);

  int ToInteger(const FileDescriptor& fd) const;
  PosixErrorOr<FileDescriptor> FromInteger(int fd) const;

  // Advances the generation, clears the list of fds and returns them
  std::unordered_set<int> AdvanceGeneration();

  int generation() const {
    return current_generation_.load(std::memory_order_relaxed);
  }

 private:
  grpc_core::Mutex mu_;
  std::unordered_set<int> file_descriptors_ ABSL_GUARDED_BY(mu_);
  std::atomic_int current_generation_{1};
};

}  // namespace grpc_event_engine::experimental

#endif  // GRPC_SRC_CORE_LIB_EVENT_ENGINE_POSIX_ENGINE_FILE_DESCRIPTOR_COLLECTION_H
