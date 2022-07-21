// Copyright 2022 The gRPC Authors
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

#include <grpc/support/port_platform.h>

#include <netinet/in.h>

#include "grpc/event_engine/event_engine.h"

#include "src/core/lib/iomgr/port.h"

#ifdef GRPC_POSIX_SOCKET_UTILS_COMMON
#ifdef GRPC_LINUX_TCP_H
#include <linux/tcp.h>
#else
#include <netinet/tcp.h>
#endif
#include <fcntl.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

#include <cstring>

#include "absl/status/status.h"

#include <grpc/impl/codegen/grpc_types.h>
#include <grpc/support/log.h>

#include "src/core/lib/event_engine/iomgr_engine/tcp_posix_socket_utils.h"

using ::grpc_event_engine::experimental::EndpointConfig;

namespace grpc_event_engine {
namespace iomgr_engine {

namespace {

int Clamp(int default_value, int min_value, int max_value, int actual_value) {
  if (actual_value < min_value || actual_value > max_value) {
    return default_value;
  }
  return actual_value;
}

int GetConfigValue(const EndpointConfig& config, absl::string_view key,
                   int min_value, int max_value, int default_value) {
  EndpointConfig::Setting value = config.Get(key);
  if (absl::holds_alternative<int>(value)) {
    return Clamp(default_value, min_value, max_value, absl::get<int>(value));
  }
  return default_value;
}

#ifdef GRPC_POSIX_SOCKET_UTILS_COMMON

absl::Status ErrorForFd(
    int fd, const experimental::EventEngine::ResolvedAddress& addr) {
  if (fd >= 0) return absl::OkStatus();
  const char* addr_str = reinterpret_cast<const char*>(addr.address());
  return absl::Status(absl::StatusCode::kInternal,
                      absl::StrCat("socket: ", strerror(errno),
                                   std::string(addr_str, addr.size())));
}

int CreateSocket(std::function<int(int, int, int)> socket_factory, int family,
                 int type, int protocol) {
  return socket_factory != nullptr ? socket_factory(family, type, protocol)
                                   : socket(family, type, protocol);
}

const uint8_t kV4MappedPrefix[] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0xff, 0xff};

bool SockaddrIsV4Mapped(const sockaddr* addr) {
  if (addr->sa_family == AF_INET6) {
    const sockaddr_in6* addr6 = reinterpret_cast<const sockaddr_in6*>(addr);
    if (memcmp(addr6->sin6_addr.s6_addr, kV4MappedPrefix,
               sizeof(kV4MappedPrefix)) == 0) {
      return true;
    }
  }
  return false;
}

#endif /* GRPC_POSIX_SOCKET_UTILS_COMMON */

}  // namespace

PosixTcpOptions TcpOptionsFromEndpointConfig(const EndpointConfig& config) {
  EndpointConfig::Setting value;
  PosixTcpOptions options;
  options.tcp_read_chunk_size = GetConfigValue(
      config, GRPC_ARG_TCP_READ_CHUNK_SIZE, 1, PosixTcpOptions::kMaxChunkSize,
      PosixTcpOptions::kDefaultReadChunkSize);
  options.tcp_min_read_chunk_size =
      GetConfigValue(config, GRPC_ARG_TCP_MIN_READ_CHUNK_SIZE, 1,
                     PosixTcpOptions::kMaxChunkSize,
                     PosixTcpOptions::kDefaultMinReadChunksize);
  options.tcp_max_read_chunk_size =
      GetConfigValue(config, GRPC_ARG_TCP_MAX_READ_CHUNK_SIZE, 1,
                     PosixTcpOptions::kMaxChunkSize,
                     PosixTcpOptions::kDefaultMaxReadChunksize);
  options.tcp_tx_zerocopy_send_bytes_threshold =
      GetConfigValue(config, GRPC_ARG_TCP_TX_ZEROCOPY_SEND_BYTES_THRESHOLD, 0,
                     INT_MAX, PosixTcpOptions::kDefaultSendBytesThreshold);
  options.tcp_tx_zerocopy_max_simultaneous_sends =
      GetConfigValue(config, GRPC_ARG_TCP_TX_ZEROCOPY_MAX_SIMULT_SENDS, 0,
                     INT_MAX, PosixTcpOptions::kDefaultMaxSends);
  options.tcp_tx_zero_copy_enabled =
      (GetConfigValue(config, GRPC_ARG_TCP_TX_ZEROCOPY_ENABLED, 0, 1,
                      PosixTcpOptions::kZerocpTxEnabledDefault) != 0);
  options.keep_alive_time_ms =
      GetConfigValue(config, GRPC_ARG_KEEPALIVE_TIME_MS, 1, INT_MAX, 0);
  options.keep_alive_timeout_ms =
      GetConfigValue(config, GRPC_ARG_KEEPALIVE_TIMEOUT_MS, 1, INT_MAX, 0);
  options.expand_wildcard_addrs =
      (GetConfigValue(config, GRPC_ARG_EXPAND_WILDCARD_ADDRS, 1, INT_MAX, 0) !=
       0);
  options.allow_reuse_port =
      (GetConfigValue(config, GRPC_ARG_ALLOW_REUSEPORT, 1, INT_MAX, 0) != 0);

  if (options.tcp_min_read_chunk_size > options.tcp_max_read_chunk_size) {
    options.tcp_min_read_chunk_size = options.tcp_max_read_chunk_size;
  }
  options.tcp_read_chunk_size = grpc_core::Clamp(
      options.tcp_read_chunk_size, options.tcp_min_read_chunk_size,
      options.tcp_max_read_chunk_size);

  value = config.Get(GRPC_ARG_RESOURCE_QUOTA);
  if (absl::holds_alternative<void*>(value)) {
    options.resource_quota =
        reinterpret_cast<grpc_core::ResourceQuota*>(absl::get<void*>(value))
            ->Ref();
  }
  value = config.Get(GRPC_ARG_SOCKET_MUTATOR);
  if (absl::holds_alternative<void*>(value)) {
    options.socket_mutator = grpc_socket_mutator_ref(
        static_cast<grpc_socket_mutator*>(absl::get<void*>(value)));
  }
  return options;
}

#ifdef GRPC_POSIX_SOCKETUTILS

int Accept4(int sockfd,
            grpc_event_engine::experimental::EventEngine::ResolvedAddress& addr,
            int nonblock, int cloexec) {
  int fd, flags;
  socklen_t len = addr.size();
  fd = accept(sockfd, const_cast<sockaddr*>(addr.address()), &len);
  if (fd >= 0) {
    if (nonblock) {
      flags = fcntl(fd, F_GETFL, 0);
      if (flags < 0) goto close_and_error;
      if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) != 0) goto close_and_error;
    }
    if (cloexec) {
      flags = fcntl(fd, F_GETFD, 0);
      if (flags < 0) goto close_and_error;
      if (fcntl(fd, F_SETFD, flags | FD_CLOEXEC) != 0) goto close_and_error;
    }
  }
  return fd;

close_and_error:
  close(fd);
  return -1;
}

#elif GRPC_LINUX_SOCKETUTILS

int Accept4(int sockfd,
            grpc_event_engine::experimental::EventEngine::ResolvedAddress& addr,
            int nonblock, int cloexec) {
  int flags = 0;
  flags |= nonblock ? SOCK_NONBLOCK : 0;
  flags |= cloexec ? SOCK_CLOEXEC : 0;
  socklen_t len = addr.size();
  return accept4(sockfd, const_cast<sockaddr*>(addr.address()), &len, flags);
}

#endif /* GRPC_LINUX_SOCKETUTILS */

#ifdef GRPC_POSIX_SOCKET_UTILS_COMMON
// Set a socket to use zerocopy
absl::Status PosixSocket::SetSocketZeroCopy() {
#ifdef GRPC_LINUX_ERRQUEUE
  const int enable = 1;
  auto err = setsockopt(fd_, SOL_SOCKET, SO_ZEROCOPY, &enable, sizeof(enable));
  if (err != 0) {
    return absl::Status(
        absl::StatusCode::kInternal,
        absl::StrCat("setsockopt(SO_ZEROCOPY): ", strerror(errno)));
  }
  return absl::OkStatus();
#else
  return absl::Status(
      absl::StatusCode::kInternal,
      absl::StrCat("setsockopt(SO_ZEROCOPY): ", strerror(ENOSYS)));
#endif
}

// Set a socket to non blocking mode
absl::Status PosixSocket::SetSocketNonBlocking(int non_blocking) {
  int oldflags = fcntl(fd_, F_GETFL, 0);
  if (oldflags < 0) {
    return absl::Status(absl::StatusCode::kInternal,
                        absl::StrCat("fcntl: ", strerror(errno)));
  }

  if (non_blocking) {
    oldflags |= O_NONBLOCK;
  } else {
    oldflags &= ~O_NONBLOCK;
  }

  if (fcntl(fd_, F_SETFL, oldflags) != 0) {
    return absl::Status(absl::StatusCode::kInternal,
                        absl::StrCat("fcntl: ", strerror(errno)));
  }

  return absl::OkStatus();
}

absl::Status PosixSocket::SetSocketNoSigpipeIfPossible() {
#ifdef GRPC_HAVE_SO_NOSIGPIPE
  int val = 1;
  int newval;
  socklen_t intlen = sizeof(newval);
  if (0 != setsockopt(fd_, SOL_SOCKET, SO_NOSIGPIPE, &val, sizeof(val))) {
    return absl::Status(
        absl::StatusCode::kInternal,
        absl::StrCat("setsockopt(SO_NOSIGPIPE): ", strerror(errno)));
  }
  if (0 != getsockopt(fd_, SOL_SOCKET, SO_NOSIGPIPE, &newval, &intlen)) {
    return absl::Status(
        absl::StatusCode::kInternal,
        absl::StrCat("getsockopt(SO_NOSIGPIPE): ", strerror(errno)));
  }
  if ((newval != 0) != (val != 0)) {
    return absl::Status(absl::StatusCode::kInternal,
                        "Failed to set SO_NOSIGPIPE");
  }
#endif
  return absl::OkStatus();
}

absl::Status PosixSocket::SetSocketIpPktInfoIfPossible() {
#ifdef GRPC_HAVE_IP_PKTINFO
  int get_local_ip = 1;
  if (0 != setsockopt(fd_, IPPROTO_IP, IP_PKTINFO, &get_local_ip,
                      sizeof(get_local_ip))) {
    return absl::Status(
        absl::StatusCode::kInternal,
        absl::StrCat("setsockopt(IP_PKTINFO): ", strerror(errno)));
  }
#endif
  return absl::OkStatus();
}

absl::Status PosixSocket::SetSocketIpv6RecvPktInfoIfPossible() {
#ifdef GRPC_HAVE_IPV6_RECVPKTINFO
  int get_local_ip = 1;
  if (0 != setsockopt(fd_, IPPROTO_IPV6, IPV6_RECVPKTINFO, &get_local_ip,
                      sizeof(get_local_ip))) {
    return absl::Status(
        absl::StatusCode::kInternal,
        absl::StrCat("setsockopt(IPV6_RECVPKTINFO): ", strerror(errno)));
  }
#endif
  return absl::OkStatus();
}

absl::Status PosixSocket::SetSocketSndBuf(int buffer_size_bytes) {
  return 0 == setsockopt(fd_, SOL_SOCKET, SO_SNDBUF, &buffer_size_bytes,
                         sizeof(buffer_size_bytes))
             ? absl::OkStatus()
             : absl::Status(
                   absl::StatusCode::kInternal,
                   absl::StrCat("setsockopt(SO_SNDBUF): ", strerror(errno)));
  ;
}

absl::Status PosixSocket::SetSocketRcvBuf(int buffer_size_bytes) {
  return 0 == setsockopt(fd_, SOL_SOCKET, SO_RCVBUF, &buffer_size_bytes,
                         sizeof(buffer_size_bytes))
             ? absl::OkStatus()
             : absl::Status(
                   absl::StatusCode::kInternal,
                   absl::StrCat("setsockopt(SO_RCVBUF): ", strerror(errno)));
}

// Set a socket to close on exec
absl::Status PosixSocket::SetSocketCloexec(int close_on_exec) {
  int oldflags = fcntl(fd_, F_GETFD, 0);
  if (oldflags < 0) {
    return absl::Status(absl::StatusCode::kInternal,
                        absl::StrCat("fcntl: ", strerror(errno)));
  }

  if (close_on_exec) {
    oldflags |= FD_CLOEXEC;
  } else {
    oldflags &= ~FD_CLOEXEC;
  }

  if (fcntl(fd_, F_SETFD, oldflags) != 0) {
    return absl::Status(absl::StatusCode::kInternal,
                        absl::StrCat("fcntl: ", strerror(errno)));
  }

  return absl::OkStatus();
}

// set a socket to reuse old addresses
absl::Status PosixSocket::SetSocketReuseAddr(int reuse) {
  int val = (reuse != 0);
  int newval;
  socklen_t intlen = sizeof(newval);
  if (0 != setsockopt(fd_, SOL_SOCKET, SO_REUSEADDR, &val, sizeof(val))) {
    return absl::Status(
        absl::StatusCode::kInternal,
        absl::StrCat("setsockopt(SO_REUSEADDR): ", strerror(errno)));
  }
  if (0 != getsockopt(fd_, SOL_SOCKET, SO_REUSEADDR, &newval, &intlen)) {
    return absl::Status(
        absl::StatusCode::kInternal,
        absl::StrCat("getsockopt(SO_REUSEADDR): ", strerror(errno)));
  }
  if ((newval != 0) != val) {
    return absl::Status(absl::StatusCode::kInternal,
                        "Failed to set SO_REUSEADDR");
  }

  return absl::OkStatus();
}

// set a socket to reuse old ports
absl::Status PosixSocket::SetSocketReusePort(int reuse) {
#ifndef SO_REUSEPORT
  return absl::Status(absl::StatusCode::kInternal,
                      "SO_REUSEPORT unavailable on compiling system");
#else
  int val = (reuse != 0);
  int newval;
  socklen_t intlen = sizeof(newval);
  if (0 != setsockopt(fd_, SOL_SOCKET, SO_REUSEPORT, &val, sizeof(val))) {
    return absl::Status(
        absl::StatusCode::kInternal,
        absl::StrCat("setsockopt(SO_REUSEPORT): ", strerror(errno)));
  }
  if (0 != getsockopt(fd_, SOL_SOCKET, SO_REUSEPORT, &newval, &intlen)) {
    return absl::Status(
        absl::StatusCode::kInternal,
        absl::StrCat("getsockopt(SO_REUSEPORT): ", strerror(errno)));
  }
  if ((newval != 0) != val) {
    return absl::Status(absl::StatusCode::kInternal,
                        "Failed to set SO_REUSEPORT");
  }

  return absl::OkStatus();
#endif
}

bool PosixSocket::IsSocketReusePortSupported() {
  static bool kSupportSoReusePort = []() -> bool {
    int s = socket(AF_INET, SOCK_STREAM, 0);
    if (s < 0) {
      // This might be an ipv6-only environment in which case
      // 'socket(AF_INET,..)' call would fail. Try creating IPv6 socket in that
      // case
      s = socket(AF_INET6, SOCK_STREAM, 0);
    }
    if (s >= 0) {
      PosixSocket sock(s);
      return sock.SetSocketReusePort(1).ok();
    } else {
      return false;
    }
  }();
  return kSupportSoReusePort;
}

// Disable nagle algorithm
absl::Status PosixSocket::SetSocketLowLatency(int low_latency) {
  int val = (low_latency != 0);
  int newval;
  socklen_t intlen = sizeof(newval);
  if (0 != setsockopt(fd_, IPPROTO_TCP, TCP_NODELAY, &val, sizeof(val))) {
    return absl::Status(
        absl::StatusCode::kInternal,
        absl::StrCat("setsockopt(TCP_NODELAY): ", strerror(errno)));
  }
  if (0 != getsockopt(fd_, IPPROTO_TCP, TCP_NODELAY, &newval, &intlen)) {
    return absl::Status(
        absl::StatusCode::kInternal,
        absl::StrCat("getsockopt(TCP_NODELAY): ", strerror(errno)));
  }
  if ((newval != 0) != val) {
    return absl::Status(absl::StatusCode::kInternal,
                        "Failed to set TCP_NODELAY");
  }
  return absl::OkStatus();
}

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

// Whether the socket supports TCP_USER_TIMEOUT option.
// (0: don't know, 1: support, -1: not support)
static std::atomic<int> g_socket_supports_tcp_user_timeout(
    SOCKET_SUPPORTS_TCP_USER_TIMEOUT_DEFAULT);

// Set TCP_USER_TIMEOUT
void PosixSocket::TrySetSocketTcpUserTimeout(const PosixTcpOptions& options,
                                             bool is_client) {
  static int kDefaultClientUserTimeoutMs = 20000;
  static int kDefaultServerUserTimeoutMs = 20000;
  static bool kDefaultClientUserTimeoutEnabled = false;
  static bool kDefaultServerUserTimeoutEnabled = true;
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
      if (0 != getsockopt(fd_, IPPROTO_TCP, TCP_USER_TIMEOUT, &newval, &len)) {
        gpr_log(GPR_INFO,
                "TCP_USER_TIMEOUT is not available. TCP_USER_TIMEOUT won't "
                "be used thereafter");
        g_socket_supports_tcp_user_timeout.store(-1);
      } else {
        gpr_log(GPR_INFO,
                "TCP_USER_TIMEOUT is available. TCP_USER_TIMEOUT will be "
                "used thereafter");
        g_socket_supports_tcp_user_timeout.store(1);
      }
    }
    if (g_socket_supports_tcp_user_timeout.load() > 0) {
      if (0 != setsockopt(fd_, IPPROTO_TCP, TCP_USER_TIMEOUT, &timeout,
                          sizeof(timeout))) {
        gpr_log(GPR_ERROR, "setsockopt(TCP_USER_TIMEOUT) %s", strerror(errno));
        return;
      }
      if (0 != getsockopt(fd_, IPPROTO_TCP, TCP_USER_TIMEOUT, &newval, &len)) {
        gpr_log(GPR_ERROR, "getsockopt(TCP_USER_TIMEOUT) %s", strerror(errno));
        return;
      }
      if (newval != timeout) {
        // Do not fail on failing to set TCP_USER_TIMEOUT
        gpr_log(GPR_ERROR, "Failed to set TCP_USER_TIMEOUT");
        return;
      }
    }
  }
}

// Set a socket using a grpc_socket_mutator
absl::Status PosixSocket::SetSocketMutator(grpc_fd_usage usage,
                                           grpc_socket_mutator* mutator) {
  GPR_ASSERT(mutator);
  if (!grpc_socket_mutator_mutate_fd(mutator, fd_, usage)) {
    return absl::Status(absl::StatusCode::kInternal,
                        "grpc_socket_mutator failed.");
  }
  return absl::OkStatus();
}

absl::Status PosixSocket::ApplySocketMutatorInOptions(
    grpc_fd_usage usage, const PosixTcpOptions& options) {
  if (options.socket_mutator == nullptr) {
    return absl::OkStatus();
  }
  return SetSocketMutator(usage, options.socket_mutator);
}

bool PosixSocket::SetSocketDualStack() {
  const int off = 0;
  return 0 == setsockopt(fd_, IPPROTO_IPV6, IPV6_V6ONLY, &off, sizeof(off));
}

bool PosixSocket::IsIpv6LoopbackAvailable() {
  static bool kIpv6LoopbackAvailable = []() -> bool {
    int fd = socket(AF_INET6, SOCK_STREAM, 0);
    bool loopback_available = false;
    if (fd < 0) {
      gpr_log(GPR_INFO, "Disabling AF_INET6 sockets because socket() failed.");
    } else {
      sockaddr_in6 addr;
      memset(&addr, 0, sizeof(addr));
      addr.sin6_family = AF_INET6;
      addr.sin6_addr.s6_addr[15] = 1; /* [::1]:0 */
      if (bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == 0) {
        loopback_available = true;
      } else {
        gpr_log(GPR_INFO,
                "Disabling AF_INET6 sockets because ::1 is not available.");
      }
      close(fd);
    }
    return loopback_available;
  }();
  return kIpv6LoopbackAvailable;
}

absl::StatusOr<PosixSocket> PosixSocket::CreateDualStackSocket(
    std::function<int(int, int, int)> socket_factory,
    const experimental::EventEngine::ResolvedAddress& addr, int type,
    int protocol, PosixSocket::DSMode& dsmode) {
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
    if (newfd < 0) {
      return ErrorForFd(newfd, addr);
    }
    PosixSocket sock(newfd);
    // Check if we've got a valid dualstack socket.
    if (sock.SetSocketDualStack()) {
      dsmode = PosixSocket::DSMode::DSMODE_DUALSTACK;
      return sock;
    }
    // If this isn't an IPv4 address, then return whatever we've got.
    if (!SockaddrIsV4Mapped(sock_addr)) {
      dsmode = PosixSocket::DSMode::DSMODE_IPV6;
      return sock;
    }
    // Fall back to AF_INET.
    if (newfd >= 0) {
      close(newfd);
    }
    family = AF_INET;
  }
  dsmode = family == AF_INET ? PosixSocket::DSMode::DSMODE_IPV4
                             : PosixSocket::DSMode::DSMODE_NONE;
  newfd = CreateSocket(socket_factory, family, type, protocol);
  if (newfd < 0) {
    return ErrorForFd(newfd, addr);
  }
  return PosixSocket(newfd);
}

#else /* GRPC_POSIX_SOCKET_UTILS_COMMON */

absl::Status PosixSocket::SetSocketZeroCopy() {
  GPR_ASSERT(false && "unimplemented");
}

absl::Status PosixSocket::SetSocketNonBlocking(int /*non_blocking*/) {
  GPR_ASSERT(false && "unimplemented");
}

absl::Status PosixSocket::SetSocketCloexec(int /*close_on_exec*/) {
  GPR_ASSERT(false && "unimplemented");
}

absl::Status PosixSocket::SetSocketReuseAddr(int /*reuse*/) {
  GPR_ASSERT(false && "unimplemented");
}

absl::Status PosixSocket::SetSocketLowLatency(int /*low_latency*/) {
  GPR_ASSERT(false && "unimplemented");
}

absl::Status PosixSocket::SetSocketReusePort(int /*reuse*/) {
  GPR_ASSERT(false && "unimplemented");
}

void PosixSocket::TrySetSocketTcpUserTimeout(const PosixTcpOptions& /*options*/,
                                             bool /*is_client*/) {
  GPR_ASSERT(false && "unimplemented");
}

absl::Status PosixSocket::SetSocketNoSigpipeIfPossible() {
  GPR_ASSERT(false && "unimplemented");
}

absl::Status PosixSocket::SetSocketIpPktInfoIfPossible() {
  GPR_ASSERT(false && "unimplemented");
}

absl::Status PosixSocket::SetSocketIpv6RecvPktInfoIfPossible() {
  GPR_ASSERT(false && "unimplemented");
}

absl::Status PosixSocket::SetSocketSndBuf(int /*buffer_size_bytes*/) {
  GPR_ASSERT(false && "unimplemented");
}

absl::Status PosixSocket::SetSocketRcvBuf(int /*buffer_size_bytes*/) {
  GPR_ASSERT(false && "unimplemented");
}

absl::Status PosixSocket::SetSocketMutator(grpc_fd_usage /*usage*/,
                                           grpc_socket_mutator* /*mutator*/) {
  GPR_ASSERT(false && "unimplemented");
}

absl::Status PosixSocket::ApplySocketMutatorInOptions(
    grpc_fd_usage /*usage*/, const PosixTcpOptions& /*options*/) {
  GPR_ASSERT(false && "unimplemented");
}

bool PosixSocket::SetSocketDualStack() { GPR_ASSERT(false && "unimplemented"); }

static bool PosixSocket::IsSocketReusePortSupported() {
  GPR_ASSERT(false && "unimplemented");
}

static bool PosixSocket::IsIpv6LoopbackAvailable() {
  GPR_ASSERT(false && "unimplemented");
}

static absl::StatusOr<PosixSocket> PosixSocket::CreateDualStackSocket(
    std::function<int(int /*domain*/, int /*type*/, int /*protocol*/)>
    /* socket_factory */,
    const experimental::EventEngine::ResolvedAddress& /*addr*/, int /*type*/,
    int /*protocol*/, DSMode& /*dsmode*/) {
  GPR_ASSERT(false && "unimplemented");
}
}

#endif /* GRPC_POSIX_SOCKET_UTILS_COMMON */

}  // namespace iomgr_engine
}  // namespace grpc_event_engine
