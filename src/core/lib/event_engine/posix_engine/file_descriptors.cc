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
#include "src/core/lib/event_engine/posix_engine/tcp_socket_utils.h"
#include "src/core/lib/event_engine/tcp_socket_utils.h"
#include "src/core/lib/iomgr/port.h"
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
#include "src/core/util/crash.h"
#define IF_EPOLL(signature, body) \
  signature { grpc_core::Crash("unimplemented"); }
#endif  // GRPC_LINUX_EPOLL
#else   // GRPC_POSIX_SOCKET
#include "src/core/util/crash.h"
#define IF_POSIX_SOCKET(signature, body) \
  signature { grpc_core::Crash("unimplemented"); }
#define IF_EPOLL(signature, body) \
  signature { grpc_core::Crash("unimplemented"); }
#endif  // GRPC_POSIX_SOCKET

namespace grpc_event_engine::experimental {

namespace {

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
    absl::StatusOr<PosixSocketWrapper> FileDescriptors::CreateDualStackSocket(
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
          return PosixSocketWrapper(newfd);
        }
        // If this isn't an IPv4 address, then return whatever we've got.
        if (!ResolvedAddressIsV4Mapped(addr, nullptr)) {
          if (newfd < 0) {
            return ErrorForFd(newfd, addr);
          }
          dsmode = PosixSocketWrapper::DSMode::DSMODE_IPV6;
          return PosixSocketWrapper(newfd);
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
      return PosixSocketWrapper(newfd);
    })

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

IF_POSIX_SOCKET(Int64Result FileDescriptors::RecvMsg(const FileDescriptor& fd,
                                                     struct msghdr* message,
                                                     int flags),
                { return Int64Wrap(recvmsg(fd.fd(), message, flags)); })

IF_POSIX_SOCKET(Int64Result FileDescriptors::SendMsg(
                    const FileDescriptor& fd, const struct msghdr* message,
                    int flags),
                { return Int64Wrap(sendmsg(fd.fd(), message, flags)); })

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

IF_POSIX_SOCKET(absl::Status FileDescriptors::PrepareTcpClientSocket(
                    PosixSocketWrapper sock, const FileDescriptor& fd,
                    const EventEngine::ResolvedAddress& addr,
                    const PosixTcpOptions& options),
                {
                  bool close_fd = true;
                  auto sock_cleanup =
                      absl::MakeCleanup([&close_fd, &sock]() -> void {
                        if (close_fd and sock.Fd() >= 0) {
                          close(sock.Fd());
                        }
                      });
                  GRPC_RETURN_IF_ERROR(sock.SetSocketNonBlocking(1));
                  GRPC_RETURN_IF_ERROR(sock.SetSocketCloexec(1));
                  if (options.tcp_receive_buffer_size !=
                      options.kReadBufferSizeUnset) {
                    GRPC_RETURN_IF_ERROR(
                        sock.SetSocketRcvBuf(options.tcp_receive_buffer_size));
                  }
                  if (addr.address()->sa_family != AF_UNIX &&
                      !ResolvedAddressIsVSock(addr)) {
                    // If its not a unix socket or vsock address.
                    GRPC_RETURN_IF_ERROR(sock.SetSocketLowLatency(1));
                    GRPC_RETURN_IF_ERROR(sock.SetSocketReuseAddr(1));
                    GRPC_RETURN_IF_ERROR(sock.SetSocketDscp(options.dscp));
                    sock.TrySetSocketTcpUserTimeout(options, true);
                  }
                  GRPC_RETURN_IF_ERROR(SetSocketNoSigpipeIfPossible(fd));
                  GRPC_RETURN_IF_ERROR(ApplySocketMutatorInOptions(
                      fd, GRPC_FD_CLIENT_CONNECTION_USAGE, options));
                  // No errors. Set close_fd to false to ensure the socket is
                  // not closed.
                  close_fd = false;
                  return absl::OkStatus();
                })

absl::StatusOr<PosixSocketWrapper::PosixSocketCreateResult>
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
  absl::StatusOr<PosixSocketWrapper> posix_socket_wrapper =
      CreateDualStackSocket(nullptr, mapped_target_addr, SOCK_STREAM, 0,
                            dsmode);
  if (!posix_socket_wrapper.ok()) {
    return posix_socket_wrapper.status();
  }

  if (dsmode == PosixSocketWrapper::DSMode::DSMODE_IPV4) {
    // Original addr is either v4 or v4 mapped to v6. Set mapped_addr to v4.
    if (!ResolvedAddressIsV4Mapped(target_addr, &mapped_target_addr)) {
      mapped_target_addr = target_addr;
    }
  }

  auto error = PrepareTcpClientSocket(*posix_socket_wrapper,
                                      Adopt(posix_socket_wrapper->Fd()),
                                      mapped_target_addr, options);
  if (!error.ok()) {
    return error;
  }
  return PosixSocketWrapper::PosixSocketCreateResult{*posix_socket_wrapper,
                                                     mapped_target_addr};
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

}  // namespace grpc_event_engine::experimental