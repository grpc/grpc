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

#ifndef GRPC_SRC_CORE_LIB_EVENT_ENGINE_POSIX_ENGINE_FILE_DESCRIPTORS_H
#define GRPC_SRC_CORE_LIB_EVENT_ENGINE_POSIX_ENGINE_FILE_DESCRIPTORS_H

#include <grpc/event_engine/event_engine.h>

#include <cerrno>

#include "absl/log/check.h"
#include "absl/status/status.h"
#include "absl/strings/str_format.h"

namespace grpc_event_engine::experimental {

class FileDescriptor {
 public:
  FileDescriptor() = default;
  explicit FileDescriptor(int fd) : fd_(fd) {};
  int fd() const { return fd_; }
  bool ready() const { return fd_ > 0; }

 private:
  int fd_ = 0;
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
  absl::Format(sink, "%s",
               kind == OperationResultKind::kSuccess ? "(Success)"
               : kind == OperationResultKind::kError ? "(Success)"
                                                     : "(Success)");
}

// Result of the factory call. kWrongGeneration may happen in the call to
// Accept*
class FileDescriptorResult {
 public:
  static FileDescriptorResult FD(const FileDescriptor& fd) {
    return FileDescriptorResult(OperationResultKind::kSuccess, fd, 0);
  }

  static FileDescriptorResult Error() {
    return FileDescriptorResult(OperationResultKind::kError, FileDescriptor(),
                                errno);
  }

  FileDescriptorResult() = default;

  FileDescriptor operator*() const {
    CHECK_OK(status());
    return fd_;
  }

  int fd() const {
    CHECK_OK(status());
    return fd_.fd();
  }

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

  bool ok() const {
    return kind_ == OperationResultKind::kSuccess && fd_.fd() > 0;
  }

  bool IsPosixError(int err) const {
    return kind_ == OperationResultKind::kError && errno_value_ == err;
  }

  OperationResultKind kind() const { return kind_; }
  int errno_value() const { return errno_value_; }

 private:
  FileDescriptorResult(OperationResultKind kind, const FileDescriptor& fd,
                       int errno_value = 0)
      : kind_(kind), fd_(fd), errno_value_(errno_value) {}

  OperationResultKind kind_;
  // gRPC wrapped FileDescriptor, as described above
  FileDescriptor fd_;
  // errno value on call completion, in order to reduce the race conditions
  // from relying on global variable.
  int errno_value_;
};

class FileDescriptors {
 public:
  FileDescriptorResult Accept(int sockfd, struct sockaddr* addr,
                              socklen_t* addrlen);
  FileDescriptorResult Accept4(int sockfd, EventEngine::ResolvedAddress& addr,
                               int nonblock, int cloexec);

  FileDescriptor Adopt(int fd);

  void Close(const FileDescriptor& fd);

 private:
  FileDescriptorResult RegisterPosixResult(int result);
};

}  // namespace grpc_event_engine::experimental

#endif  // GRPC_SRC_CORE_LIB_EVENT_ENGINE_POSIX_ENGINE_FILE_DESCRIPTORS_H