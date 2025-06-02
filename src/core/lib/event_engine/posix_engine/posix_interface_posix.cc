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

#include <grpc/event_engine/event_engine.h>
#include <grpc/support/port_platform.h>

#include "src/core/lib/event_engine/posix_engine/posix_interface.h"
#include "src/core/lib/iomgr/port.h"

#ifdef GRPC_POSIX_WAKEUP_FD
#include <fcntl.h>

#include "src/core/util/strerror.h"
#endif  // GRPC_POSIX_WAKEUP_FD

#ifdef GRPC_POSIX_SOCKET

#include <sys/types.h>

#include <cerrno>
#include <cstdint>
#include <utility>

#include "absl/cleanup/cleanup.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_replace.h"
#include "absl/strings/string_view.h"
#include "src/core/lib/event_engine/posix_engine/file_descriptor_collection.h"
#include "src/core/lib/event_engine/posix_engine/tcp_socket_utils.h"
#include "src/core/lib/event_engine/tcp_socket_utils.h"
#include "src/core/util/crash.h"  // IWYU pragma: keep
#include "src/core/util/status_helper.h"

#ifdef GRPC_POSIX_SOCKET_UTILS_COMMON
#include <arpa/inet.h>  // IWYU pragma: keep
#ifdef GRPC_LINUX_TCP_H
#include <linux/tcp.h>
#else
#include <netinet/in.h>  // IWYU pragma: keep
#include <netinet/tcp.h>
#endif  // GRPC_LINUX_TCP_H
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>
#endif  //  GRPC_POSIX_SOCKET_UTILS_COMMON

#ifdef GRPC_LINUX_EVENTFD
#include <sys/eventfd.h>
#endif

// File needs to be compilable on all platforms. These macros will produce stubs
// if specific feature is not available in specific environment
#ifdef GRPC_LINUX_EPOLL
#include <sys/epoll.h>
#endif  // GRPC_LINUX_EPOLL

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

#define MIN_SAFE_ACCEPT_QUEUE_SIZE 100

#endif  // GRPC_POSIX_SOCKET

namespace grpc_event_engine::experimental {

#if defined(GRPC_POSIX_WAKEUP_FD) || defined(GRPC_POSIX_SOCKET)
namespace {

// Templated Fn to make it easier to compile on all platforms.
template <typename Fn, typename... Args>
PosixErrorOr<int64_t> Int64Wrap(bool correct_gen, int fd, const Fn& fn,
                                Args&&... args) {
  if (!correct_gen) return PosixError::WrongGeneration();
  auto result = std::invoke(fn, fd, std::forward<Args>(args)...);
  if (result < 0) return PosixError::Error(errno);
  return result;
}

// Set a socket to non blocking mode
absl::Status SetSocketNonBlocking(int fd, int non_blocking) {
  int oldflags = fcntl(fd, F_GETFL, 0);
  if (oldflags < 0) {
    return absl::Status(absl::StatusCode::kInternal,
                        absl::StrCat("fcntl: ", grpc_core::StrError(errno)));
  }

  if (non_blocking) {
    oldflags |= O_NONBLOCK;
  } else {
    oldflags &= ~O_NONBLOCK;
  }

  if (fcntl(fd, F_SETFL, oldflags) != 0) {
    return absl::Status(absl::StatusCode::kInternal,
                        absl::StrCat("fcntl: ", grpc_core::StrError(errno)));
  }
  return absl::OkStatus();
}

}  // namespace
#endif  // defined(GRPC_POSIX_WAKEUP_FD) || defined(GRPC_POSIX_SOCKET)

#ifdef GRPC_POSIX_SOCKET

namespace {

// This way if constexpr can be used and also macro nesting is not needed
#ifdef GRPC_LINUX_ERRQUEUE
constexpr bool kLinuxErrqueue = true;
#else   // GRPC_LINUX_ERRQUEUE
constexpr bool kLinuxErrqueue = false;
#endif  // GRPC_LINUX_ERRQUEUE

// The default values for TCP_USER_TIMEOUT are currently configured to be in
// line with the default values of KEEPALIVE_TIMEOUT as proposed in
// https://github.com/grpc/proposal/blob/master/A18-tcp-user-timeout.md
int kDefaultClientUserTimeoutMs = 20000;
int kDefaultServerUserTimeoutMs = 20000;
bool kDefaultClientUserTimeoutEnabled = false;
bool kDefaultServerUserTimeoutEnabled = true;

// Whether the socket supports TCP_USER_TIMEOUT option.
// (0: don't know, 1: support, -1: not support)
std::atomic<int> g_socket_supports_tcp_user_timeout(
    SOCKET_SUPPORTS_TCP_USER_TIMEOUT_DEFAULT);

absl::Status ErrorForFd(
    int fd, const experimental::EventEngine::ResolvedAddress& addr) {
  if (fd >= 0) return absl::OkStatus();
  const char* addr_str = reinterpret_cast<const char*>(addr.address());
  return absl::Status(absl::StatusCode::kInternal,
                      absl::StrCat("socket: ", grpc_core::StrError(errno),
                                   std::string(addr_str, addr.size())));
}

int CreateSocket(std::function<int(int, int, int)> socket_factory, int family,
                 int type, int protocol) {
  int res = socket_factory != nullptr ? socket_factory(family, type, protocol)
                                      : socket(family, type, protocol);
  if (res < 0 && errno == EMFILE) {
    int saved_errno = errno;
    LOG_EVERY_N_SEC(ERROR, 10)
        << "socket(" << family << ", " << type << ", " << protocol
        << ") returned " << res << " with error: |"
        << grpc_core::StrError(errno)
        << "|. This process might not have a sufficient file descriptor limit "
           "for the number of connections grpc wants to open (which is "
           "generally a function of the number of grpc channels, the lb policy "
           "of each channel, and the number of backends each channel is load "
           "balancing across).";
    errno = saved_errno;
  }
  return res;
}

// Tries to set the socket's receive buffer to given size.
absl::Status SetSocketRcvBuf(int fd, int buffer_size_bytes) {
  return 0 == setsockopt(fd, SOL_SOCKET, SO_RCVBUF, &buffer_size_bytes,
                         sizeof(buffer_size_bytes))
             ? absl::OkStatus()
             : absl::Status(absl::StatusCode::kInternal,
                            absl::StrCat("setsockopt(SO_RCVBUF): ",
                                         grpc_core::StrError(errno)));
}

// get max listen queue size on linux
int InitMaxAcceptQueueSize() {
  int n = SOMAXCONN;
  char buf[64];
  FILE* fp = fopen("/proc/sys/net/core/somaxconn", "r");
  int max_accept_queue_size;
  if (fp == nullptr) {
    // 2.4 kernel.
    return SOMAXCONN;
  }
  if (fgets(buf, sizeof buf, fp)) {
    char* end;
    long i = strtol(buf, &end, 10);
    if (i > 0 && i <= INT_MAX && end && *end == '\n') {
      n = static_cast<int>(i);
    }
  }
  fclose(fp);
  max_accept_queue_size = n;

  if (max_accept_queue_size < MIN_SAFE_ACCEPT_QUEUE_SIZE) {
    LOG(INFO) << "Suspiciously small accept queue (" << max_accept_queue_size
              << ") will probably lead to connection drops";
  }
  return max_accept_queue_size;
}

int GetMaxAcceptQueueSize() {
  static const int kMaxAcceptQueueSize = InitMaxAcceptQueueSize();
  return kMaxAcceptQueueSize;
}

// Set a socket to close on exec
absl::Status SetSocketCloexec(int fd, int close_on_exec) {
  int oldflags = fcntl(fd, F_GETFD, 0);
  if (oldflags < 0) {
    return absl::Status(absl::StatusCode::kInternal,
                        absl::StrCat("fcntl: ", grpc_core::StrError(errno)));
  }

  if (close_on_exec) {
    oldflags |= FD_CLOEXEC;
  } else {
    oldflags &= ~FD_CLOEXEC;
  }

  if (fcntl(fd, F_SETFD, oldflags) != 0) {
    return absl::Status(absl::StatusCode::kInternal,
                        absl::StrCat("fcntl: ", grpc_core::StrError(errno)));
  }

  return absl::OkStatus();
}

// set a socket to reuse old addresses
absl::Status SetSocketOption(int fd, int level, int option, int value,
                             absl::string_view debug_label) {
  int val = (value != 0);
  int newval;
  socklen_t intlen = sizeof(newval);
  if (0 != setsockopt(fd, level, option, &val, sizeof(val))) {
    return absl::Status(absl::StatusCode::kInternal,
                        absl::StrCat("setsockopt(", debug_label,
                                     "): ", grpc_core::StrError(errno)));
  }
  if (0 != getsockopt(fd, level, option, &newval, &intlen)) {
    return absl::Status(absl::StatusCode::kInternal,
                        absl::StrCat("setsockopt(", debug_label,
                                     "): ", grpc_core::StrError(errno)));
  }
  if ((newval != 0) != val) {
    return absl::Status(absl::StatusCode::kInternal,
                        absl::StrCat("Failed to set ", debug_label));
  }

  return absl::OkStatus();
}

// set a socket to reuse old ports
absl::Status SetSocketReusePort(int fd, GRPC_UNUSED int reuse) {
#ifndef SO_REUSEPORT
  return absl::Status(absl::StatusCode::kInternal,
                      "SO_REUSEPORT unavailable on compiling system");
#else
  return SetSocketOption(fd, SOL_SOCKET, SO_REUSEPORT, 1, "SO_REUSEPORT");
#endif
}

// Set Differentiated Services Code Point (DSCP)
absl::Status SetSocketDscp(int fdesc, int dscp) {
  if (dscp == PosixTcpOptions::kDscpNotSet) {
    return absl::OkStatus();
  }
  // The TOS/TrafficClass byte consists of following bits:
  // | 7 6 5 4 3 2 | 1 0 |
  // |    DSCP     | ECN |
  int newval = dscp << 2;
  int val;
  socklen_t intlen = sizeof(val);
  // Get ECN bits from current IP_TOS value unless IPv6 only
  if (0 == getsockopt(fdesc, IPPROTO_IP, IP_TOS, &val, &intlen)) {
    newval |= (val & 0x3);
    if (0 != setsockopt(fdesc, IPPROTO_IP, IP_TOS, &newval, sizeof(newval))) {
      return absl::Status(
          absl::StatusCode::kInternal,
          absl::StrCat("setsockopt(IP_TOS): ", grpc_core::StrError(errno)));
    }
  }
  // Get ECN from current Traffic Class value if IPv6 is available
  if (0 == getsockopt(fdesc, IPPROTO_IPV6, IPV6_TCLASS, &val, &intlen)) {
    newval |= (val & 0x3);
    if (0 !=
        setsockopt(fdesc, IPPROTO_IPV6, IPV6_TCLASS, &newval, sizeof(newval))) {
      return absl::Status(absl::StatusCode::kInternal,
                          absl::StrCat("setsockopt(IPV6_TCLASS): ",
                                       grpc_core::StrError(errno)));
    }
  }
  return absl::OkStatus();
}

// Set a socket to use zerocopy
absl::Status SetSocketZeroCopy(int fd) {
#ifdef GRPC_LINUX_ERRQUEUE
  return SetSocketOption(fd, SOL_SOCKET, SO_ZEROCOPY, 1, "SO_ZEROCOPY");
#else   // GRPC_LINUX_ERRQUEUE
  return absl::Status(absl::StatusCode::kInternal,
                      absl::StrCat("setsockopt(SO_ZEROCOPY): ",
                                   grpc_core::StrError(ENOSYS).c_str()));
#endif  // GRPC_LINUX_ERRQUEUE
}

// Set TCP_USER_TIMEOUT
void TrySetSocketTcpUserTimeout(int fd, const PosixTcpOptions& options,
                                bool is_client) {
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
    // If this is the first time to use TCP_USER_TIMEOUT, try to check
    // if it is available.
    if (g_socket_supports_tcp_user_timeout.load() == 0) {
      if (0 != getsockopt(fd, IPPROTO_TCP, TCP_USER_TIMEOUT, &newval, &len)) {
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
      if (0 != setsockopt(fd, IPPROTO_TCP, TCP_USER_TIMEOUT, &timeout,
                          sizeof(timeout))) {
        LOG(ERROR) << "setsockopt(TCP_USER_TIMEOUT) "
                   << grpc_core::StrError(errno);
        return;
      }
      if (0 != getsockopt(fd, IPPROTO_TCP, TCP_USER_TIMEOUT, &newval, &len)) {
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

absl::StatusOr<int> InternalCreateDualStackSocket(
    std::function<int(int, int, int)> socket_factory,
    const experimental::EventEngine::ResolvedAddress& addr, int type,
    int protocol, EventEnginePosixInterface::DSMode& dsmode) {
  const sockaddr* sock_addr = addr.address();
  int family = sock_addr->sa_family;
  int newfd;
  if (family == AF_INET6) {
    if (IsIpv6LoopbackAvailable()) {
      newfd = CreateSocket(socket_factory, family, type, protocol);
    } else {
      newfd = -1;
      errno = EAFNOSUPPORT;
    }
    // Check if we've got a valid dualstack socket.
    if (newfd > 0 && SetSocketDualStack(newfd)) {
      dsmode = EventEnginePosixInterface::DSMode::DSMODE_DUALSTACK;
      return newfd;
    }
    // If this isn't an IPv4 address, then return whatever we've
    // got.
    if (!ResolvedAddressIsV4Mapped(addr, nullptr)) {
      if (newfd < 0) {
        return ErrorForFd(newfd, addr);
      }
      dsmode = EventEnginePosixInterface::DSMode::DSMODE_IPV6;
      return newfd;
    }
    // Fall back to AF_INET.
    if (newfd >= 0) {
      close(newfd);
    }
    family = AF_INET;
  }
  dsmode = family == AF_INET ? EventEnginePosixInterface::DSMode::DSMODE_IPV4
                             : EventEnginePosixInterface::DSMode::DSMODE_NONE;
  newfd = CreateSocket(socket_factory, family, type, protocol);
  if (newfd < 0) {
    return ErrorForFd(newfd, addr);
  }
  return newfd;
}

absl::Status InternalSetSocketNoSigpipeIfPossible(GRPC_UNUSED int fd) {
#ifdef GRPC_HAVE_SO_NOSIGPIPE
  return SetSocketOption(fd, SOL_SOCKET, SO_NOSIGPIPE, 1, "SO_NOSIGPIPE");
#else   // GRPC_HAVE_SO_NOSIGPIPE
  return absl::OkStatus();
#endif  // GRPC_HAVE_SO_NOSIGPIPE
}

absl::Status InternalApplySocketMutatorInOptions(
    int fd, grpc_fd_usage usage, const PosixTcpOptions& options) {
  if (options.socket_mutator == nullptr) {
    return absl::OkStatus();
  }
  if (!grpc_socket_mutator_mutate_fd(options.socket_mutator, fd, usage)) {
    return absl::Status(absl::StatusCode::kInternal,
                        "grpc_socket_mutator failed.");
  }
  return absl::OkStatus();
}

}  // namespace

bool IsSocketReusePortSupported() {
  static bool kSupportSoReusePort = []() -> bool {
    EventEnginePosixInterface posix_interface;
    auto s = posix_interface.Socket(AF_INET, SOCK_STREAM, 0);
    if (!s.ok()) {
      // This might be an ipv6-only environment in which case
      // 'socket(AF_INET,..)' call would fail. Try creating IPv6 socket in
      // that case
      s = posix_interface.Socket(AF_INET6, SOCK_STREAM, 0);
    }
    if (s.ok()) {
      bool result =
          SetSocketReusePort(posix_interface.GetFd(s.value()).value(), 1).ok();
      posix_interface.Close(s.value());
      return result;
    } else {
      return false;
    }
  }();
  return kSupportSoReusePort;
}

#ifdef GRPC_ENABLE_FORK_SUPPORT

void EventEnginePosixInterface::AdvanceGeneration() {
  if (!IsEventEngineForkEnabled()) {
    grpc_core::Crash(
        "Fork support is disabled but AdvanceGeneration was called");
  }
  for (int fd : descriptors_.ClearAndReturnRawDescriptors()) {
    if (fd > 0) {
      close(fd);
    }
  }
  descriptors_ = FileDescriptorCollection(descriptors_.generation() + 1);
}

#endif  // GRPC_ENABLE_FORK_SUPPORT

FileDescriptor EventEnginePosixInterface::Adopt(int fd) {
  return descriptors_.Add(fd);
}

PosixErrorOr<int> EventEnginePosixInterface::GetFd(const FileDescriptor& fd) {
  if (!IsCorrectGeneration(fd)) {
    return PosixError::WrongGeneration();
  }
  return fd.fd();
}

//
// ---- Socket/FD Creation Factories ----
//

absl::StatusOr<EventEnginePosixInterface::PosixSocketCreateResult>
EventEnginePosixInterface::CreateAndPrepareTcpClientSocket(
    const PosixTcpOptions& options,
    const EventEngine::ResolvedAddress& target_addr) {
  EventEnginePosixInterface::DSMode dsmode;
  EventEngine::ResolvedAddress mapped_target_addr;

  // Use dualstack sockets where available. Set mapped to v6 or v4 mapped to
  // v6.
  if (!ResolvedAddressToV4Mapped(target_addr, &mapped_target_addr)) {
    // addr is v4 mapped to v6 or just v6.
    mapped_target_addr = target_addr;
  }
  absl::StatusOr<FileDescriptor> socket_fd = CreateDualStackSocket(
      nullptr, mapped_target_addr, SOCK_STREAM, 0, dsmode);
  if (!socket_fd.ok()) {
    return socket_fd.status();
  }

  if (dsmode == DSMode::DSMODE_IPV4) {
    // Original addr is either v4 or v4 mapped to v6. Set mapped_addr to v4.
    if (!ResolvedAddressIsV4Mapped(target_addr, &mapped_target_addr)) {
      mapped_target_addr = target_addr;
    }
  }
  auto error =
      PrepareTcpClientSocket(socket_fd->fd(), mapped_target_addr, options);
  if (!error.ok()) {
    return error;
  }
  return PosixSocketCreateResult{*socket_fd, mapped_target_addr};
}

absl::StatusOr<FileDescriptor> EventEnginePosixInterface::CreateDualStackSocket(
    std::function<int(int, int, int)> socket_factory,
    const experimental::EventEngine::ResolvedAddress& addr, int type,
    int protocol, DSMode& dsmode) {
  auto fd = InternalCreateDualStackSocket(std::move(socket_factory), addr, type,
                                          protocol, dsmode);
  if (!fd.ok()) {
    return std::move(fd).status();
  }
  return descriptors_.Add(*fd);
}

#ifdef GRPC_LINUX_EPOLL
#ifdef GRPC_LINUX_EPOLL_CREATE1

PosixErrorOr<FileDescriptor>
EventEnginePosixInterface::EpollCreateAndCloexec() {
  auto fd = RegisterPosixResult(epoll_create1(EPOLL_CLOEXEC));
  if (!fd.ok()) {
    LOG(ERROR) << "epoll_create1 unavailable";
  }
  return fd;
}

#else  // GRPC_LINUX_EPOLL_CREATE1

PosixErrorOr<FileDescriptor>
EventEnginePosixInterface::EpollCreateAndCloexec() {
  auto fd = RegisterPosixResult(epoll_create(MAX_EPOLL_EVENTS));
  if (!fd.ok()) {
    LOG(ERROR) << "epoll_create unavailable";
    return fd;
  } else if (fcntl(fd->value(), F_SETFD, FD_CLOEXEC) != 0) {
    LOG(ERROR) << "fcntl following epoll_create failed";
    return PosixError::Error(errno);
  }
  return fd;
}

#endif  // GRPC_LINUX_EPOLL_CREATE1
#else   // GRPC_LINUX_EPOLL

PosixErrorOr<FileDescriptor>
EventEnginePosixInterface::EpollCreateAndCloexec() {
  grpc_core::Crash("Not supported");
}

#endif  // GRPC_LINUX_EPOLL

PosixErrorOr<FileDescriptor> EventEnginePosixInterface::Socket(int domain,
                                                               int type,
                                                               int protocol) {
  return RegisterPosixResult(socket(domain, type, protocol));
}

PosixErrorOr<FileDescriptor> EventEnginePosixInterface::Accept(
    const FileDescriptor& sockfd, struct sockaddr* addr, socklen_t* addrlen) {
  if (!IsCorrectGeneration(sockfd)) {
    return PosixError::WrongGeneration();
  }
  return RegisterPosixResult(accept(sockfd.fd(), addr, addrlen));
}

#ifdef GRPC_POSIX_SOCKETUTILS

PosixErrorOr<FileDescriptor> EventEnginePosixInterface::Accept4(
    const FileDescriptor& sockfd,
    grpc_event_engine::experimental::EventEngine::ResolvedAddress& addr,
    int nonblock, int cloexec) {
  int flags;
  EventEngine::ResolvedAddress peer_addr;
  socklen_t len = EventEngine::ResolvedAddress::MAX_SIZE_BYTES;
  auto fd = Accept(sockfd, const_cast<sockaddr*>(peer_addr.address()), &len);
  if (!fd.ok()) {
    return fd;
  }
  int raw_fd = fd->fd();
  if (nonblock) {
    flags = fcntl(raw_fd, F_GETFL, 0);
    if (flags < 0) goto close_and_error;
    if (fcntl(raw_fd, F_SETFL, flags | O_NONBLOCK) != 0) goto close_and_error;
  }
  if (cloexec) {
    flags = fcntl(raw_fd, F_GETFD, 0);
    if (flags < 0) goto close_and_error;
    if (fcntl(raw_fd, F_SETFD, flags | FD_CLOEXEC) != 0) goto close_and_error;
  }
  addr = EventEngine::ResolvedAddress(peer_addr.address(), len);
  return fd;

close_and_error:
  Close(*fd);
  return PosixError::Error(errno);
}

#else  // GRPC_POSIX_SOCKETUTILS

PosixErrorOr<FileDescriptor> EventEnginePosixInterface::Accept4(
    const FileDescriptor& sockfd,
    grpc_event_engine::experimental::EventEngine::ResolvedAddress& addr,
    int nonblock, int cloexec) {
  if (!IsCorrectGeneration(sockfd)) {
    return PosixError::WrongGeneration();
  }
  int flags = 0;
  flags |= nonblock ? SOCK_NONBLOCK : 0;
  flags |= cloexec ? SOCK_CLOEXEC : 0;
  EventEngine::ResolvedAddress peer_addr;
  socklen_t len = EventEngine::ResolvedAddress::MAX_SIZE_BYTES;
  PosixErrorOr<FileDescriptor> ret = RegisterPosixResult(accept4(
      sockfd.fd(), const_cast<sockaddr*>(peer_addr.address()), &len, flags));
  if (ret.ok()) {
    addr = EventEngine::ResolvedAddress(peer_addr.address(), len);
  }
  return ret;
}

#endif  // GRPC_POSIX_SOCKETUTILS

PosixError EventEnginePosixInterface::Connect(const FileDescriptor& sockfd,
                                              const struct sockaddr* addr,
                                              socklen_t addrlen) {
  return PosixResultWrap(
      sockfd, [&](int sockfd) { return connect(sockfd, addr, addrlen); });
}

PosixErrorOr<int64_t> EventEnginePosixInterface::RecvMsg(
    const FileDescriptor& fd, struct msghdr* message, int flags) {
  return Int64Wrap(IsCorrectGeneration(fd), fd.fd(), recvmsg, message, flags);
}

PosixErrorOr<int64_t> EventEnginePosixInterface::SendMsg(
    const FileDescriptor& fd, const struct msghdr* message, int flags) {
  return Int64Wrap(IsCorrectGeneration(fd), fd.fd(), sendmsg, message, flags);
}

PosixError EventEnginePosixInterface::Shutdown(const FileDescriptor& fd,
                                               int how) {
  return PosixResultWrap(fd, [&](int fd) { return shutdown(fd, how); });
}

absl::Status EventEnginePosixInterface::ApplySocketMutatorInOptions(
    const FileDescriptor& fd, grpc_fd_usage usage,
    const PosixTcpOptions& options) {
  if (!IsCorrectGeneration(fd)) {
    return absl::InternalError("ApplySocketMutatorInOptions: wrong generation");
  }
  return InternalApplySocketMutatorInOptions(fd.fd(), usage, options);
}

void EventEnginePosixInterface::ConfigureDefaultTcpUserTimeout(bool enable,
                                                               int timeout,
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

int EventEnginePosixInterface::ConfigureSocket(const FileDescriptor& fd,
                                               int type) {
  if (!IsCorrectGeneration(fd)) {
    return -1;
  }
#define RETURN_IF_ERROR(expr) \
  if (!(expr).ok()) {         \
    return -1;                \
  }
  RETURN_IF_ERROR(SetSocketNonBlocking(fd.fd(), 1));
  RETURN_IF_ERROR(SetSocketCloexec(fd.fd(), 1));
  if (type == SOCK_STREAM) {
    RETURN_IF_ERROR(
        SetSocketOption(fd.fd(), IPPROTO_TCP, TCP_NODELAY, 1, "TCP_NODELAY"));
  }
  return 0;
}

PosixError EventEnginePosixInterface::GetSockOpt(const FileDescriptor& fd,
                                                 int level, int optname,
                                                 void* optval, void* optlen) {
  return PosixResultWrap(fd, [&](int fd) {
    return getsockopt(fd, level, optname, optval,
                      static_cast<socklen_t*>(optlen));
  });
}

// Bind to "::" to get a port number not used by any address.
absl::StatusOr<int> EventEnginePosixInterface::GetUnusedPort() {
  EventEngine::ResolvedAddress wild = ResolvedAddressMakeWild6(0);
  DSMode dsmode;
  auto fd =
      InternalCreateDualStackSocket(nullptr, wild, SOCK_STREAM, 0, dsmode);
  GRPC_RETURN_IF_ERROR(fd.status());
  if (dsmode == DSMode::DSMODE_IPV4) {
    wild = ResolvedAddressMakeWild4(0);
  }
  if (bind(*fd, wild.address(), wild.size()) != 0) {
    close(*fd);
    return absl::FailedPreconditionError(
        absl::StrCat("bind(GetUnusedPort): ", std::strerror(errno)));
  }
  socklen_t len = wild.size();
  if (getsockname(*fd, const_cast<sockaddr*>(wild.address()), &len) != 0) {
    close(*fd);
    return absl::FailedPreconditionError(
        absl::StrCat("getsockname(GetUnusedPort): ", std::strerror(errno)));
  }
  close(*fd);
  int port = ResolvedAddressGetPort(wild);
  if (port <= 0) {
    return absl::FailedPreconditionError("Bad port");
  }
  return port;
}

PosixError EventEnginePosixInterface::Ioctl(const FileDescriptor& fd, int op,
                                            void* arg) {
  return PosixResultWrap(fd, [&](int fd) { return ioctl(fd, op, arg); });
}

absl::StatusOr<EventEngine::ResolvedAddress>
EventEnginePosixInterface::LocalAddress(const FileDescriptor& fd) {
  if (!IsCorrectGeneration(fd)) {
    return absl::InternalError(
        "getsockname: file descriptor from wrong generation");
  }
  EventEngine::ResolvedAddress addr;
  socklen_t len = EventEngine::ResolvedAddress::MAX_SIZE_BYTES;
  if (getsockname(fd.fd(), const_cast<sockaddr*>(addr.address()), &len) < 0) {
    return absl::InternalError(
        absl::StrCat("getsockname:", grpc_core::StrError(errno)));
  }
  return EventEngine::ResolvedAddress(addr.address(), len);
}

absl::StatusOr<std::string> EventEnginePosixInterface::LocalAddressString(
    const FileDescriptor& fd) {
  auto status = LocalAddress(fd);
  if (!status.ok()) {
    return status.status();
  }
  return ResolvedAddressToNormalizedString((*status));
}

absl::StatusOr<EventEngine::ResolvedAddress>
EventEnginePosixInterface::PeerAddress(const FileDescriptor& fd) {
  if (!IsCorrectGeneration(fd)) {
    return absl::InternalError("getpeername: wrong file descriptor generation");
  }
  EventEngine::ResolvedAddress addr;
  socklen_t len = EventEngine::ResolvedAddress::MAX_SIZE_BYTES;
  if (getpeername(fd.fd(), const_cast<sockaddr*>(addr.address()), &len) < 0) {
    return absl::InternalError(
        absl::StrCat("getpeername:", grpc_core::StrError(errno)));
  }
  return EventEngine::ResolvedAddress(addr.address(), len);
}

absl::StatusOr<std::string> EventEnginePosixInterface::PeerAddressString(
    const FileDescriptor& fd) {
  auto status = PeerAddress(fd);
  if (!status.ok()) {
    return status.status();
  }
  return ResolvedAddressToNormalizedString((*status));
}

absl::StatusOr<EventEngine::ResolvedAddress>
EventEnginePosixInterface::PrepareListenerSocket(
    const FileDescriptor& fd, const PosixTcpOptions& options,
    const EventEngine::ResolvedAddress& address) {
  if (!IsCorrectGeneration(fd)) {
    return absl::InternalError("PrepareListenerSocket: wrong generation");
  }
  int f = fd.fd();
  if (IsSocketReusePortSupported() && options.allow_reuse_port &&
      address.address()->sa_family != AF_UNIX &&
      !ResolvedAddressIsVSock(address)) {
    GRPC_RETURN_IF_ERROR(SetSocketReusePort(f, 1));
  }

  GRPC_RETURN_IF_ERROR(SetSocketNonBlocking(f, 1));
  GRPC_RETURN_IF_ERROR(SetSocketCloexec(f, 1));

  if (address.address()->sa_family != AF_UNIX &&
      !ResolvedAddressIsVSock(address)) {
    GRPC_RETURN_IF_ERROR(
        SetSocketOption(f, IPPROTO_TCP, TCP_NODELAY, 1, "TCP_NODELAY"));
    GRPC_RETURN_IF_ERROR(
        SetSocketOption(f, SOL_SOCKET, SO_REUSEADDR, 1, "SO_REUSEADDR"));
    GRPC_RETURN_IF_ERROR(SetSocketDscp(f, options.dscp));
    TrySetSocketTcpUserTimeout(f, options, false);
  }
  GRPC_RETURN_IF_ERROR(InternalSetSocketNoSigpipeIfPossible(f));
  GRPC_RETURN_IF_ERROR(InternalApplySocketMutatorInOptions(
      f, GRPC_FD_SERVER_LISTENER_USAGE, options));
  if (kLinuxErrqueue && !SetSocketZeroCopy(f).ok()) {
    // it's not fatal, so just log it.
    VLOG(2) << "Node does not support SO_ZEROCOPY, continuing.";
  }
  if (bind(f, address.address(), address.size()) < 0) {
    auto sockaddr_str = ResolvedAddressToString(address);
    if (!sockaddr_str.ok()) {
      LOG(ERROR) << "Could not convert sockaddr to string: "
                 << sockaddr_str.status();
      sockaddr_str = "<unparsable>";
    }
    sockaddr_str = absl::StrReplaceAll(*sockaddr_str, {{"\0", "@"}});
    return absl::FailedPreconditionError(
        absl::StrCat("Error in bind for address '", *sockaddr_str,
                     "': ", std::strerror(errno)));
  }
  if (listen(f, GetMaxAcceptQueueSize()) < 0) {
    return absl::FailedPreconditionError(
        absl::StrCat("Error in listen: ", std::strerror(errno)));
  }
  socklen_t len = static_cast<socklen_t>(sizeof(struct sockaddr_storage));
  EventEngine::ResolvedAddress sockname_temp;
  if (getsockname(f, const_cast<sockaddr*>(sockname_temp.address()), &len) <
      0) {
    return absl::FailedPreconditionError(
        absl::StrCat("Error in getsockname: ", std::strerror(errno)));
  }
  return sockname_temp;
}

// Set a socket using a grpc_socket_mutator
absl::Status EventEnginePosixInterface::SetSocketMutator(
    const FileDescriptor& fd, grpc_fd_usage usage,
    grpc_socket_mutator* mutator) {
  CHECK(mutator);
  if (!IsCorrectGeneration(fd)) {
    return absl::InternalError("SetSocketMutator: FD has a wrong generation");
  }
  if (!grpc_socket_mutator_mutate_fd(mutator, fd.fd(), usage)) {
    return absl::Status(absl::StatusCode::kInternal,
                        "grpc_socket_mutator failed.");
  }
  return absl::OkStatus();
}

absl::Status EventEnginePosixInterface::SetSocketNoSigpipeIfPossible(
    const FileDescriptor& fd) {
  if (!IsCorrectGeneration(fd)) {
    return absl::OkStatus();
  }
  return InternalSetSocketNoSigpipeIfPossible(fd.fd());
}

PosixErrorOr<int64_t> EventEnginePosixInterface::SetSockOpt(
    const FileDescriptor& fd, int level, int optname, uint32_t optval) {
  if (!IsCorrectGeneration(fd)) {
    return PosixError::WrongGeneration();
  }
  if (setsockopt(fd.fd(), level, optname, &optval, sizeof(optval)) < 0) {
    return PosixError::Error(errno);
  }
  return optval;
}

#ifdef GRPC_LINUX_EVENTFD

PosixErrorOr<FileDescriptor> EventEnginePosixInterface::EventFd(int initval,
                                                                int flags) {
  return RegisterPosixResult(eventfd(initval, flags));
}

PosixError EventEnginePosixInterface::EventFdRead(const FileDescriptor& fd) {
  return PosixResultWrap(fd, [](int fd) {
    eventfd_t value;
    return eventfd_read(fd, &value);
  });
}

PosixError EventEnginePosixInterface::EventFdWrite(const FileDescriptor& fd) {
  return PosixResultWrap(fd, [](int fd) { return eventfd_write(fd, 1); });
}

#else  // GRPC_LINUX_EVENTFD

PosixErrorOr<FileDescriptor> EventEnginePosixInterface::EventFd(int initval,
                                                                int flags) {
  grpc_core::Crash("EventFD not supported");
}

PosixError EventEnginePosixInterface::EventFdRead(const FileDescriptor& fd) {
  grpc_core::Crash("Not implemented");
}

PosixError EventEnginePosixInterface::EventFdWrite(const FileDescriptor& fd) {
  grpc_core::Crash("Not implemented");
}

#endif  // GRPC_LINUX_EVENTFD

//
// Epoll
//
#ifdef GRPC_LINUX_EPOLL

PosixError EventEnginePosixInterface::EpollCtlDel(const FileDescriptor& epfd,
                                                  const FileDescriptor& fd) {
  if (!IsCorrectGeneration(epfd) || !IsCorrectGeneration(fd)) {
    return PosixError::WrongGeneration();
  }
  epoll_event phony_event;
  int result = epoll_ctl(epfd.fd(), EPOLL_CTL_DEL, fd.fd(), &phony_event);
  if (result < 0) {
    return PosixError::Error(errno);
  }
  return PosixError::Ok();
}

PosixError EventEnginePosixInterface::EpollCtlAdd(const FileDescriptor& epfd,
                                                  bool writable,
                                                  const FileDescriptor& fd,
                                                  void* data) {
  epoll_event event;
  event.events = static_cast<uint32_t>(EPOLLIN | EPOLLET);
  if (writable) {
    event.events |= EPOLLOUT;
  }
  event.data.ptr = data;
  if (!IsCorrectGeneration(epfd) || !IsCorrectGeneration(fd)) {
    return PosixError::WrongGeneration();
  }
  int result = epoll_ctl(epfd.fd(), EPOLL_CTL_ADD, fd.fd(), &event);
  if (result < 0) {
    return PosixError::Error(errno);
  }
  return PosixError::Ok();
}

#endif  // GRPC_LINUX_EPOLL

absl::Status EventEnginePosixInterface::PrepareTcpClientSocket(
    int fd, const EventEngine::ResolvedAddress& addr,
    const PosixTcpOptions& options) {
  bool close_fd = true;
  auto sock_cleanup = absl::MakeCleanup([&close_fd, &fd]() -> void {
    if (close_fd && fd > 0) {
      close(fd);
    }
  });
  GRPC_RETURN_IF_ERROR(SetSocketNonBlocking(fd, 1));
  GRPC_RETURN_IF_ERROR(SetSocketCloexec(fd, 1));
  if (options.tcp_receive_buffer_size != options.kReadBufferSizeUnset) {
    GRPC_RETURN_IF_ERROR(SetSocketRcvBuf(fd, options.tcp_receive_buffer_size));
  }
  if (addr.address()->sa_family != AF_UNIX && !ResolvedAddressIsVSock(addr)) {
    // If its not a unix socket or vsock address.
    GRPC_RETURN_IF_ERROR(
        SetSocketOption(fd, IPPROTO_TCP, TCP_NODELAY, 1, "TCP_NODELAY"));
    GRPC_RETURN_IF_ERROR(
        SetSocketOption(fd, SOL_SOCKET, SO_REUSEADDR, 1, "SO_REUSEADDR"));
    GRPC_RETURN_IF_ERROR(SetSocketDscp(fd, options.dscp));
    TrySetSocketTcpUserTimeout(fd, options, true);
  }
  GRPC_RETURN_IF_ERROR(InternalSetSocketNoSigpipeIfPossible(fd));
  GRPC_RETURN_IF_ERROR(InternalApplySocketMutatorInOptions(
      fd, GRPC_FD_CLIENT_CONNECTION_USAGE, options));
  // No errors. Set close_fd to false to ensure the socket is
  // not closed.
  close_fd = false;
  return absl::OkStatus();
}

PosixError EventEnginePosixInterface::PosixResultWrap(
    const FileDescriptor& wrapped,
    const absl::AnyInvocable<int(int) const>& fn) const {
  if (!IsCorrectGeneration(wrapped)) {
    return PosixError::WrongGeneration();
  }
  int result = fn(wrapped.fd());
  if (result < 0) {
    return PosixError::Error(errno);
  }
  return PosixError::Ok();
}

PosixErrorOr<FileDescriptor> EventEnginePosixInterface::RegisterPosixResult(
    int result) {
  if (result < 0) {
    return PosixError::Error(errno);
  }
  return descriptors_.Add(result);
}

#endif  // GRPC_POSIX_SOCKET

#if defined(GRPC_POSIX_WAKEUP_FD) || defined(GRPC_LINUX_EVENTFD)

void EventEnginePosixInterface::Close(const FileDescriptor& fd) {
  if (descriptors_.Remove(fd)) {
    close(fd.fd());
  }
}

bool EventEnginePosixInterface::IsCorrectGeneration(
    const FileDescriptor& fd) const {
  (void)fd;  // Always used now
#ifdef GRPC_ENABLE_FORK_SUPPORT
  if (IsEventEngineForkEnabled()) {
    return descriptors_.generation() == fd.generation();
  }
#endif  // GRPC_ENABLE_FORK_SUPPORT
  return true;
}

absl::StatusOr<std::pair<FileDescriptor, FileDescriptor> >
EventEnginePosixInterface::Pipe() {
  int pipefd[2];
  int r = pipe(pipefd);
  if (0 != r) {
    return absl::Status(absl::StatusCode::kInternal,
                        absl::StrCat("pipe: ", grpc_core::StrError(errno)));
  }
  auto status = SetSocketNonBlocking(pipefd[0], 1);
  if (status.ok()) {
    status = SetSocketNonBlocking(pipefd[1], 1);
  }
  if (status.ok()) {
    return std::pair(descriptors_.Add(pipefd[0]), descriptors_.Add(pipefd[1]));
  }
  close(pipefd[0]);
  close(pipefd[1]);
  return status;
}

PosixErrorOr<int64_t> EventEnginePosixInterface::Read(const FileDescriptor& fd,
                                                      absl::Span<char> buf) {
  return Int64Wrap(IsCorrectGeneration(fd), fd.fd(), read, buf.data(),
                   buf.size());
}

PosixErrorOr<int64_t> EventEnginePosixInterface::Write(const FileDescriptor& fd,
                                                       absl::Span<char> buf) {
  return Int64Wrap(IsCorrectGeneration(fd), fd.fd(), write, buf.data(),
                   buf.size());
}

#endif  // defined (GRPC_POSIX_WAKEUP_FD) || defined (GRPC_LINUX_EVENTFD)

}  // namespace grpc_event_engine::experimental
