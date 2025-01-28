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

#include "src/core/lib/event_engine/posix_engine/file_descriptors.h"

#include <grpc/event_engine/event_engine.h>
#include <grpc/support/port_platform.h>
#include <sys/types.h>

#include <cerrno>
#include <cstdint>

#include "absl/cleanup/cleanup.h"
#include "absl/log/log.h"
#include "absl/strings/str_cat.h"
#include "src/core/lib/event_engine/tcp_socket_utils.h"
#include "src/core/lib/iomgr/port.h"
#include "src/core/util/crash.h"  // IWYU pragma: keep
#include "src/core/util/status_helper.h"
#include "src/core/util/strerror.h"

#ifdef GRPC_POSIX_SOCKET_UTILS_COMMON
#include <arpa/inet.h>  // IWYU pragma: keep
#ifdef GRPC_LINUX_TCP_H
#include <linux/tcp.h>
#else
#include <netinet/in.h>  // IWYU pragma: keep
#include <netinet/tcp.h>
#endif
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>
#endif  //  GRPC_POSIX_SOCKET_UTILS_COMMON

// File needs to be compilable on all platforms. These macros will produce stubs
// if specific feature is not available in specific environment
#ifdef GRPC_POSIX_SOCKET
#define IF_POSIX_SOCKET(signature, body) signature body
#ifdef GRPC_LINUX_EPOLL
#include <sys/epoll.h>
#define IF_EPOLL(signature, body) signature body
#else  // GRPC_LINUX_EPOLL
#define IF_EPOLL(signature, body) \
  signature { grpc_core::Crash("unimplemented"); }
#endif  // GRPC_LINUX_EPOLL
#else   // GRPC_POSIX_SOCKET
#define IF_POSIX_SOCKET(signature, body) \
  signature { grpc_core::Crash("unimplemented"); }
#define IF_EPOLL(signature, body) \
  signature { grpc_core::Crash("unimplemented"); }
#endif  // GRPC_POSIX_SOCKET

#if GRPC_ARES == 1 && defined(GRPC_POSIX_SOCKET_ARES_EV_DRIVER)
#include <sys/uio.h>
#endif  // GRPC_ARES == 1 && defined(GRPC_POSIX_SOCKET_ARES_EV_DRIVER)

#if GPR_LINUX == 1
// For Linux, it will be detected to support TCP_USER_TIMEOUT
#ifndef TCP_USER_TIMEOUT
#define TCP_USER_TIMEOUT 18
#endif
#define SOCKET_SUPPORTS_TCP_USER_TIMEOUT_DEFAULT 0
#else
// For non-Linux, TCP_USER_TIMEOUT will be used if TCP_USER_TIMEOUT is defined.
#ifdef TCP_USER_TIMEOUT
#define SOCKET_SUPPORTS_TCP_USER_TIMEOUT_DEFAULT 0
#else
#define TCP_USER_TIMEOUT 0
#define SOCKET_SUPPORTS_TCP_USER_TIMEOUT_DEFAULT -1
#endif  // TCP_USER_TIMEOUT
#endif  // GPR_LINUX == 1

namespace grpc_event_engine::experimental {

namespace {

// The default values for TCP_USER_TIMEOUT are currently configured to be in
// line with the default values of KEEPALIVE_TIMEOUT as proposed in
// https://github.com/grpc/proposal/blob/master/A18-tcp-user-timeout.md */
int kDefaultClientUserTimeoutMs = 20000;
int kDefaultServerUserTimeoutMs = 20000;
bool kDefaultClientUserTimeoutEnabled = false;
bool kDefaultServerUserTimeoutEnabled = true;

#ifdef GRPC_POSIX_SOCKET
// Whether the socket supports TCP_USER_TIMEOUT option.
// (0: don't know, 1: support, -1: not support)
std::atomic<int> g_socket_supports_tcp_user_timeout(
    SOCKET_SUPPORTS_TCP_USER_TIMEOUT_DEFAULT);
#endif  // GRPC_POSIX_SOCKET

PosixResult PosixResultSuccess() {
  return PosixResult(OperationResultKind::kSuccess, 0);
}

PosixResult PosixResultError() {
  return PosixResult(OperationResultKind::kError, errno);
}

PosixResult PosixResultWrap(int result) {
  return result == 0 ? PosixResultSuccess() : PosixResultError();
}

Int64Result Int64Wrap(int64_t result) {
  return result < 0 ? Int64Result(OperationResultKind::kError, errno, result)
                    : Int64Result(result);
}

absl::Status ErrorForFd(
    int fd, const experimental::EventEngine::ResolvedAddress& addr) {
  if (fd >= 0) return absl::OkStatus();
  const char* addr_str = reinterpret_cast<const char*>(addr.address());
  return absl::Status(absl::StatusCode::kInternal,
                      absl::StrCat("socket: ", grpc_core::StrError(errno),
                                   std::string(addr_str, addr.size())));
}

IF_POSIX_SOCKET(
    int CreateSocket(std::function<int(int, int, int)> socket_factory,
                     int family, int type, int protocol),
    {
      int res = socket_factory != nullptr
                    ? socket_factory(family, type, protocol)
                    : socket(family, type, protocol);
      if (res < 0 && errno == EMFILE) {
        int saved_errno = errno;
        LOG_EVERY_N_SEC(ERROR, 10)
            << "socket(" << family << ", " << type << ", " << protocol
            << ") returned " << res << " with error: |"
            << grpc_core::StrError(errno)
            << "|. This process might not have a sufficient file descriptor "
               "limit "
               "for the number of connections grpc wants to open (which is "
               "generally a function of the number of grpc channels, the lb "
               "policy "
               "of each channel, and the number of backends each channel is "
               "load "
               "balancing across).";
        errno = saved_errno;
      }
      return res;
    })

}  // namespace

bool IsSocketReusePortSupported() {
  static bool kSupportSoReusePort = []() -> bool {
    FileDescriptors fds;
    auto s = fds.Socket(AF_INET, SOCK_STREAM, 0);
    if (!s.ok()) {
      // This might be an ipv6-only environment in which case
      // 'socket(AF_INET,..)' call would fail. Try creating IPv6 socket in
      // that case
      s = fds.Socket(AF_INET6, SOCK_STREAM, 0);
    }
    if (s.ok()) {
      bool result = fds.SetSocketReusePort(*s, 1).ok();
      fds.Close(*s);
      return result;
    } else {
      return false;
    }
  }();
  return kSupportSoReusePort;
}

void FileDescriptors::ConfigureDefaultTcpUserTimeout(bool enable, int timeout,
                                                     bool is_client) {
  if (is_client) {
    kDefaultClientUserTimeoutEnabled = enable;
    if (timeout > 0) {
      kDefaultClientUserTimeoutMs = timeout;
    }
  } else {
    kDefaultServerUserTimeoutEnabled = enable;
    if (timeout > 0) {
      kDefaultServerUserTimeoutMs = timeout;
    }
  }
}

FileDescriptor FileDescriptors::Adopt(int fd) { return FileDescriptor(fd); }

std::optional<int> FileDescriptors::GetRawFileDescriptor(
    const FileDescriptor& fd) {
  return fd.fd();
}

FileDescriptorResult FileDescriptors::RegisterPosixResult(int result) {
  if (result > 0) {
    return FileDescriptorResult(Adopt(result));
  } else {
    return FileDescriptorResult(OperationResultKind::kError, errno);
  }
}

IF_POSIX_SOCKET(void FileDescriptors::Close(const FileDescriptor& fd),
                { close(fd.fd()); })

//
// Factories
//
IF_POSIX_SOCKET(
    FileDescriptorResult FileDescriptors::Accept(const FileDescriptor& sockfd,
                                                 struct sockaddr* addr,
                                                 socklen_t* addrlen),
    { return RegisterPosixResult(accept(sockfd.fd(), addr, addrlen)); })

#ifdef GRPC_POSIX_SOCKETUTILS

IF_POSIX_SOCKET(
    FileDescriptorResult FileDescriptors::Accept4(
        const FileDescriptor& sockfd,
        grpc_event_engine::experimental::EventEngine::ResolvedAddress& addr,
        int nonblock, int cloexec),
    {
      EventEngine::ResolvedAddress peer_addr;
      socklen_t len = EventEngine::ResolvedAddress::MAX_SIZE_BYTES;
      FileDescriptorResult fd =
          Accept(sockfd, const_cast<sockaddr*>(peer_addr.address()), &len);
      if (!fd.ok()) {
        return fd;
      }
      int flags;
      int fdescriptor = fd.fd();
      if (nonblock) {
        flags = fcntl(fdescriptor, F_GETFL, 0);
        if (flags < 0) goto close_and_error;
        if (fcntl(fdescriptor, F_SETFL, flags | O_NONBLOCK) != 0) {
          goto close_and_error;
        }
      }
      if (cloexec) {
        flags = fcntl(fdescriptor, F_GETFD, 0);
        if (flags < 0) goto close_and_error;
        if (fcntl(fdescriptor, F_SETFD, flags | FD_CLOEXEC) != 0) {
          goto close_and_error;
        }
      }
      addr = EventEngine::ResolvedAddress(peer_addr.address(), len);
      return fd;
    close_and_error:
      FileDescriptorResult result(OperationResultKind::kError, errno);
      Close(*fd);
      return result;
    })

#else  // GRPC_POSIX_SOCKETUTILS

IF_POSIX_SOCKET(
    FileDescriptorResult FileDescriptors::Accept4(
        const FileDescriptor& sockfd,
        grpc_event_engine::experimental::EventEngine::ResolvedAddress& addr,
        int nonblock, int cloexec),
    {
      int flags = 0;
      flags |= nonblock ? SOCK_NONBLOCK : 0;
      flags |= cloexec ? SOCK_CLOEXEC : 0;
      EventEngine::ResolvedAddress peer_addr;
      socklen_t len = EventEngine::ResolvedAddress::MAX_SIZE_BYTES;
      FileDescriptorResult ret = RegisterPosixResult(
          accept4(sockfd.fd(), const_cast<sockaddr*>(peer_addr.address()), &len,
                  flags));
      if (ret.ok()) {
        addr = EventEngine::ResolvedAddress(peer_addr.address(), len);
      }
      return ret;
    })

#endif  // GRPC_POSIX_SOCKETUTILS

IF_POSIX_SOCKET(
    absl::StatusOr<FileDescriptor> FileDescriptors::CreateDualStackSocket(
        std::function<int(int, int, int)> socket_factory,
        const experimental::EventEngine::ResolvedAddress& addr, int type,
        int protocol, PosixSocketWrapper::DSMode& dsmode),
    {
      const sockaddr* sock_addr = addr.address();
      int family = sock_addr->sa_family;
      int newfd;
      if (family == AF_INET6) {
        if (PosixSocketWrapper::IsIpv6LoopbackAvailable()) {
          newfd = CreateSocket(socket_factory, family, type, protocol);
        } else {
          newfd = -1;
          errno = EAFNOSUPPORT;
        }
        // Check if we've got a valid dualstack socket.
        if (newfd > 0 && SetSocketDualStack(newfd)) {
          dsmode = PosixSocketWrapper::DSMode::DSMODE_DUALSTACK;
          return Adopt(newfd);
        }
        // If this isn't an IPv4 address, then return whatever we've got.
        if (!ResolvedAddressIsV4Mapped(addr, nullptr)) {
          if (newfd < 0) {
            return ErrorForFd(newfd, addr);
          }
          dsmode = PosixSocketWrapper::DSMode::DSMODE_IPV6;
          return Adopt(newfd);
        }
        // Fall back to AF_INET.
        if (newfd >= 0) {
          close(newfd);
        }
        family = AF_INET;
      }
      dsmode = family == AF_INET ? PosixSocketWrapper::DSMode::DSMODE_IPV4
                                 : PosixSocketWrapper::DSMode::DSMODE_NONE;
      newfd = CreateSocket(socket_factory, family, type, protocol);
      if (newfd < 0) {
        return ErrorForFd(newfd, addr);
      }
      return Adopt(newfd);
    })

IF_POSIX_SOCKET(FileDescriptorResult FileDescriptors::Socket(int domain,
                                                             int type,
                                                             int protocol),
                { return RegisterPosixResult(socket(domain, type, protocol)); })

int FileDescriptors::AsInteger(const FileDescriptor& fd) { return fd.fd(); }

FileDescriptorResult FileDescriptors::FromInteger(int fd) {
  return FileDescriptorResult(FileDescriptor(fd));
}

IF_POSIX_SOCKET(
    PosixResult FileDescriptors::Connect(const FileDescriptor& sockfd,
                                         const struct sockaddr* addr,
                                         socklen_t addrlen),
    { return PosixResultWrap(connect(sockfd.fd(), addr, addrlen)); })

IF_POSIX_SOCKET(PosixResult FileDescriptors::Ioctl(const FileDescriptor& fd,
                                                   int op, void* arg),
                { return PosixResultWrap(ioctl(fd.fd(), op, arg)); });

IF_POSIX_SOCKET(PosixResult FileDescriptors::Shutdown(const FileDescriptor& fd,
                                                      int how),
                { return PosixResultWrap(shutdown(fd.fd(), how)); })

IF_POSIX_SOCKET(
    PosixResult FileDescriptors::GetSockOpt(const FileDescriptor& fd, int level,
                                            int optname, void* optval,
                                            void* optlen),
    {
      return PosixResultWrap(getsockopt(fd.fd(), level, optname, optval,
                                        static_cast<socklen_t*>(optlen)));
    })

IF_POSIX_SOCKET(
    Int64Result FileDescriptors::SetSockOpt(const FileDescriptor& fd, int level,
                                            int optname, uint32_t optval),
    {
      if (setsockopt(fd.fd(), level, optname, &optval, sizeof(optval)) < 0) {
        return Int64Result(OperationResultKind::kError, errno, optval);
      } else {
        return Int64Result(optval);
      }
    })

IF_POSIX_SOCKET(Int64Result FileDescriptors::RecvFrom(
                    const FileDescriptor& fd, void* buf, size_t len, int flags,
                    struct sockaddr* src_addr, socklen_t* addrlen),
                {
                  return Int64Wrap(
                      recvfrom(fd.fd(), buf, len, flags, src_addr, addrlen));
                })

IF_POSIX_SOCKET(Int64Result FileDescriptors::RecvMsg(const FileDescriptor& fd,
                                                     struct msghdr* message,
                                                     int flags),
                { return Int64Wrap(recvmsg(fd.fd(), message, flags)); })

IF_POSIX_SOCKET(Int64Result FileDescriptors::SendMsg(
                    const FileDescriptor& fd, const struct msghdr* message,
                    int flags),
                { return Int64Wrap(sendmsg(fd.fd(), message, flags)); })

Int64Result FileDescriptors::WriteV(const FileDescriptor& fd,
                                    const struct iovec* iov, int iovcnt) {
#if defined(GRPC_POSIX_SOCKET) && GRPC_ARES == 1 && \
    defined(GRPC_POSIX_SOCKET_ARES_EV_DRIVER)
  return Int64Wrap(writev(fd.fd(), iov, iovcnt));
#else   // GRPC_ARES == 1 && defined(GRPC_POSIX_SOCKET_ARES_EV_DRIVER)
  grpc_core::Crash("Not available");
#endif  // GRPC_ARES == 1 && defined(GRPC_POSIX_SOCKET_ARES_EV_DRIVER)
}

//
// Epoll
//
IF_EPOLL(PosixResult FileDescriptors::EpollCtlDel(int epfd,
                                                  const FileDescriptor& fd),
         {
           epoll_event phony_event;
           return PosixResultWrap(
               epoll_ctl(epfd, EPOLL_CTL_DEL, fd.fd(), &phony_event));
         })

IF_EPOLL(PosixResult FileDescriptors::EpollCtlAdd(int epfd,
                                                  const FileDescriptor& fd,
                                                  void* data),
         {
           epoll_event event;
           event.events = static_cast<uint32_t>(EPOLLIN | EPOLLOUT | EPOLLET);
           event.data.ptr = data;
           return PosixResultWrap(
               epoll_ctl(epfd, EPOLL_CTL_ADD, fd.fd(), &event));
         })

absl::StatusOr<EventEngine::ResolvedAddress> FileDescriptors::LocalAddress(
    const FileDescriptor& fd) {
  EventEngine::ResolvedAddress addr;
  socklen_t len = EventEngine::ResolvedAddress::MAX_SIZE_BYTES;
  if (getsockname(fd.fd(), const_cast<sockaddr*>(addr.address()), &len) < 0) {
    return absl::InternalError(
        absl::StrCat("getsockname:", grpc_core::StrError(errno)));
  }
  return EventEngine::ResolvedAddress(addr.address(), len);
}

absl::StatusOr<std::string> FileDescriptors::LocalAddressString(
    const FileDescriptor& fd) {
  auto status = LocalAddress(fd);
  if (!status.ok()) {
    return status.status();
  }
  return ResolvedAddressToNormalizedString((*status));
}

absl::StatusOr<EventEngine::ResolvedAddress> FileDescriptors::PeerAddress(
    const FileDescriptor& fd) {
  EventEngine::ResolvedAddress addr;
  socklen_t len = EventEngine::ResolvedAddress::MAX_SIZE_BYTES;
  if (getpeername(fd.fd(), const_cast<sockaddr*>(addr.address()), &len) < 0) {
    return absl::InternalError(
        absl::StrCat("getpeername:", grpc_core::StrError(errno)));
  }
  return EventEngine::ResolvedAddress(addr.address(), len);
}

absl::StatusOr<std::string> FileDescriptors::PeerAddressString(
    const FileDescriptor& fd) {
  auto status = PeerAddress(fd);
  if (!status.ok()) {
    return status.status();
  }
  return ResolvedAddressToNormalizedString((*status));
}

absl::Status FileDescriptors::SetSocketNoSigpipeIfPossible(
    GRPC_UNUSED const FileDescriptor& fd) {
#ifdef GRPC_HAVE_SO_NOSIGPIPE
  int val = 1;
  int newval;
  socklen_t intlen = sizeof(newval);
  int fd_posix = fd.fd();
  if (0 != setsockopt(fd_posix, SOL_SOCKET, SO_NOSIGPIPE, &val, sizeof(val))) {
    return absl::Status(
        absl::StatusCode::kInternal,
        absl::StrCat("setsockopt(SO_NOSIGPIPE): ", grpc_core::StrError(errno)));
  }
  if (0 != getsockopt(fd_posix, SOL_SOCKET, SO_NOSIGPIPE, &newval, &intlen)) {
    return absl::Status(
        absl::StatusCode::kInternal,
        absl::StrCat("getsockopt(SO_NOSIGPIPE): ", grpc_core::StrError(errno)));
  }
  if ((newval != 0) != (val != 0)) {
    return absl::Status(absl::StatusCode::kInternal,
                        "Failed to set SO_NOSIGPIPE");
  }
#endif
  return absl::OkStatus();
}

IF_POSIX_SOCKET(
    absl::Status FileDescriptors::PrepareTcpClientSocket(
        const FileDescriptor& fd, const EventEngine::ResolvedAddress& addr,
        const PosixTcpOptions& options),
    {
      bool close_fd = true;
      auto sock_cleanup = absl::MakeCleanup([&close_fd, &fd, this]() -> void {
        if (close_fd and fd.fd()) {
          Close(fd);
        }
      });
      GRPC_RETURN_IF_ERROR(SetSocketNonBlocking(fd, 1));
      GRPC_RETURN_IF_ERROR(SetSocketCloexec(fd, 1));
      if (options.tcp_receive_buffer_size != options.kReadBufferSizeUnset) {
        GRPC_RETURN_IF_ERROR(
            SetSocketRcvBuf(fd, options.tcp_receive_buffer_size));
      }
      if (addr.address()->sa_family != AF_UNIX &&
          !ResolvedAddressIsVSock(addr)) {
        // If its not a unix socket or vsock address.
        GRPC_RETURN_IF_ERROR(SetSocketLowLatency(fd, 1));
        GRPC_RETURN_IF_ERROR(SetSocketReuseAddr(fd, 1));
        GRPC_RETURN_IF_ERROR(SetSocketDscp(fd, options.dscp));
        TrySetSocketTcpUserTimeout(fd, options, true);
      }
      GRPC_RETURN_IF_ERROR(SetSocketNoSigpipeIfPossible(fd));
      GRPC_RETURN_IF_ERROR(ApplySocketMutatorInOptions(
          fd, GRPC_FD_CLIENT_CONNECTION_USAGE, options));
      // No errors. Set close_fd to false to ensure the socket is
      // not closed.
      close_fd = false;
      return absl::OkStatus();
    })

absl::StatusOr<FileDescriptors::PosixSocketCreateResult>
FileDescriptors::CreateAndPrepareTcpClientSocket(
    const PosixTcpOptions& options,
    const EventEngine::ResolvedAddress& target_addr) {
  PosixSocketWrapper::DSMode dsmode;
  EventEngine::ResolvedAddress mapped_target_addr;

  // Use dualstack sockets where available. Set mapped to v6 or v4 mapped to
  // v6.
  if (!ResolvedAddressToV4Mapped(target_addr, &mapped_target_addr)) {
    // addr is v4 mapped to v6 or just v6.
    mapped_target_addr = target_addr;
  }
  absl::StatusOr<FileDescriptor> posix_socket_wrapper = CreateDualStackSocket(
      nullptr, mapped_target_addr, SOCK_STREAM, 0, dsmode);
  if (!posix_socket_wrapper.ok()) {
    return posix_socket_wrapper.status();
  }

  if (dsmode == PosixSocketWrapper::DSMode::DSMODE_IPV4) {
    // Original addr is either v4 or v4 mapped to v6. Set mapped_addr to v4.
    if (!ResolvedAddressIsV4Mapped(target_addr, &mapped_target_addr)) {
      mapped_target_addr = target_addr;
    }
  }

  auto error = PrepareTcpClientSocket(Adopt(posix_socket_wrapper->fd()),
                                      mapped_target_addr, options);
  if (!error.ok()) {
    return error;
  }
  return PosixSocketCreateResult{*posix_socket_wrapper, mapped_target_addr};
}

// Set a socket using a grpc_socket_mutator
absl::Status FileDescriptors::SetSocketMutator(const FileDescriptor& fd,
                                               grpc_fd_usage usage,
                                               grpc_socket_mutator* mutator) {
  CHECK(mutator);
  if (!grpc_socket_mutator_mutate_fd(mutator, fd.fd(), usage)) {
    return absl::Status(absl::StatusCode::kInternal,
                        "grpc_socket_mutator failed.");
  }
  return absl::OkStatus();
}

absl::Status FileDescriptors::ApplySocketMutatorInOptions(
    const FileDescriptor& fd, grpc_fd_usage usage,
    const PosixTcpOptions& options) {
  if (options.socket_mutator == nullptr) {
    return absl::OkStatus();
  }
  return SetSocketMutator(fd, usage, options.socket_mutator);
}

// Set a socket to use zerocopy
absl::Status FileDescriptors::SetSocketZeroCopy(const FileDescriptor& fd) {
#ifdef GRPC_LINUX_ERRQUEUE
  const int enable = 1;
  auto err =
      setsockopt(fd.fd(), SOL_SOCKET, SO_ZEROCOPY, &enable, sizeof(enable));
  if (err != 0) {
    return absl::Status(
        absl::StatusCode::kInternal,
        absl::StrCat("setsockopt(SO_ZEROCOPY): ", grpc_core::StrError(errno)));
  }
  return absl::OkStatus();
#else
  return absl::Status(absl::StatusCode::kInternal,
                      absl::StrCat("setsockopt(SO_ZEROCOPY): ",
                                   grpc_core::StrError(ENOSYS).c_str()));
#endif
}

// Set a socket to non blocking mode
IF_POSIX_SOCKET(absl::Status FileDescriptors::SetSocketNonBlocking(
                    const FileDescriptor& fd, int non_blocking),
                {
                  int oldflags = fcntl(fd.fd(), F_GETFL, 0);
                  if (oldflags < 0) {
                    return absl::Status(
                        absl::StatusCode::kInternal,
                        absl::StrCat("fcntl: ", grpc_core::StrError(errno)));
                  }

                  if (non_blocking) {
                    oldflags |= O_NONBLOCK;
                  } else {
                    oldflags &= ~O_NONBLOCK;
                  }

                  if (fcntl(fd.fd(), F_SETFL, oldflags) != 0) {
                    return absl::Status(
                        absl::StatusCode::kInternal,
                        absl::StrCat("fcntl: ", grpc_core::StrError(errno)));
                  }

                  return absl::OkStatus();
                })

// Set a socket to close on exec
IF_POSIX_SOCKET(absl::Status FileDescriptors::SetSocketCloexec(
                    const FileDescriptor& fd, int close_on_exec),
                {
                  int oldflags = fcntl(fd.fd(), F_GETFD, 0);
                  if (oldflags < 0) {
                    return absl::Status(
                        absl::StatusCode::kInternal,
                        absl::StrCat("fcntl: ", grpc_core::StrError(errno)));
                  }

                  if (close_on_exec) {
                    oldflags |= FD_CLOEXEC;
                  } else {
                    oldflags &= ~FD_CLOEXEC;
                  }

                  if (fcntl(fd.fd(), F_SETFD, oldflags) != 0) {
                    return absl::Status(
                        absl::StatusCode::kInternal,
                        absl::StrCat("fcntl: ", grpc_core::StrError(errno)));
                  }

                  return absl::OkStatus();
                })

// set a socket to reuse old addresses
IF_POSIX_SOCKET(
    absl::Status FileDescriptors::SetSocketReuseAddr(const FileDescriptor& fd,
                                                     int reuse),
    {
      int val = (reuse != 0);
      int newval;
      socklen_t intlen = sizeof(newval);
      if (0 !=
          setsockopt(fd.fd(), SOL_SOCKET, SO_REUSEADDR, &val, sizeof(val))) {
        return absl::Status(absl::StatusCode::kInternal,
                            absl::StrCat("setsockopt(SO_REUSEADDR): ",
                                         grpc_core::StrError(errno)));
      }
      if (0 !=
          getsockopt(fd.fd(), SOL_SOCKET, SO_REUSEADDR, &newval, &intlen)) {
        return absl::Status(absl::StatusCode::kInternal,
                            absl::StrCat("getsockopt(SO_REUSEADDR): ",
                                         grpc_core::StrError(errno)));
      }
      if ((newval != 0) != val) {
        return absl::Status(absl::StatusCode::kInternal,
                            "Failed to set SO_REUSEADDR");
      }

      return absl::OkStatus();
    })

// Disable nagle algorithm
IF_POSIX_SOCKET(
    absl::Status FileDescriptors::SetSocketLowLatency(const FileDescriptor& fd,
                                                      int low_latency),
    {
      int val = (low_latency != 0);
      int newval;
      socklen_t intlen = sizeof(newval);
      int unwrapped = fd.fd();
      if (0 !=
          setsockopt(unwrapped, IPPROTO_TCP, TCP_NODELAY, &val, sizeof(val))) {
        return absl::Status(absl::StatusCode::kInternal,
                            absl::StrCat("setsockopt(TCP_NODELAY): ",
                                         grpc_core::StrError(errno)));
      }
      if (0 !=
          getsockopt(unwrapped, IPPROTO_TCP, TCP_NODELAY, &newval, &intlen)) {
        return absl::Status(absl::StatusCode::kInternal,
                            absl::StrCat("getsockopt(TCP_NODELAY): ",
                                         grpc_core::StrError(errno)));
      }
      if ((newval != 0) != val) {
        return absl::Status(absl::StatusCode::kInternal,
                            "Failed to set TCP_NODELAY");
      }
      return absl::OkStatus();
    })

int FileDescriptors::ConfigureSocket(const FileDescriptor& fd, int type) {
  // clang-format off
#define RETURN_IF_ERROR(expr) if (!(expr).ok()) { return -1; }
  // clang-format on
  RETURN_IF_ERROR(SetSocketNonBlocking(fd, 1));
  RETURN_IF_ERROR(SetSocketCloexec(fd, 1));
  if (type == SOCK_STREAM) {
    RETURN_IF_ERROR(SetSocketLowLatency(fd, 1));
  }
  return 0;
}

// Set Differentiated Services Code Point (DSCP)
IF_POSIX_SOCKET(
    absl::Status FileDescriptors::SetSocketDscp(const FileDescriptor& fd,
                                                int dscp),
    {
      if (dscp == PosixTcpOptions::kDscpNotSet) {
        return absl::OkStatus();
      }
      // The TOS/TrafficClass byte consists of following bits:
      // | 7 6 5 4 3 2 | 1 0 |
      // |    DSCP     | ECN |
      int newval = dscp << 2;
      int val;
      socklen_t intlen = sizeof(val);
      int fdesc = fd.fd();
      // Get ECN bits from current IP_TOS value unless IPv6 only
      if (0 == getsockopt(fdesc, IPPROTO_IP, IP_TOS, &val, &intlen)) {
        newval |= (val & 0x3);
        if (0 !=
            setsockopt(fdesc, IPPROTO_IP, IP_TOS, &newval, sizeof(newval))) {
          return absl::Status(
              absl::StatusCode::kInternal,
              absl::StrCat("setsockopt(IP_TOS): ", grpc_core::StrError(errno)));
        }
      }
      // Get ECN from current Traffic Class value if IPv6 is available
      if (0 == getsockopt(fdesc, IPPROTO_IPV6, IPV6_TCLASS, &val, &intlen)) {
        newval |= (val & 0x3);
        if (0 != setsockopt(fdesc, IPPROTO_IPV6, IPV6_TCLASS, &newval,
                            sizeof(newval))) {
          return absl::Status(absl::StatusCode::kInternal,
                              absl::StrCat("setsockopt(IPV6_TCLASS): ",
                                           grpc_core::StrError(errno)));
        }
      }
      return absl::OkStatus();
    })

// set a socket to reuse old ports
absl::Status FileDescriptors::SetSocketReusePort(const FileDescriptor& fd,
                                                 int reuse) {
#ifndef SO_REUSEPORT
  return absl::Status(absl::StatusCode::kInternal,
                      "SO_REUSEPORT unavailable on compiling system");
#else
  int val = (reuse != 0);
  int newval;
  socklen_t intlen = sizeof(newval);
  int fdesc = fd.fd();
  if (0 != setsockopt(fdesc, SOL_SOCKET, SO_REUSEPORT, &val, sizeof(val))) {
    return absl::Status(
        absl::StatusCode::kInternal,
        absl::StrCat("setsockopt(SO_REUSEPORT): ", grpc_core::StrError(errno)));
  }
  if (0 != getsockopt(fdesc, SOL_SOCKET, SO_REUSEPORT, &newval, &intlen)) {
    return absl::Status(
        absl::StatusCode::kInternal,
        absl::StrCat("getsockopt(SO_REUSEPORT): ", grpc_core::StrError(errno)));
  }
  if ((newval != 0) != val) {
    return absl::Status(absl::StatusCode::kInternal,
                        "Failed to set SO_REUSEPORT");
  }

  return absl::OkStatus();
#endif
}

absl::Status FileDescriptors::SetSocketIpPktInfoIfPossible(
    const FileDescriptor& fd) {
#ifdef GRPC_HAVE_IP_PKTINFO
  auto result = SetSockOpt(fd, IPPROTO_IP, IP_PKTINFO, 1);
  if (!result.ok()) {
    return absl::Status(
        absl::StatusCode::kInternal,
        absl::StrCat("setsockopt(IP_PKTINFO): ",
                     grpc_core::StrError(result.errno_value())));
  }
#endif
  return absl::OkStatus();
}

absl::Status FileDescriptors::SetSocketIpv6RecvPktInfoIfPossible(
    const FileDescriptor& fd) {
#ifdef GRPC_HAVE_IPV6_RECVPKTINFO
  auto result = SetSockOpt(fd, IPPROTO_IPV6, IPV6_RECVPKTINFO, 1);
  if (!result.ok()) {
    return absl::Status(absl::StatusCode::kInternal,
                        absl::StrCat("setsockopt(IPV6_RECVPKTINFO): ",
                                     grpc_core::StrError(result.ok())));
  }
#endif
  return absl::OkStatus();
}

IF_POSIX_SOCKET(
    absl::Status FileDescriptors::SetSocketSndBuf(const FileDescriptor& fd,
                                                  int buffer_size_bytes),
    {
      int f = fd.fd();
      return 0 == setsockopt(f, SOL_SOCKET, SO_SNDBUF, &buffer_size_bytes,
                             sizeof(buffer_size_bytes))
                 ? absl::OkStatus()
                 : absl::Status(absl::StatusCode::kInternal,
                                absl::StrCat("setsockopt(SO_SNDBUF): ",
                                             grpc_core::StrError(errno)));
    })

IF_POSIX_SOCKET(
    absl::Status FileDescriptors::SetSocketRcvBuf(const FileDescriptor& fd,
                                                  int buffer_size_bytes),
    {
      int f = fd.fd();
      return 0 == setsockopt(f, SOL_SOCKET, SO_RCVBUF, &buffer_size_bytes,
                             sizeof(buffer_size_bytes))
                 ? absl::OkStatus()
                 : absl::Status(absl::StatusCode::kInternal,
                                absl::StrCat("setsockopt(SO_RCVBUF): ",
                                             grpc_core::StrError(errno)));
    })

// Set TCP_USER_TIMEOUT
IF_POSIX_SOCKET(
    void FileDescriptors::TrySetSocketTcpUserTimeout(
        const FileDescriptor& fd, const PosixTcpOptions& options,
        bool is_client),
    {
      if (g_socket_supports_tcp_user_timeout.load() < 0) {
        return;
      }
      bool enable = is_client ? kDefaultClientUserTimeoutEnabled
                              : kDefaultServerUserTimeoutEnabled;
      int timeout =
          is_client ? kDefaultClientUserTimeoutMs : kDefaultServerUserTimeoutMs;
      if (options.keep_alive_time_ms > 0) {
        enable = options.keep_alive_time_ms != INT_MAX;
      }
      if (options.keep_alive_timeout_ms > 0) {
        timeout = options.keep_alive_timeout_ms;
      }
      if (enable) {
        int newval;
        socklen_t len = sizeof(newval);
        int f = fd.fd();
        // If this is the first time to use TCP_USER_TIMEOUT, try to check
        // if it is available.
        if (g_socket_supports_tcp_user_timeout.load() == 0) {
          if (0 !=
              getsockopt(f, IPPROTO_TCP, TCP_USER_TIMEOUT, &newval, &len)) {
            // This log is intentionally not protected behind a flag, so that
            // users know that TCP_USER_TIMEOUT is not being used.
            GRPC_TRACE_LOG(tcp, INFO)
                << "TCP_USER_TIMEOUT is not available. TCP_USER_TIMEOUT "
                   "won't be used thereafter";
            g_socket_supports_tcp_user_timeout.store(-1);
          } else {
            GRPC_TRACE_LOG(tcp, INFO)
                << "TCP_USER_TIMEOUT is available. TCP_USER_TIMEOUT will be "
                   "used thereafter";
            g_socket_supports_tcp_user_timeout.store(1);
          }
        }
        if (g_socket_supports_tcp_user_timeout.load() > 0) {
          if (0 != setsockopt(f, IPPROTO_TCP, TCP_USER_TIMEOUT, &timeout,
                              sizeof(timeout))) {
            LOG(ERROR) << "setsockopt(TCP_USER_TIMEOUT) "
                       << grpc_core::StrError(errno);
            return;
          }
          if (0 !=
              getsockopt(f, IPPROTO_TCP, TCP_USER_TIMEOUT, &newval, &len)) {
            LOG(ERROR) << "getsockopt(TCP_USER_TIMEOUT) "
                       << grpc_core::StrError(errno);
            return;
          }
          if (newval != timeout) {
            // Do not fail on failing to set TCP_USER_TIMEOUT
            LOG(ERROR) << "Failed to set TCP_USER_TIMEOUT";
            return;
          }
        }
      }
    })

}  // namespace grpc_event_engine::experimental