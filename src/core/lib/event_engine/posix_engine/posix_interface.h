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

#ifndef GRPC_SRC_CORE_LIB_EVENT_ENGINE_POSIX_ENGINE_POSIX_INTERFACE_H
#define GRPC_SRC_CORE_LIB_EVENT_ENGINE_POSIX_ENGINE_POSIX_INTERFACE_H

#include <grpc/event_engine/event_engine.h>

#include <cerrno>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <utility>

#include "absl/log/check.h"
#include "absl/status/status.h"
#include "src/core/lib/event_engine/posix_engine/file_descriptor_collection.h"
#include "src/core/lib/event_engine/posix_engine/tcp_socket_utils.h"

namespace grpc_event_engine::experimental {

class EventEnginePosixInterface {
 public:
  struct PosixSocketCreateResult {
    FileDescriptor sock;
    EventEngine::ResolvedAddress mapped_target_addr;
  };

  EventEnginePosixInterface() = default;
  EventEnginePosixInterface(const EventEnginePosixInterface& other) = delete;
  EventEnginePosixInterface(EventEnginePosixInterface&& other) = delete;

  void AdvanceGeneration();

  std::optional<int> GetFdForPolling(const FileDescriptor& fd);

  static void ConfigureDefaultTcpUserTimeout(bool enable, int timeout,
                                             bool is_client);

  PosixErrorOr<FileDescriptor> Accept(const FileDescriptor& sockfd,
                                      struct sockaddr* addr,
                                      socklen_t* addrlen);
  PosixErrorOr<FileDescriptor> Accept4(const FileDescriptor& sockfd,
                                       EventEngine::ResolvedAddress& addr,
                                       int nonblock, int cloexec);
  PosixErrorOr<FileDescriptor> EventFd(int initval, int flags);
  PosixErrorOr<FileDescriptor> Socket(int domain, int type, int protocol);
  absl::StatusOr<std::pair<FileDescriptor, FileDescriptor>> Pipe();
  PosixErrorOr<FileDescriptor> EpollCreateAndCloexec();

  // Represents fd as integer. Needed for APIs like ARES, that need to have
  // a single int as a handle.
  int ToInteger(const FileDescriptor& fd) {
#if GRPC_ENABLE_FORK_SUPPORT
    static const auto kToInteger =
        grpc_core::IsEventEngineForkEnabled()
            ? [](const FileDescriptorCollection& collection,
                 const FileDescriptor& fd) { return collection.ToInteger(fd); }
            : [](const FileDescriptorCollection& collection,
                 const FileDescriptor& fd) { return fd.fd(); };
    return kToInteger(descriptors_, fd);
#else   // GRPC_ENABLE_FORK_SUPPORT
    return fd.fd();
#endif  // GRPC_ENABLE_FORK_SUPPORT
  }

  // May return a wrong generation error
  PosixErrorOr<FileDescriptor> FromInteger(int fd) {
#if GRPC_ENABLE_FORK_SUPPORT
    static const auto kFromInteger =
        grpc_core::IsEventEngineForkEnabled()
            ? [](const FileDescriptorCollection& collection,
                 int fd) { return collection.FromInteger(fd); }
            : [](const FileDescriptorCollection& collection, int fd) {
                return PosixErrorOr<FileDescriptor>(FileDescriptor(fd, 0));
              };
    return kFromInteger(descriptors_, fd);
#else   // GRPC_ENABLE_FORK_SUPPORT
    return PosixErrorOr<FileDescriptor>(FileDescriptor(fd, 0));
#endif  // GRPC_ENABLE_FORK_SUPPORT
  }

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

  // Posix
  PosixErrorOr<void> Connect(const FileDescriptor& sockfd,
                             const struct sockaddr* addr, socklen_t addrlen);
  PosixErrorOr<void> GetSockOpt(const FileDescriptor& fd, int level,
                                int optname, void* optval, void* optlen);
  PosixErrorOr<void> Ioctl(const FileDescriptor& fd, int op, void* arg);
  PosixErrorOr<int64_t> RecvFrom(const FileDescriptor& fd, void* buf,
                                 size_t len, int flags,
                                 struct sockaddr* src_addr, socklen_t* addrlen);
  PosixErrorOr<int64_t> Read(const FileDescriptor& fd, absl::Span<char> buffer);
  PosixErrorOr<int64_t> RecvMsg(const FileDescriptor& fd,
                                struct msghdr* message, int flags);
  PosixErrorOr<int64_t> SetSockOpt(const FileDescriptor& fd, int level,
                                   int optname, uint32_t optval);
  PosixErrorOr<int64_t> SendMsg(const FileDescriptor& fd,
                                const struct msghdr* message, int flags);
  PosixErrorOr<void> Shutdown(const FileDescriptor& fd, int how);
  PosixErrorOr<int64_t> Write(const FileDescriptor& fd,
                              absl::Span<char> buffer);
  PosixErrorOr<int64_t> WriteV(const FileDescriptor& fd,
                               const struct iovec* iov, int iovcnt);

  // Epoll
  PosixErrorOr<void> EpollCtlAdd(const FileDescriptor& epfd, bool writable,
                                 const FileDescriptor& fd, void* data);
  PosixErrorOr<void> EpollCtlDel(const FileDescriptor& epfd,
                                 const FileDescriptor& fd);

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

  PosixErrorOr<void> EventFdRead(const FileDescriptor& fd);
  PosixErrorOr<void> EventFdWrite(const FileDescriptor& fd);

 private:
  absl::Status PrepareTcpClientSocket(int fd,
                                      const EventEngine::ResolvedAddress& addr,
                                      const PosixTcpOptions& options);

  PosixErrorOr<void> PosixResultWrap(
      const FileDescriptor& wrapped,
      const absl::AnyInvocable<int(int) const>& fn) const;

  // Need parameter R for portability, ssize_t is neither available everywhere
  // nor is the same cardinality
  template <typename R, typename Fn>
  R RunIfCorrectGeneration(const FileDescriptor& fd, const Fn& fn,
                           R&& r) const {
#if GRPC_ENABLE_FORK_SUPPORT
    if (!IsCorrectGeneration(fd)) {
      return std::forward<R>(r);
    }
#else   // GRPC_ENABLE_FORK_SUPPORT
    (void)r;  // Get rid of the unused warning
#endif  // GRPC_ENABLE_FORK_SUPPORT
    return std::invoke(fn, fd.fd());
  }

  template <typename... Args>
  PosixErrorOr<int64_t> PosixResultWrap(const FileDescriptor& fd,
                                        int (*fn)(int, Args...),
                                        Args&&... args) const {
    return RunIfCorrectGeneration(
        fd,
        [&](int raw) {
          int64_t result = std::invoke(fn, raw, std::forward<Args>(args)...);
          return result < 0 ? PosixErrorOr<int64_t>::Error(errno)
                            : PosixErrorOr<int64_t>(result);
        },
        PosixErrorOr<int64_t>::WrongGeneration());
  }

  // Templated Fn to make it easier to compile on all platforms.
  template <typename Fn, typename... Args>
  PosixErrorOr<int64_t> Int64Wrap(const FileDescriptor& fd, const Fn& fn,
                                  Args&&... args) const {
    return RunIfCorrectGeneration(
        fd,
        [&](int raw) {
          auto result = std::invoke(fn, raw, std::forward<Args>(args)...);
          return result < 0 ? PosixErrorOr<int64_t>::Error(errno)
                            : PosixErrorOr<int64_t>(result);
        },
        PosixErrorOr<int64_t>::WrongGeneration());
  }

  bool IsCorrectGeneration(const FileDescriptor& fd) const {
#if GRPC_ENABLE_FORK_SUPPORT
    static const auto kIsGeneration =
        grpc_core::IsEventEngineForkEnabled()
            ? [](const FileDescriptorCollection& collection,
                 const FileDescriptor&
                     fd) { return collection.generation() == fd.generation(); }
            : [](const FileDescriptorCollection& collection,
                 const FileDescriptor& fd) { return true; };
    return kIsGeneration(descriptors_, fd);
#else   // GRPC_ENABLE_FORK_SUPPORT
    (void)fd;
    return true;
#endif  // GRPC_ENABLE_FORK_SUPPORT
  }

  PosixErrorOr<FileDescriptor> RegisterPosixResult(int result) {
    if (result > 0) {
      return PosixErrorOr<FileDescriptor>(Adopt(result));
    } else {
      return PosixErrorOr<FileDescriptor>::Error(errno);
    }
  }

#if GRPC_ENABLE_FORK_SUPPORT
  FileDescriptorCollection descriptors_;
#endif  // GRPC_ENABLE_FORK_SUPPORT
};

}  // namespace grpc_event_engine::experimental

#endif  // GRPC_SRC_CORE_LIB_EVENT_ENGINE_POSIX_ENGINE_POSIX_INTERFACE_H