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
#include <utility>

#include "absl/log/check.h"
#include "absl/status/status.h"
#include "absl/strings/str_format.h"
#include "src/core/lib/event_engine/posix_engine/tcp_socket_utils.h"

namespace grpc_event_engine::experimental {

class FileDescriptor {
 public:
  FileDescriptor() = default;
  explicit FileDescriptor(int fd) : fd_(fd) {};
  bool ready() const { return fd_ > 0; }
  // Escape for iomgr and tests. Not to be used elsewhere
  int iomgr_fd() const { return fd_; }
  // For logging/debug purposes - may consider including generation, do not
  // use for Posix calls!
  int debug_fd() const { return fd_; }

  template <typename Sink>
  friend void AbslStringify(Sink& sink, FileDescriptor fd) {
    sink.Append(absl::StrFormat("FD(%d)", fd.fd()));
  }

 private:
  int fd() const { return fd_; }

  // Can get fd_!
  friend class FileDescriptors;

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
  struct PosixSocketCreateResult {
    FileDescriptor sock;
    EventEngine::ResolvedAddress mapped_target_addr;
  };

  FileDescriptors() = default;
  FileDescriptors(const FileDescriptors&& other) = delete;

  std::optional<int> GetFdForPolling(const FileDescriptor& fd);

  static void ConfigureDefaultTcpUserTimeout(bool enable, int timeout,
                                             bool is_client);

  FileDescriptorResult Accept(const FileDescriptor& sockfd,
                              struct sockaddr* addr, socklen_t* addrlen);
  FileDescriptorResult Accept4(const FileDescriptor& sockfd,
                               EventEngine::ResolvedAddress& addr, int nonblock,
                               int cloexec);
  FileDescriptorResult EventFd(int initval, int flags);
  FileDescriptorResult Socket(int domain, int type, int protocol);
  absl::StatusOr<std::pair<FileDescriptor, FileDescriptor>> Pipe();
  FileDescriptorResult EpollCreateAndCloexec();

  // Represents fd as integer. Needed for APIs like ARES, that need to have
  // a single int as a handle.
  int AsInteger(const FileDescriptor& fd);
  // May return a wrong generation error
  FileDescriptorResult FromInteger(int fd);

  // Creates a new socket for connecting to (or listening on) an address.
  //
  // If addr is AF_INET6, this creates an IPv6 socket first.  If that fails,
  // and addr is within ::ffff:0.0.0.0/96, then it automatically falls back
  // to an IPv4 socket.
  //
  // If addr is AF_INET, AF_UNIX, or anything else, then this is similar to
  // calling socket() directly.
  //
  // Returns an PosixSocketWrapper on success, otherwise returns a not-OK
  // absl::Status
  //
  // The dsmode output indicates which address family was actually created.
  absl::StatusOr<FileDescriptor> CreateDualStackSocket(
      std::function<int(int, int, int)> socket_factory,
      const experimental::EventEngine::ResolvedAddress& addr, int type,
      int protocol, DSMode& dsmode);

  FileDescriptor Adopt(int fd);

  void Close(const FileDescriptor& fd);

  // Returns nullopt if the file descriptor is not usable
  std::optional<int> GetRawFileDescriptor(const FileDescriptor& fd);

  // Posix
  PosixResult Connect(const FileDescriptor& sockfd, const struct sockaddr* addr,
                      socklen_t addrlen);
  PosixResult GetSockOpt(const FileDescriptor& fd, int level, int optname,
                         void* optval, void* optlen);
  PosixResult Ioctl(const FileDescriptor& fd, int op, void* arg);
  Int64Result RecvFrom(const FileDescriptor& fd, void* buf, size_t len,
                       int flags, struct sockaddr* src_addr,
                       socklen_t* addrlen);
  Int64Result Read(const FileDescriptor& fd, absl::Span<char> buffer);
  Int64Result RecvMsg(const FileDescriptor& fd, struct msghdr* message,
                      int flags);
  Int64Result SetSockOpt(const FileDescriptor& fd, int level, int optname,
                         uint32_t optval);
  Int64Result SendMsg(const FileDescriptor& fd, const struct msghdr* message,
                      int flags);
  PosixResult Shutdown(const FileDescriptor& fd, int how);
  Int64Result Write(const FileDescriptor& fd, absl::Span<char> buffer);
  Int64Result WriteV(const FileDescriptor& fd, const struct iovec* iov,
                     int iovcnt);

  // Epoll
  PosixResult EpollCtlAdd(const FileDescriptor& epfd, bool writable,
                          const FileDescriptor& fd, void* data);
  PosixResult EpollCtlDel(const FileDescriptor& epfd, const FileDescriptor& fd);

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
  // Tries to set SO_NOSIGPIPE if available on this platform.
  // If SO_NO_SIGPIPE is not available, returns not OK status.
  absl::Status SetSocketNoSigpipeIfPossible(const FileDescriptor& fd);

  // Return a PosixSocketCreateResult which manages a configured, unbound,
  // unconnected TCP client fd.
  //  options: may contain custom tcp settings for the fd.
  //  target_addr: the destination address.
  //
  // Returns: Not-OK status on error. Otherwise it returns a
  // PosixSocketWrapper::PosixSocketCreateResult type which includes a sock
  // of type PosixSocketWrapper and a mapped_target_addr which is
  // target_addr mapped to an address appropriate to the type of socket FD
  // created. For example, if target_addr is IPv4 and dual stack sockets are
  // available, mapped_target_addr will be an IPv4-mapped IPv6 address.
  //
  absl::StatusOr<PosixSocketCreateResult> CreateAndPrepareTcpClientSocket(
      const PosixTcpOptions& options,
      const EventEngine::ResolvedAddress& target_addr);

  // Extracts the first socket mutator from config if any and applies on the fd.
  absl::Status ApplySocketMutatorInOptions(const FileDescriptor& fd,
                                           grpc_fd_usage usage,
                                           const PosixTcpOptions& options);

  // Tries to set the socket using a grpc_socket_mutator
  absl::Status SetSocketMutator(const FileDescriptor& fd, grpc_fd_usage usage,
                                grpc_socket_mutator* mutator);

  absl::StatusOr<EventEngine::ResolvedAddress> PrepareListenerSocket(
      const FileDescriptor& fd, const PosixTcpOptions& options,
      const EventEngine::ResolvedAddress& address);

  int ConfigureSocket(const FileDescriptor& fd, int type);

  absl::StatusOr<int> GetUnusedPort();

  PosixResult EventFdRead(const FileDescriptor& fd);
  PosixResult EventFdWrite(const FileDescriptor& fd);

 private:
  absl::Status PrepareTcpClientSocket(const FileDescriptor& fd,
                                      const EventEngine::ResolvedAddress& addr,
                                      const PosixTcpOptions& options);

  FileDescriptorResult RegisterPosixResult(int result);
};

}  // namespace grpc_event_engine::experimental

#endif  // GRPC_SRC_CORE_LIB_EVENT_ENGINE_POSIX_ENGINE_FILE_DESCRIPTORS_H