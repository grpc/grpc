// Copyright 2024 The gRPC Authors
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

#include "src/core/lib/event_engine/posix_engine/posix_system_api.h"

#include <grpc/event_engine/event_engine.h>
#include <grpc/support/port_platform.h>

#include <array>

#include "absl/log/log.h"
#include "absl/strings/str_cat.h"
#include "src/core/lib/debug/trace.h"
#include "src/core/lib/event_engine/tcp_socket_utils.h"
#include "src/core/lib/iomgr/port.h"
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

#ifdef GRPC_LINUX_EPOLL
#include <sys/epoll.h>
#endif  // GRPC_LINUX_EPOLL
#ifdef GRPC_LINUX_EVENTFD
#include <sys/eventfd.h>
#endif  // GRPC_LINUX_EVENTFD

#ifdef GRPC_POSIX_SOCKET

namespace grpc_event_engine {
namespace experimental {

#ifdef GRPC_POSIX_SOCKETUTILS

FileDescriptor SystemApi::Accept4(
    FileDescriptor sockfd,
    grpc_event_engine::experimental::EventEngine::ResolvedAddress& addr,
    int nonblock, int cloexec) const {
  int flags;
  EventEngine::ResolvedAddress peer_addr;
  socklen_t len = EventEngine::ResolvedAddress::MAX_SIZE_BYTES;
  FileDescriptor fd =
      Accept(sockfd, const_cast<sockaddr*>(peer_addr.address()), &len);
  if (fd.ready()) {
    if (nonblock) {
      flags = Fcntl(fd, F_GETFL, 0);
      if (flags < 0) goto close_and_error;
      if (Fcntl(fd, F_SETFL, flags | O_NONBLOCK) != 0) {
        goto close_and_error;
      }
    }
    if (cloexec) {
      flags = Fcntl(fd, F_GETFD, 0);
      if (flags < 0) goto close_and_error;
      if (Fcntl(fd, F_SETFD, flags | FD_CLOEXEC) != 0) {
        goto close_and_error;
      }
    }
  }
  addr = EventEngine::ResolvedAddress(peer_addr.address(), len);
  return fd;

close_and_error:
  Close(fd);
  return FileDescriptor();
}

#elif GRPC_LINUX_SOCKETUTILS

FileDescriptor SystemApi::Accept4(FileDescriptor sockfd, struct sockaddr* addr,
                                  socklen_t* addrlen, int flags) const {
  return FileDescriptor(accept4(sockfd.fd(), addr, addrlen, flags));
}

FileDescriptor SystemApi::Accept4(
    FileDescriptor sockfd,
    grpc_event_engine::experimental::EventEngine::ResolvedAddress& addr,
    int nonblock, int cloexec) const {
  int flags = 0;
  flags |= nonblock ? SOCK_NONBLOCK : 0;
  flags |= cloexec ? SOCK_CLOEXEC : 0;
  EventEngine::ResolvedAddress peer_addr;
  socklen_t len = EventEngine::ResolvedAddress::MAX_SIZE_BYTES;
  FileDescriptor ret =
      Accept4(sockfd, const_cast<sockaddr*>(peer_addr.address()), &len, flags);
  addr = EventEngine::ResolvedAddress(peer_addr.address(), len);
  return ret;
}

#endif  // GRPC_LINUX_SOCKETUTILS

FileDescriptor SystemApi::Accept(FileDescriptor sockfd, struct sockaddr* addr,
                                 socklen_t* addrlen) const {
  return FileDescriptor(accept(sockfd.fd(), addr, addrlen));
}

FileDescriptor SystemApi::AdoptExternalFd(int fd) const {
  return FileDescriptor(fd);
}

FileDescriptor SystemApi::Socket(int domain, int type, int protocol) const {
  return FileDescriptor(socket(domain, type, protocol));
}

int SystemApi::Bind(FileDescriptor fd, const struct sockaddr* addr,
                    socklen_t addrlen) const {
  return bind(fd.fd(), addr, addrlen);
}

void SystemApi::Close(FileDescriptor fd) const { close(fd.fd()); }

int SystemApi::Fcntl(FileDescriptor fd, int op, int args) const {
  return fcntl(fd.fd(), op, args);
}

int SystemApi::GetSockOpt(FileDescriptor fd, int level, int optname,
                          void* optval, socklen_t* optlen) const {
  return getsockopt(fd.fd(), level, optname, optval, optlen);
}

int SystemApi::GetSockName(FileDescriptor fd, struct sockaddr* addr,
                           socklen_t* addrlen) const {
  return getsockname(fd.fd(), addr, addrlen);
}

int SystemApi::GetPeerName(FileDescriptor fd, struct sockaddr* addr,
                           socklen_t* addrlen) const {
  return getpeername(fd.fd(), addr, addrlen);
}

int SystemApi::Listen(FileDescriptor fd, int backlog) const {
  return listen(fd.fd(), backlog);
}

long SystemApi::RecvMsg(FileDescriptor fd, struct msghdr* msg,
                        int flags) const {
  return recvmsg(fd.fd(), msg, flags);
}

long SystemApi::SendMsg(FileDescriptor fd, const struct msghdr* message,
                        int flags) const {
  return sendmsg(fd.fd(), message, flags);
}

int SystemApi::SetSockOpt(FileDescriptor fd, int level, int optname,
                          const void* optval, socklen_t optlen) const {
  return setsockopt(fd.fd(), level, optname, optval, optlen);
}

absl::Status SystemApi::SetSocketNoSigpipeIfPossible(FileDescriptor fd) const {
#ifdef GRPC_HAVE_SO_NOSIGPIPE
  int val = 1;
  int newval;
  socklen_t intlen = sizeof(newval);
  if (0 != SetSockOpt(fd, SOL_SOCKET, SO_NOSIGPIPE, &val, sizeof(val))) {
    return absl::Status(
        absl::StatusCode::kInternal,
        absl::StrCat("setsockopt(SO_NOSIGPIPE): ", grpc_core::StrError(errno)));
  }
  if (0 != GetSockOpt(fd, SOL_SOCKET, SO_NOSIGPIPE, &newval, &intlen)) {
    return absl::Status(
        absl::StatusCode::kInternal,
        absl::StrCat("getsockopt(SO_NOSIGPIPE): ", grpc_core::StrError(errno)));
  }
  if ((newval != 0) != (val != 0)) {
    return absl::Status(absl::StatusCode::kInternal,
                        "Failed to set SO_NOSIGPIPE");
  }
#else
  (void)fd;  // Makes the compiler error go away
#endif
  return absl::OkStatus();
}

bool SystemApi::IsSocketReusePortSupported() const {
  // This is depends on the OS so it is ok to make it static
  static bool kSupportSoReusePort = [this]() -> bool {
    FileDescriptor s = Socket(AF_INET, SOCK_STREAM, 0);
    if (!s.ready()) {
      // This might be an ipv6-only environment in which case
      // 'socket(AF_INET,..)' call would fail. Try creating IPv6 socket in
      // that case
      s = Socket(AF_INET6, SOCK_STREAM, 0);
    }
    if (s.ready()) {
      bool result = SetSocketReusePort(s, 1).ok();
      Close(s);
      return result;
    } else {
      return false;
    }
  }();
  return kSupportSoReusePort;
}

// set a socket to reuse old ports
absl::Status SystemApi::SetSocketReusePort(FileDescriptor fd, int reuse) const {
#ifndef SO_REUSEPORT
  return absl::Status(absl::StatusCode::kInternal,
                      "SO_REUSEPORT unavailable on compiling system");
#else
  int val = (reuse != 0);
  int newval;
  socklen_t intlen = sizeof(newval);
  if (0 != SetSockOpt(fd, SOL_SOCKET, SO_REUSEPORT, &val, sizeof(val))) {
    return absl::Status(
        absl::StatusCode::kInternal,
        absl::StrCat("setsockopt(SO_REUSEPORT): ", grpc_core::StrError(errno)));
  }
  if (0 != GetSockOpt(fd, SOL_SOCKET, SO_REUSEPORT, &newval, &intlen)) {
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

// Set Differentiated Services Code Point (DSCP)
absl::Status SystemApi::SetSocketDscp(FileDescriptor fd, int dscp) const {
  if (dscp == kDscpNotSet) {
    return absl::OkStatus();
  }
  // The TOS/TrafficClass byte consists of following bits:
  // | 7 6 5 4 3 2 | 1 0 |
  // |    DSCP     | ECN |
  int newval = dscp << 2;
  int val;
  socklen_t intlen = sizeof(val);
  // Get ECN bits from current IP_TOS value unless IPv6 only
  if (0 == GetSockOpt(fd, IPPROTO_IP, IP_TOS, &val, &intlen)) {
    newval |= (val & 0x3);
    if (0 != SetSockOpt(fd, IPPROTO_IP, IP_TOS, &newval, sizeof(newval))) {
      return absl::Status(
          absl::StatusCode::kInternal,
          absl::StrCat("setsockopt(IP_TOS): ", grpc_core::StrError(errno)));
    }
  }
  // Get ECN from current Traffic Class value if IPv6 is available
  if (0 == GetSockOpt(fd, IPPROTO_IPV6, IPV6_TCLASS, &val, &intlen)) {
    newval |= (val & 0x3);
    if (0 !=
        SetSockOpt(fd, IPPROTO_IPV6, IPV6_TCLASS, &newval, sizeof(newval))) {
      return absl::Status(absl::StatusCode::kInternal,
                          absl::StrCat("setsockopt(IPV6_TCLASS): ",
                                       grpc_core::StrError(errno)));
    }
  }
  return absl::OkStatus();
}

// Set a socket to use zerocopy
absl::Status SystemApi::SetSocketZeroCopy(FileDescriptor fd) const {
#ifdef GRPC_LINUX_ERRQUEUE
  const int enable = 1;
  auto err = SetSockOpt(fd, SOL_SOCKET, SO_ZEROCOPY, &enable, sizeof(enable));
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
absl::Status SystemApi::SetSocketNonBlocking(FileDescriptor fd,
                                             int non_blocking) const {
  int oldflags = Fcntl(fd, F_GETFL, 0);
  if (oldflags < 0) {
    return absl::Status(absl::StatusCode::kInternal,
                        absl::StrCat("fcntl: ", grpc_core::StrError(errno)));
  }
  if (non_blocking) {
    oldflags |= O_NONBLOCK;
  } else {
    oldflags &= ~O_NONBLOCK;
  }
  if (Fcntl(fd, F_SETFL, oldflags) != 0) {
    return absl::Status(absl::StatusCode::kInternal,
                        absl::StrCat("fcntl: ", grpc_core::StrError(errno)));
  }
  return absl::OkStatus();
}

// Set a socket to close on exec
absl::Status SystemApi::SetSocketCloexec(FileDescriptor fd,
                                         int close_on_exec) const {
  int oldflags = Fcntl(fd, F_GETFD, 0);
  if (oldflags < 0) {
    return absl::Status(absl::StatusCode::kInternal,
                        absl::StrCat("fcntl: ", grpc_core::StrError(errno)));
  }

  if (close_on_exec) {
    oldflags |= FD_CLOEXEC;
  } else {
    oldflags &= ~FD_CLOEXEC;
  }

  if (Fcntl(fd, F_SETFD, oldflags) != 0) {
    return absl::Status(absl::StatusCode::kInternal,
                        absl::StrCat("fcntl: ", grpc_core::StrError(errno)));
  }

  return absl::OkStatus();
}

// Disable nagle algorithm
absl::Status SystemApi::SetSocketLowLatency(FileDescriptor fd,
                                            int low_latency) const {
  int val = (low_latency != 0);
  int newval;
  socklen_t intlen = sizeof(newval);
  if (0 != SetSockOpt(fd, IPPROTO_TCP, TCP_NODELAY, &val, sizeof(val))) {
    return absl::Status(
        absl::StatusCode::kInternal,
        absl::StrCat("setsockopt(TCP_NODELAY): ", grpc_core::StrError(errno)));
  }
  if (0 != GetSockOpt(fd, IPPROTO_TCP, TCP_NODELAY, &newval, &intlen)) {
    return absl::Status(
        absl::StatusCode::kInternal,
        absl::StrCat("getsockopt(TCP_NODELAY): ", grpc_core::StrError(errno)));
  }
  if ((newval != 0) != val) {
    return absl::Status(absl::StatusCode::kInternal,
                        "Failed to set TCP_NODELAY");
  }
  return absl::OkStatus();
}

// set a socket to reuse old addresses
absl::Status SystemApi::SetSocketReuseAddr(FileDescriptor fd, int reuse) const {
  int val = (reuse != 0);
  int newval;
  socklen_t intlen = sizeof(newval);
  if (0 != SetSockOpt(fd, SOL_SOCKET, SO_REUSEADDR, &val, sizeof(val))) {
    return absl::Status(
        absl::StatusCode::kInternal,
        absl::StrCat("setsockopt(SO_REUSEADDR): ", grpc_core::StrError(errno)));
  }
  if (0 != GetSockOpt(fd, SOL_SOCKET, SO_REUSEADDR, &newval, &intlen)) {
    return absl::Status(
        absl::StatusCode::kInternal,
        absl::StrCat("getsockopt(SO_REUSEADDR): ", grpc_core::StrError(errno)));
  }
  if ((newval != 0) != val) {
    return absl::Status(absl::StatusCode::kInternal,
                        "Failed to set SO_REUSEADDR");
  }

  return absl::OkStatus();
}

absl::Status SystemApi::SetSocketIpPktInfoIfPossible(FileDescriptor fd) const {
#ifdef GRPC_HAVE_IP_PKTINFO
  int get_local_ip = 1;
  if (0 != SetSockOpt(fd, IPPROTO_IP, IP_PKTINFO, &get_local_ip,
                      sizeof(get_local_ip))) {
    return absl::Status(
        absl::StatusCode::kInternal,
        absl::StrCat("setsockopt(IP_PKTINFO): ", grpc_core::StrError(errno)));
  }
#endif
  return absl::OkStatus();
}

absl::Status SystemApi::SetSocketIpv6RecvPktInfoIfPossible(
    FileDescriptor fd) const {
#ifdef GRPC_HAVE_IPV6_RECVPKTINFO
  int get_local_ip = 1;
  if (0 != SetSockOpt(fd, IPPROTO_IPV6, IPV6_RECVPKTINFO, &get_local_ip,
                      sizeof(get_local_ip))) {
    return absl::Status(absl::StatusCode::kInternal,
                        absl::StrCat("setsockopt(IPV6_RECVPKTINFO): ",
                                     grpc_core::StrError(errno)));
  }
#endif
  return absl::OkStatus();
}

absl::Status SystemApi::SetSocketSndBuf(FileDescriptor fd,
                                        int buffer_size_bytes) const {
  return 0 == SetSockOpt(fd, SOL_SOCKET, SO_SNDBUF, &buffer_size_bytes,
                         sizeof(buffer_size_bytes))
             ? absl::OkStatus()
             : absl::Status(absl::StatusCode::kInternal,
                            absl::StrCat("setsockopt(SO_SNDBUF): ",
                                         grpc_core::StrError(errno)));
}

absl::Status SystemApi::SetSocketRcvBuf(FileDescriptor fd,
                                        int buffer_size_bytes) const {
  return 0 == SetSockOpt(fd, SOL_SOCKET, SO_RCVBUF, &buffer_size_bytes,
                         sizeof(buffer_size_bytes))
             ? absl::OkStatus()
             : absl::Status(absl::StatusCode::kInternal,
                            absl::StrCat("setsockopt(SO_RCVBUF): ",
                                         grpc_core::StrError(errno)));
}

// Set TCP_USER_TIMEOUT
void SystemApi::TrySetSocketTcpUserTimeout(FileDescriptor fd,
                                           int keep_alive_time_ms,
                                           int keep_alive_timeout_ms,
                                           bool is_client) const {
  if (g_socket_supports_tcp_user_timeout.load() < 0) {
    return;
  }
  bool enable = is_client ? kDefaultClientUserTimeoutEnabled
                          : kDefaultServerUserTimeoutEnabled;
  int timeout =
      is_client ? kDefaultClientUserTimeoutMs : kDefaultServerUserTimeoutMs;
  if (keep_alive_time_ms > 0) {
    enable = keep_alive_time_ms != INT_MAX;
  }
  if (keep_alive_timeout_ms > 0) {
    timeout = keep_alive_timeout_ms;
  }
  if (enable) {
    int newval;
    socklen_t len = sizeof(newval);
    // If this is the first time to use TCP_USER_TIMEOUT, try to check
    // if it is available.
    if (g_socket_supports_tcp_user_timeout.load() == 0) {
      if (0 != GetSockOpt(fd, IPPROTO_TCP, TCP_USER_TIMEOUT, &newval, &len)) {
        // This log is intentionally not protected behind a flag, so that users
        // know that TCP_USER_TIMEOUT is not being used.
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
      if (0 != SetSockOpt(fd, IPPROTO_TCP, TCP_USER_TIMEOUT, &timeout,
                          sizeof(timeout))) {
        LOG(ERROR) << "setsockopt(TCP_USER_TIMEOUT) "
                   << grpc_core::StrError(errno);
        return;
      }
      if (0 != GetSockOpt(fd, IPPROTO_TCP, TCP_USER_TIMEOUT, &newval, &len)) {
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
}

void SystemApi::ConfigureDefaultTcpUserTimeout(bool enable, int timeout,
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

absl::StatusOr<EventEngine::ResolvedAddress> SystemApi::LocalAddress(
    FileDescriptor fd) const {
  EventEngine::ResolvedAddress addr;
  socklen_t len = EventEngine::ResolvedAddress::MAX_SIZE_BYTES;
  if (GetSockName(fd, const_cast<sockaddr*>(addr.address()), &len) < 0) {
    return absl::InternalError(
        absl::StrCat("getsockname:", grpc_core::StrError(errno)));
  }
  return EventEngine::ResolvedAddress(addr.address(), len);
}

absl::StatusOr<EventEngine::ResolvedAddress> SystemApi::PeerAddress(
    FileDescriptor fd) const {
  EventEngine::ResolvedAddress addr;
  socklen_t len = EventEngine::ResolvedAddress::MAX_SIZE_BYTES;
  if (GetPeerName(fd, const_cast<sockaddr*>(addr.address()), &len) < 0) {
    return absl::InternalError(
        absl::StrCat("getpeername:", grpc_core::StrError(errno)));
  }
  return EventEngine::ResolvedAddress(addr.address(), len);
}

absl::StatusOr<std::string> SystemApi::LocalAddressString(
    FileDescriptor fd) const {
  auto status = LocalAddress(fd);
  if (!status.ok()) {
    return status.status();
  }
  return ResolvedAddressToNormalizedString((*status));
}

absl::StatusOr<std::string> SystemApi::PeerAddressString(
    FileDescriptor fd) const {
  auto status = PeerAddress(fd);
  if (!status.ok()) {
    return status.status();
  }
  return ResolvedAddressToNormalizedString((*status));
}

int SystemApi::Shutdown(FileDescriptor sockfd, int how) const {
  return shutdown(sockfd.fd(), how);
}

int SystemApi::Connect(FileDescriptor sockfd, const struct sockaddr* addr,
                       socklen_t addrlen) const {
  return connect(sockfd.fd(), addr, addrlen);
}

int SystemApi::Ioctl(FileDescriptor fd, int request, void* extras) const {
  return ioctl(fd.fd(), request, extras);
}

long SystemApi::Read(FileDescriptor fd, void* buf, size_t count) const {
  return read(fd.fd(), buf, count);
}

long SystemApi::Write(FileDescriptor fd, const void* buf, size_t count) const {
  return write(fd.fd(), buf, count);
}

std::pair<int, std::array<FileDescriptor, 2>> SystemApi::SocketPair(
    int domain, int type, int protocol) const {
  std::array<int, 2> fds;
  int result = socketpair(domain, type, protocol, fds.data());
  return {result, {AdoptExternalFd(fds[0]), AdoptExternalFd(fds[1])}};
}

absl::Status SystemApi::SetSocketNonBlocking(FileDescriptor fd) const {
  int oldflags = Fcntl(fd, F_GETFL, 0);
  if (oldflags < 0) {
    return absl::Status(absl::StatusCode::kInternal,
                        absl::StrCat("fcntl: ", grpc_core::StrError(errno)));
  }

  oldflags |= O_NONBLOCK;

  if (Fcntl(fd, F_SETFL, oldflags) != 0) {
    return absl::Status(absl::StatusCode::kInternal,
                        absl::StrCat("fcntl: ", grpc_core::StrError(errno)));
  }

  return absl::OkStatus();
}

#ifdef GRPC_LINUX_EPOLL
FileDescriptor SystemApi::EpollCreateAndCloexec() const {
#ifdef GRPC_LINUX_EPOLL_CREATE1
  int fd = epoll_create1(EPOLL_CLOEXEC);
  if (fd < 0) {
    LOG(ERROR) << "epoll_create1 unavailable";
  }
#else
  int fd = epoll_create(MAX_EPOLL_EVENTS);
  if (fd < 0) {
    LOG(ERROR) << "epoll_create unavailable";
  } else if (fcntl(fd, F_SETFD, FD_CLOEXEC) != 0) {
    LOG(ERROR) << "fcntl following epoll_create failed";
    return -1;
  }
#endif
  return AdoptExternalFd(fd);
}
#endif  // GRPC_LINUX_EPOLL

std::pair<int, std::array<FileDescriptor, 2>> SystemApi::Pipe() const {
  std::array<int, 2> fds;
  int status = pipe(fds.data());
  return {status, {AdoptExternalFd(fds[0]), AdoptExternalFd(fds[1])}};
}

#ifdef GRPC_LINUX_EPOLL
long SystemApi::EventFdRead(FileDescriptor fd, uint64_t* value) const {
  return eventfd_read(fd.fd(), value);
}

int SystemApi::EpollCtl(FileDescriptor epfd, int op, FileDescriptor fd,
                        struct epoll_event* event) const {
  return epoll_ctl(epfd.fd(), op, fd.fd(), event);
}

int SystemApi::EpollWait(FileDescriptor epfd, struct epoll_event* events,
                         int maxevents, int timeout) const {
  return epoll_wait(epfd.fd(), events, maxevents, timeout);
}

FileDescriptor SystemApi::EventFd(unsigned int initval, int flags) const {
  return AdoptExternalFd(eventfd(initval, flags));
}

long SystemApi::EventFdWrite(FileDescriptor fd, uint64_t value) const {
  return eventfd_write(fd.fd(), value);
}

#endif  // GRPC_LINUX_EVENTFD

}  // namespace experimental
}  // namespace grpc_event_engine

#else  //  GRPC_POSIX_SOCKET

#include "src/core/util/crash.h"

namespace grpc_event_engine {
namespace experimental {

FileDescriptor SystemApi::AdoptExternalFd(int fd) const {
  grpc_core::Crash("unimplemented");
}

FileDescriptor SystemApi::Socket(int domain, int type, int protocol) const {
  grpc_core::Crash("unimplemented");
}

int SystemApi::Bind(FileDescriptor fd, const struct sockaddr* addr,
                    socklen_t addrlen) const {
  grpc_core::Crash("unimplemented");
}

void SystemApi::Close(FileDescriptor fd) const {
  grpc_core::Crash("unimplemented");
}

int SystemApi::Fcntl(FileDescriptor fd, int op, int args) const {
  grpc_core::Crash("unimplemented");
}

int SystemApi::GetSockOpt(FileDescriptor fd, int level, int optname,
                          void* optval, socklen_t* optlen) const {
  grpc_core::Crash("unimplemented");
}

int SystemApi::GetSockName(FileDescriptor fd, struct sockaddr* addr,
                           socklen_t* addrlen) const {
  grpc_core::Crash("unimplemented");
}

int SystemApi::GetPeerName(FileDescriptor fd, struct sockaddr* addr,
                           socklen_t* addrlen) const {
  grpc_core::Crash("unimplemented");
}

int SystemApi::Listen(FileDescriptor fd, int backlog) const {
  grpc_core::Crash("unimplemented");
}

long SystemApi::RecvMsg(FileDescriptor fd, struct msghdr* msg,
                        int flags) const {
  grpc_core::Crash("unimplemented");
}

long SystemApi::SendMsg(FileDescriptor fd, const struct msghdr* message,
                        int flags) const {
  grpc_core::Crash("unimplemented");
}

int SystemApi::SetSockOpt(FileDescriptor fd, int level, int optname,
                          const void* optval, socklen_t optlen) const {
  grpc_core::Crash("unimplemented");
}

absl::Status SystemApi::SetSocketNoSigpipeIfPossible(FileDescriptor fd) const {
  grpc_core::Crash("unimplemented");
}

absl::Status SystemApi::SetSocketZeroCopy(FileDescriptor fd) const {
  grpc_core::Crash("unimplemented");
}

absl::Status SystemApi::SetSocketNonBlocking(FileDescriptor fd,
                                             int non_blocking) const {
  grpc_core::Crash("unimplemented");
}

absl::Status SystemApi::SetSocketCloexec(FileDescriptor fd,
                                         int close_on_exec) const {
  grpc_core::Crash("unimplemented");
}

absl::Status SystemApi::SetSocketLowLatency(FileDescriptor fd,
                                            int low_latency) const {
  grpc_core::Crash("unimplemented");
}

absl::Status SystemApi::SetSocketDscp(FileDescriptor fd, int dscp) const {
  grpc_core::Crash("unimplemented");
}

absl::Status SystemApi::SetSocketIpPktInfoIfPossible(FileDescriptor fd) const {
  grpc_core::Crash("unimplemented");
}

absl::Status SystemApi::SetSocketIpv6RecvPktInfoIfPossible(
    FileDescriptor fd) const {
  grpc_core::Crash("unimplemented");
}

absl::Status SystemApi::SetSocketSndBuf(FileDescriptor fd,
                                        int buffer_size_bytes) const {
  grpc_core::Crash("unimplemented");
}

absl::Status SystemApi::SetSocketRcvBuf(FileDescriptor fd,
                                        int buffer_size_bytes) const {
  grpc_core::Crash("unimplemented");
}

void SystemApi::TrySetSocketTcpUserTimeout(FileDescriptor fd,
                                           int keep_alive_time_ms,
                                           int keep_alive_timeout_ms,
                                           bool is_client) const {
  grpc_core::Crash("unimplemented");
}

void SystemApi::ConfigureDefaultTcpUserTimeout(bool enable, int timeout,
                                               bool is_client) {
  grpc_core::Crash("unimplemented");
}

bool SystemApi::IsSocketReusePortSupported() const {
  grpc_core::Crash("unimplemented");
}

int SystemApi::Shutdown(FileDescriptor sockfd, int how) const {
  grpc_core::Crash("unimplemented");
}

int SystemApi::Connect(FileDescriptor sockfd, const struct sockaddr* addr,
                       socklen_t addrlen) const {
  grpc_core::Crash("unimplemented");
}

int SystemApi::Ioctl(FileDescriptor fd, int request, void* extras) const {
  grpc_core::Crash("unimplemented");
}

long SystemApi::Read(FileDescriptor fd, void* buf, size_t count) const {
  grpc_core::Crash("unimplemented");
}

long SystemApi::Write(FileDescriptor fd, const void* buf, size_t count) const {
  grpc_core::Crash("unimplemented");
}

std::tuple<int, FileDescriptor, FileDescriptor> SystemApi::SocketPair(
    int domain, int type, int protocol) {
  grpc_core::Crash("unimplemented");
}

}  // namespace experimental
}  // namespace grpc_event_engine

#endif  //  GRPC_POSIX_SOCKET
