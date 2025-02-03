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
#include <optional>
#include <unordered_set>

#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/strings/str_format.h"
#include "src/core/util/sync.h"

namespace grpc_event_engine::experimental {

class FileDescriptor {
 public:
  constexpr FileDescriptor() = default;
  constexpr FileDescriptor(int fd, int generation)
      : fd_(fd), generation_(generation) {};
  bool ready() const { return fd_ > 0; }
  // Escape for iomgr and tests. Not to be used elsewhere
  int iomgr_fd() const { return fd_; }
  // For logging/debug purposes - may consider including generation, do not
  // use for Posix calls!
  int debug_fd() const { return fd_; }
  // For tests, logging and such.
  int generation() const { return generation_; }

  constexpr static FileDescriptor Invalid() { return FileDescriptor(-1, 0); }

  template <typename Sink>
  friend void AbslStringify(Sink& sink, FileDescriptor fd) {
    sink.Append(
        absl::StrFormat("FD(%d), generation: %d", fd.fd_, fd.generation_));
  }

 private:
  int fd() const { return fd_; }

  // Can get fd!
  friend class FileDescriptorCollection;

  int fd_ = 0;
  int generation_ = 0;
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
      default:
        return absl::InvalidArgumentError("Unexpected kind_");
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

// Result of the factory call. kWrongGeneration may happen in the call to
// Accept*
class FileDescriptorResult final : public PosixResult {
 public:
  static FileDescriptorResult WrongGeneration() {
    return FileDescriptorResult(OperationResultKind::kWrongGeneration, 0);
  }

  FileDescriptorResult() = default;
  explicit FileDescriptorResult(const FileDescriptor& fd)
      : PosixResult(OperationResultKind::kSuccess, 0), fd_(fd) {}
  FileDescriptorResult(OperationResultKind kind, int errno_value)
      : PosixResult(kind, errno_value) {}

  FileDescriptor operator*() const {
    CHECK_OK(status());
    return fd_;
  }

  const FileDescriptor* operator->() const {
    CHECK_OK(status());
    return &fd_;
  }

  bool ok() const override { return PosixResult::ok() && fd_.ready(); }

  template <typename R, typename Fn>
  R if_ok(R if_bad, const Fn& fn) {
    if (ok()) {
      return fn(fd_);
    } else {
      return std::move(if_bad);
    }
  }

 private:
  // gRPC wrapped FileDescriptor, as described above
  FileDescriptor fd_;
};

class FileDescriptorCollection {
 public:
  static constexpr int kIntFdBits = 28;
  static constexpr int kGenerationMask = 0x7;  // 3 bits

  FileDescriptor Add(int fd);
  std::optional<int> Remove(const FileDescriptor& fd);

  FileDescriptorResult RegisterPosixResult(int result);

  std::optional<int> GetRawFileDescriptor(const FileDescriptor& fd) const;

  int ToInteger(const FileDescriptor& fd) const;
  FileDescriptorResult FromInteger(int fd) const;

  // Advances the generation, clears the list of fds and returns them
  std::unordered_set<int> AdvanceGeneration();

  bool IsCorrectGeneration(const FileDescriptor& fd) const {
    return fd.generation() ==
           current_generation_.load(std::memory_order_relaxed);
  }

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
