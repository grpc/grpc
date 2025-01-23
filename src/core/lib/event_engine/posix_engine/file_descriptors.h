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
#include <cstdint>

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

  template <typename Sink>
  friend void AbslStringify(Sink& sink, FileDescriptor fd) {
    sink.Append(absl::StrFormat("FD(%d)", fd.fd()));
  }

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
  sink.Append(kind == OperationResultKind::kSuccess ? "(Success)"
              : kind == OperationResultKind::kError ? "(Success)"
                                                    : "(Success)");
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

// Result of the factory call. kWrongGeneration may happen in the call to
// Accept*
class FileDescriptorResult final : public PosixResult {
 public:
  FileDescriptorResult() = default;
  explicit FileDescriptorResult(const FileDescriptor& fd)
      : PosixResult(OperationResultKind::kSuccess, 0), fd_(fd) {}
  FileDescriptorResult(OperationResultKind kind, int errno_value)
      : PosixResult(kind, errno_value) {}

  FileDescriptor operator*() const {
    CHECK_OK(status());
    return fd_;
  }

  int fd() const {
    CHECK_OK(status());
    return fd_.fd();
  }

  bool ok() const override { return PosixResult::ok() && fd_.fd() > 0; }

 private:
  // gRPC wrapped FileDescriptor, as described above
  FileDescriptor fd_;
};

// Result of the call that returns error or ssize_t. Smaller integer types
// will also be packed here.
class Int64Result final : public PosixResult {
 public:
  Int64Result() = default;
  explicit Int64Result(int64_t result)
      : PosixResult(OperationResultKind::kSuccess, 0), result_(result) {}
  Int64Result(OperationResultKind kind, int errno_value, int64_t result)
      : PosixResult(kind, errno_value), result_(result) {}

  int64_t operator*() const { return result_; }

 private:
  int64_t result_ = 0;
};

class FileDescriptors {
 public:
  FileDescriptors() = default;
  FileDescriptors(const FileDescriptors&& other) = delete;

  FileDescriptorResult Accept(const FileDescriptor& sockfd,
                              struct sockaddr* addr, socklen_t* addrlen);
  FileDescriptorResult Accept4(const FileDescriptor& sockfd,
                               EventEngine::ResolvedAddress& addr, int nonblock,
                               int cloexec);
  FileDescriptor Adopt(int fd);

  void Close(const FileDescriptor& fd);

  // Returns nullopt if the file descriptor is not usable
  std::optional<int> GetRawFileDescriptor(const FileDescriptor& fd);

  // Posix
  PosixResult Ioctl(const FileDescriptor& fd, int op, void* arg);
  PosixResult Shutdown(const FileDescriptor& fd, int how);
  PosixResult GetSockOpt(const FileDescriptor& fd, int level, int optname,
                         void* optval, void* optlen);
  Int64Result SetSockOpt(const FileDescriptor& fd, int level, int optname,
                         uint32_t optval);
  Int64Result RecvMsg(const FileDescriptor& fd, struct msghdr* message,
                      int flags);
  Int64Result SendMsg(const FileDescriptor& fd, const struct msghdr* message,
                      int flags);

  // Epoll
  PosixResult EpollCtlAdd(int epfd, const FileDescriptor& fd, void* data);
  PosixResult EpollCtlDel(int epfd, const FileDescriptor& fd);

  // Return LocalAddress as EventEngine::ResolvedAddress
  absl::StatusOr<EventEngine::ResolvedAddress> LocalAddress(
      const FileDescriptor& fd);
  // Return LocalAddress as string
  absl::StatusOr<std::string> LocalAddressString(const FileDescriptor& fd);
  // Return PeerAddress as EventEngine::ResolvedAddress
  absl::StatusOr<EventEngine::ResolvedAddress> PeerAddress(
      const FileDescriptor& fd);
  // Return PeerAddress as string
  absl::StatusOr<std::string> PeerAddressString(const FileDescriptor& fd);

 private:
  FileDescriptorResult RegisterPosixResult(int result);
};

}  // namespace grpc_event_engine::experimental

#endif  // GRPC_SRC_CORE_LIB_EVENT_ENGINE_POSIX_ENGINE_FILE_DESCRIPTORS_H