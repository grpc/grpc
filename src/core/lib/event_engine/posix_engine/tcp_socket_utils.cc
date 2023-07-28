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

#include "src/core/lib/event_engine/posix_engine/tcp_socket_utils.h"

#include <errno.h>
#include <inttypes.h>
#include <limits.h>

#include "absl/cleanup/cleanup.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "absl/types/optional.h"

#include <grpc/event_engine/event_engine.h>
#include <grpc/impl/channel_arg_names.h>

#include "src/core/lib/gpr/useful.h"
#include "src/core/lib/gprpp/crash.h"  // IWYU pragma: keep
#include "src/core/lib/gprpp/time.h"
#include "src/core/lib/iomgr/port.h"

#ifdef GRPC_POSIX_SOCKET_UTILS_COMMON
#include <arpa/inet.h>  // IWYU pragma: keep
#ifdef GRPC_LINUX_TCP_H
#include <linux/tcp.h>
#else
#include <netinet/in.h>  // IWYU pragma: keep
#include <netinet/tcp.h>
#endif
#include <fcntl.h>
#include <sys/socket.h>
#include <unistd.h>
#endif  //  GRPC_POSIX_SOCKET_UTILS_COMMON

#include <atomic>
#include <cstring>

#include "absl/status/status.h"

#include <grpc/support/log.h>

#include "src/core/lib/event_engine/tcp_socket_utils.h"
#include "src/core/lib/gprpp/status_helper.h"
#include "src/core/lib/gprpp/strerror.h"

#ifdef GRPC_HAVE_UNIX_SOCKET
#include <sys/stat.h>  // IWYU pragma: keep
#include <sys/un.h>
#endif

namespace grpc_event_engine {
namespace experimental {

namespace {

int AdjustValue(int default_value, int min_value, int max_value,
                absl::optional<int> actual_value) {
  if (!actual_value.has_value() || *actual_value < min_value ||
      *actual_value > max_value) {
    return default_value;
  }
  return *actual_value;
}

// The default values for TCP_USER_TIMEOUT are currently configured to be in
// line with the default values of KEEPALIVE_TIMEOUT as proposed in
// https://github.com/grpc/proposal/blob/master/A18-tcp-user-timeout.md */
int kDefaultClientUserTimeoutMs = 20000;
int kDefaultServerUserTimeoutMs = 20000;
bool kDefaultClientUserTimeoutEnabled = false;
bool kDefaultServerUserTimeoutEnabled = true;

#ifdef GRPC_POSIX_SOCKET_UTILS_COMMON

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
    GRPC_LOG_EVERY_N_SEC(
        10, GPR_ERROR,
        "socket(%d, %d, %d) returned %d with error: |%s|. This process "
        "might not have a sufficient file descriptor limit for the number "
        "of connections grpc wants to open (which is generally a function of "
        "the number of grpc channels, the lb policy of each channel, and the "
        "number of backends each channel is load balancing across).",
        family, type, protocol, res, grpc_core::StrError(errno).c_str());
    errno = saved_errno;
  }
  return res;
}

absl::Status PrepareTcpClientSocket(PosixSocketWrapper sock,
                                    const EventEngine::ResolvedAddress& addr,
                                    const PosixTcpOptions& options) {
  bool close_fd = true;
  auto sock_cleanup = absl::MakeCleanup([&close_fd, &sock]() -> void {
    if (close_fd and sock.Fd() >= 0) {
      close(sock.Fd());
    }
  });
  GRPC_RETURN_IF_ERROR(sock.SetSocketNonBlocking(1));
  GRPC_RETURN_IF_ERROR(sock.SetSocketCloexec(1));
  if (options.tcp_receive_buffer_size != options.kReadBufferSizeUnset) {
    GRPC_RETURN_IF_ERROR(sock.SetSocketRcvBuf(options.tcp_receive_buffer_size));
  }
  if (addr.address()->sa_family != AF_UNIX && !ResolvedAddressIsVSock(addr)) {
    // If its not a unix socket or vsock address.
    GRPC_RETURN_IF_ERROR(sock.SetSocketLowLatency(1));
    GRPC_RETURN_IF_ERROR(sock.SetSocketReuseAddr(1));
    GRPC_RETURN_IF_ERROR(sock.SetSocketDscp(options.dscp));
    sock.TrySetSocketTcpUserTimeout(options, true);
  }
  GRPC_RETURN_IF_ERROR(sock.SetSocketNoSigpipeIfPossible());
  GRPC_RETURN_IF_ERROR(sock.ApplySocketMutatorInOptions(
      GRPC_FD_CLIENT_CONNECTION_USAGE, options));
  // No errors. Set close_fd to false to ensure the socket is not closed.
  close_fd = false;
  return absl::OkStatus();
}

bool SetSocketDualStack(int fd) {
  const int off = 0;
  return 0 == setsockopt(fd, IPPROTO_IPV6, IPV6_V6ONLY, &off, sizeof(off));
}

#endif  // GRPC_POSIX_SOCKET_UTILS_COMMON

}  // namespace

PosixTcpOptions TcpOptionsFromEndpointConfig(const EndpointConfig& config) {
  void* value;
  PosixTcpOptions options;
  options.tcp_read_chunk_size = AdjustValue(
      PosixTcpOptions::kDefaultReadChunkSize, 1, PosixTcpOptions::kMaxChunkSize,
      config.GetInt(GRPC_ARG_TCP_READ_CHUNK_SIZE));
  options.tcp_min_read_chunk_size =
      AdjustValue(PosixTcpOptions::kDefaultMinReadChunksize, 1,
                  PosixTcpOptions::kMaxChunkSize,
                  config.GetInt(GRPC_ARG_TCP_MIN_READ_CHUNK_SIZE));
  options.tcp_max_read_chunk_size =
      AdjustValue(PosixTcpOptions::kDefaultMaxReadChunksize, 1,
                  PosixTcpOptions::kMaxChunkSize,
                  config.GetInt(GRPC_ARG_TCP_MAX_READ_CHUNK_SIZE));
  options.tcp_tx_zerocopy_send_bytes_threshold =
      AdjustValue(PosixTcpOptions::kDefaultSendBytesThreshold, 0, INT_MAX,
                  config.GetInt(GRPC_ARG_TCP_TX_ZEROCOPY_SEND_BYTES_THRESHOLD));
  options.tcp_tx_zerocopy_max_simultaneous_sends =
      AdjustValue(PosixTcpOptions::kDefaultMaxSends, 0, INT_MAX,
                  config.GetInt(GRPC_ARG_TCP_TX_ZEROCOPY_MAX_SIMULT_SENDS));
  options.tcp_receive_buffer_size =
      AdjustValue(PosixTcpOptions::kReadBufferSizeUnset, 0, INT_MAX,
                  config.GetInt(GRPC_ARG_TCP_RECEIVE_BUFFER_SIZE));
  options.tcp_tx_zero_copy_enabled =
      (AdjustValue(PosixTcpOptions::kZerocpTxEnabledDefault, 0, 1,
                   config.GetInt(GRPC_ARG_TCP_TX_ZEROCOPY_ENABLED)) != 0);
  options.keep_alive_time_ms =
      AdjustValue(0, 1, INT_MAX, config.GetInt(GRPC_ARG_KEEPALIVE_TIME_MS));
  options.keep_alive_timeout_ms =
      AdjustValue(0, 1, INT_MAX, config.GetInt(GRPC_ARG_KEEPALIVE_TIMEOUT_MS));
  options.expand_wildcard_addrs =
      (AdjustValue(0, 1, INT_MAX,
                   config.GetInt(GRPC_ARG_EXPAND_WILDCARD_ADDRS)) != 0);
  options.dscp = AdjustValue(PosixTcpOptions::kDscpNotSet, 0, 63,
                             config.GetInt(GRPC_ARG_DSCP));
  options.allow_reuse_port = PosixSocketWrapper::IsSocketReusePortSupported();
  auto allow_reuse_port_value = config.GetInt(GRPC_ARG_ALLOW_REUSEPORT);
  if (allow_reuse_port_value.has_value()) {
    options.allow_reuse_port =
        (AdjustValue(0, 1, INT_MAX, config.GetInt(GRPC_ARG_ALLOW_REUSEPORT)) !=
         0);
  }
  if (options.tcp_min_read_chunk_size > options.tcp_max_read_chunk_size) {
    options.tcp_min_read_chunk_size = options.tcp_max_read_chunk_size;
  }
  options.tcp_read_chunk_size = grpc_core::Clamp(
      options.tcp_read_chunk_size, options.tcp_min_read_chunk_size,
      options.tcp_max_read_chunk_size);

  value = config.GetVoidPointer(GRPC_ARG_RESOURCE_QUOTA);
  if (value != nullptr) {
    options.resource_quota =
        reinterpret_cast<grpc_core::ResourceQuota*>(value)->Ref();
  }
  value = config.GetVoidPointer(GRPC_ARG_SOCKET_MUTATOR);
  if (value != nullptr) {
    options.socket_mutator =
        grpc_socket_mutator_ref(static_cast<grpc_socket_mutator*>(value));
  }
  return options;
}

#ifdef GRPC_POSIX_SOCKETUTILS

int Accept4(int sockfd,
            grpc_event_engine::experimental::EventEngine::ResolvedAddress& addr,
            int nonblock, int cloexec) {
  int fd, flags;
  EventEngine::ResolvedAddress peer_addr;
  socklen_t len = EventEngine::ResolvedAddress::MAX_SIZE_BYTES;
  fd = accept(sockfd, const_cast<sockaddr*>(peer_addr.address()), &len);
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
  addr = EventEngine::ResolvedAddress(peer_addr.address(), len);
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
  EventEngine::ResolvedAddress peer_addr;
  socklen_t len = EventEngine::ResolvedAddress::MAX_SIZE_BYTES;
  int ret =
      accept4(sockfd, const_cast<sockaddr*>(peer_addr.address()), &len, flags);
  addr = EventEngine::ResolvedAddress(peer_addr.address(), len);
  return ret;
}

#endif  // GRPC_LINUX_SOCKETUTILS

#ifdef GRPC_POSIX_SOCKET_UTILS_COMMON

void UnlinkIfUnixDomainSocket(
    const EventEngine::ResolvedAddress& resolved_addr) {
#ifdef GRPC_HAVE_UNIX_SOCKET
  if (resolved_addr.address()->sa_family != AF_UNIX) {
    return;
  }
  struct sockaddr_un* un = reinterpret_cast<struct sockaddr_un*>(
      const_cast<sockaddr*>(resolved_addr.address()));

  // There is nothing to unlink for an abstract unix socket
  if (un->sun_path[0] == '\0' && un->sun_path[1] != '\0') {
    return;
  }

  struct stat st;
  if (stat(un->sun_path, &st) == 0 && (st.st_mode & S_IFMT) == S_IFSOCK) {
    unlink(un->sun_path);
  }
#else
  (void)resolved_addr;
#endif
}

// Instruct the kernel to wait for specified number of bytes to be received on
// the socket before generating an interrupt for packet receive. If the call
// succeeds, it returns the number of bytes (wait threshold) that was actually
// set.
absl::StatusOr<int> PosixSocketWrapper::SetSocketRcvLowat(int bytes) {
  if (setsockopt(fd_, SOL_SOCKET, SO_RCVLOWAT, &bytes, sizeof(bytes)) != 0) {
    return absl::Status(
        absl::StatusCode::kInternal,
        absl::StrCat("setsockopt(SO_RCVLOWAT): ", grpc_core::StrError(errno)));
  }
  return bytes;
}

// Set a socket to use zerocopy
absl::Status PosixSocketWrapper::SetSocketZeroCopy() {
#ifdef GRPC_LINUX_ERRQUEUE
  const int enable = 1;
  auto err = setsockopt(fd_, SOL_SOCKET, SO_ZEROCOPY, &enable, sizeof(enable));
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
absl::Status PosixSocketWrapper::SetSocketNonBlocking(int non_blocking) {
  int oldflags = fcntl(fd_, F_GETFL, 0);
  if (oldflags < 0) {
    return absl::Status(absl::StatusCode::kInternal,
                        absl::StrCat("fcntl: ", grpc_core::StrError(errno)));
  }

  if (non_blocking) {
    oldflags |= O_NONBLOCK;
  } else {
    oldflags &= ~O_NONBLOCK;
  }

  if (fcntl(fd_, F_SETFL, oldflags) != 0) {
    return absl::Status(absl::StatusCode::kInternal,
                        absl::StrCat("fcntl: ", grpc_core::StrError(errno)));
  }

  return absl::OkStatus();
}

absl::Status PosixSocketWrapper::SetSocketNoSigpipeIfPossible() {
#ifdef GRPC_HAVE_SO_NOSIGPIPE
  int val = 1;
  int newval;
  socklen_t intlen = sizeof(newval);
  if (0 != setsockopt(fd_, SOL_SOCKET, SO_NOSIGPIPE, &val, sizeof(val))) {
    return absl::Status(
        absl::StatusCode::kInternal,
        absl::StrCat("setsockopt(SO_NOSIGPIPE): ", grpc_core::StrError(errno)));
  }
  if (0 != getsockopt(fd_, SOL_SOCKET, SO_NOSIGPIPE, &newval, &intlen)) {
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

absl::Status PosixSocketWrapper::SetSocketIpPktInfoIfPossible() {
#ifdef GRPC_HAVE_IP_PKTINFO
  int get_local_ip = 1;
  if (0 != setsockopt(fd_, IPPROTO_IP, IP_PKTINFO, &get_local_ip,
                      sizeof(get_local_ip))) {
    return absl::Status(
        absl::StatusCode::kInternal,
        absl::StrCat("setsockopt(IP_PKTINFO): ", grpc_core::StrError(errno)));
  }
#endif
  return absl::OkStatus();
}

absl::Status PosixSocketWrapper::SetSocketIpv6RecvPktInfoIfPossible() {
#ifdef GRPC_HAVE_IPV6_RECVPKTINFO
  int get_local_ip = 1;
  if (0 != setsockopt(fd_, IPPROTO_IPV6, IPV6_RECVPKTINFO, &get_local_ip,
                      sizeof(get_local_ip))) {
    return absl::Status(absl::StatusCode::kInternal,
                        absl::StrCat("setsockopt(IPV6_RECVPKTINFO): ",
                                     grpc_core::StrError(errno)));
  }
#endif
  return absl::OkStatus();
}

absl::Status PosixSocketWrapper::SetSocketSndBuf(int buffer_size_bytes) {
  return 0 == setsockopt(fd_, SOL_SOCKET, SO_SNDBUF, &buffer_size_bytes,
                         sizeof(buffer_size_bytes))
             ? absl::OkStatus()
             : absl::Status(absl::StatusCode::kInternal,
                            absl::StrCat("setsockopt(SO_SNDBUF): ",
                                         grpc_core::StrError(errno)));
}

absl::Status PosixSocketWrapper::SetSocketRcvBuf(int buffer_size_bytes) {
  return 0 == setsockopt(fd_, SOL_SOCKET, SO_RCVBUF, &buffer_size_bytes,
                         sizeof(buffer_size_bytes))
             ? absl::OkStatus()
             : absl::Status(absl::StatusCode::kInternal,
                            absl::StrCat("setsockopt(SO_RCVBUF): ",
                                         grpc_core::StrError(errno)));
}

// Set a socket to close on exec
absl::Status PosixSocketWrapper::SetSocketCloexec(int close_on_exec) {
  int oldflags = fcntl(fd_, F_GETFD, 0);
  if (oldflags < 0) {
    return absl::Status(absl::StatusCode::kInternal,
                        absl::StrCat("fcntl: ", grpc_core::StrError(errno)));
  }

  if (close_on_exec) {
    oldflags |= FD_CLOEXEC;
  } else {
    oldflags &= ~FD_CLOEXEC;
  }

  if (fcntl(fd_, F_SETFD, oldflags) != 0) {
    return absl::Status(absl::StatusCode::kInternal,
                        absl::StrCat("fcntl: ", grpc_core::StrError(errno)));
  }

  return absl::OkStatus();
}

// set a socket to reuse old addresses
absl::Status PosixSocketWrapper::SetSocketReuseAddr(int reuse) {
  int val = (reuse != 0);
  int newval;
  socklen_t intlen = sizeof(newval);
  if (0 != setsockopt(fd_, SOL_SOCKET, SO_REUSEADDR, &val, sizeof(val))) {
    return absl::Status(
        absl::StatusCode::kInternal,
        absl::StrCat("setsockopt(SO_REUSEADDR): ", grpc_core::StrError(errno)));
  }
  if (0 != getsockopt(fd_, SOL_SOCKET, SO_REUSEADDR, &newval, &intlen)) {
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

// set a socket to reuse old ports
absl::Status PosixSocketWrapper::SetSocketReusePort(int reuse) {
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
        absl::StrCat("setsockopt(SO_REUSEPORT): ", grpc_core::StrError(errno)));
  }
  if (0 != getsockopt(fd_, SOL_SOCKET, SO_REUSEPORT, &newval, &intlen)) {
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

bool PosixSocketWrapper::IsSocketReusePortSupported() {
  static bool kSupportSoReusePort = []() -> bool {
    int s = socket(AF_INET, SOCK_STREAM, 0);
    if (s < 0) {
      // This might be an ipv6-only environment in which case
      // 'socket(AF_INET,..)' call would fail. Try creating IPv6 socket in
      // that case
      s = socket(AF_INET6, SOCK_STREAM, 0);
    }
    if (s >= 0) {
      PosixSocketWrapper sock(s);
      bool result = sock.SetSocketReusePort(1).ok();
      close(sock.Fd());
      return result;
    } else {
      return false;
    }
  }();
  return kSupportSoReusePort;
}

// Disable nagle algorithm
absl::Status PosixSocketWrapper::SetSocketLowLatency(int low_latency) {
  int val = (low_latency != 0);
  int newval;
  socklen_t intlen = sizeof(newval);
  if (0 != setsockopt(fd_, IPPROTO_TCP, TCP_NODELAY, &val, sizeof(val))) {
    return absl::Status(
        absl::StatusCode::kInternal,
        absl::StrCat("setsockopt(TCP_NODELAY): ", grpc_core::StrError(errno)));
  }
  if (0 != getsockopt(fd_, IPPROTO_TCP, TCP_NODELAY, &newval, &intlen)) {
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

// Set Differentiated Services Code Point (DSCP)
absl::Status PosixSocketWrapper::SetSocketDscp(int dscp) {
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
  if (0 == getsockopt(fd_, IPPROTO_IP, IP_TOS, &val, &intlen)) {
    newval |= (val & 0x3);
    if (0 != setsockopt(fd_, IPPROTO_IP, IP_TOS, &newval, sizeof(newval))) {
      return absl::Status(
          absl::StatusCode::kInternal,
          absl::StrCat("setsockopt(IP_TOS): ", grpc_core::StrError(errno)));
    }
  }
  // Get ECN from current Traffic Class value if IPv6 is available
  if (0 == getsockopt(fd_, IPPROTO_IPV6, IPV6_TCLASS, &val, &intlen)) {
    newval |= (val & 0x3);
    if (0 !=
        setsockopt(fd_, IPPROTO_IPV6, IPV6_TCLASS, &newval, sizeof(newval))) {
      return absl::Status(absl::StatusCode::kInternal,
                          absl::StrCat("setsockopt(IPV6_TCLASS): ",
                                       grpc_core::StrError(errno)));
    }
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

void PosixSocketWrapper::ConfigureDefaultTcpUserTimeout(bool enable,
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

// Set TCP_USER_TIMEOUT
void PosixSocketWrapper::TrySetSocketTcpUserTimeout(
    const PosixTcpOptions& options, bool is_client) {
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
        gpr_log(GPR_ERROR, "setsockopt(TCP_USER_TIMEOUT) %s",
                grpc_core::StrError(errno).c_str());
        return;
      }
      if (0 != getsockopt(fd_, IPPROTO_TCP, TCP_USER_TIMEOUT, &newval, &len)) {
        gpr_log(GPR_ERROR, "getsockopt(TCP_USER_TIMEOUT) %s",
                grpc_core::StrError(errno).c_str());
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
absl::Status PosixSocketWrapper::SetSocketMutator(
    grpc_fd_usage usage, grpc_socket_mutator* mutator) {
  GPR_ASSERT(mutator);
  if (!grpc_socket_mutator_mutate_fd(mutator, fd_, usage)) {
    return absl::Status(absl::StatusCode::kInternal,
                        "grpc_socket_mutator failed.");
  }
  return absl::OkStatus();
}

absl::Status PosixSocketWrapper::ApplySocketMutatorInOptions(
    grpc_fd_usage usage, const PosixTcpOptions& options) {
  if (options.socket_mutator == nullptr) {
    return absl::OkStatus();
  }
  return SetSocketMutator(usage, options.socket_mutator);
}

bool PosixSocketWrapper::IsIpv6LoopbackAvailable() {
  static bool kIpv6LoopbackAvailable = []() -> bool {
    int fd = socket(AF_INET6, SOCK_STREAM, 0);
    bool loopback_available = false;
    if (fd < 0) {
      gpr_log(GPR_INFO, "Disabling AF_INET6 sockets because socket() failed.");
    } else {
      sockaddr_in6 addr;
      memset(&addr, 0, sizeof(addr));
      addr.sin6_family = AF_INET6;
      addr.sin6_addr.s6_addr[15] = 1;  // [::1]:0
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

absl::StatusOr<EventEngine::ResolvedAddress>
PosixSocketWrapper::LocalAddress() {
  EventEngine::ResolvedAddress addr;
  socklen_t len = EventEngine::ResolvedAddress::MAX_SIZE_BYTES;
  if (getsockname(fd_, const_cast<sockaddr*>(addr.address()), &len) < 0) {
    return absl::InternalError(
        absl::StrCat("getsockname:", grpc_core::StrError(errno)));
  }
  return EventEngine::ResolvedAddress(addr.address(), len);
}

absl::StatusOr<EventEngine::ResolvedAddress> PosixSocketWrapper::PeerAddress() {
  EventEngine::ResolvedAddress addr;
  socklen_t len = EventEngine::ResolvedAddress::MAX_SIZE_BYTES;
  if (getpeername(fd_, const_cast<sockaddr*>(addr.address()), &len) < 0) {
    return absl::InternalError(
        absl::StrCat("getpeername:", grpc_core::StrError(errno)));
  }
  return EventEngine::ResolvedAddress(addr.address(), len);
}

absl::StatusOr<std::string> PosixSocketWrapper::LocalAddressString() {
  auto status = LocalAddress();
  if (!status.ok()) {
    return status.status();
  }
  return ResolvedAddressToNormalizedString((*status));
}

absl::StatusOr<std::string> PosixSocketWrapper::PeerAddressString() {
  auto status = PeerAddress();
  if (!status.ok()) {
    return status.status();
  }
  return ResolvedAddressToNormalizedString((*status));
}

absl::StatusOr<PosixSocketWrapper> PosixSocketWrapper::CreateDualStackSocket(
    std::function<int(int, int, int)> socket_factory,
    const experimental::EventEngine::ResolvedAddress& addr, int type,
    int protocol, PosixSocketWrapper::DSMode& dsmode) {
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
      dsmode = PosixSocketWrapper::DSMode::DSMODE_DUALSTACK;
      return PosixSocketWrapper(newfd);
    }
    // If this isn't an IPv4 address, then return whatever we've got.
    if (!ResolvedAddressIsV4Mapped(addr, nullptr)) {
      if (newfd <= 0) {
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
}

absl::StatusOr<PosixSocketWrapper::PosixSocketCreateResult>
PosixSocketWrapper::CreateAndPrepareTcpClientSocket(
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
      PosixSocketWrapper::CreateDualStackSocket(nullptr, mapped_target_addr,
                                                SOCK_STREAM, 0, dsmode);
  if (!posix_socket_wrapper.ok()) {
    return posix_socket_wrapper.status();
  }

  if (dsmode == PosixSocketWrapper::DSMode::DSMODE_IPV4) {
    // Original addr is either v4 or v4 mapped to v6. Set mapped_addr to v4.
    if (!ResolvedAddressIsV4Mapped(target_addr, &mapped_target_addr)) {
      mapped_target_addr = target_addr;
    }
  }

  auto error = PrepareTcpClientSocket(*posix_socket_wrapper, mapped_target_addr,
                                      options);
  if (!error.ok()) {
    return error;
  }
  return PosixSocketWrapper::PosixSocketCreateResult{*posix_socket_wrapper,
                                                     mapped_target_addr};
}

#else  // GRPC_POSIX_SOCKET_UTILS_COMMON

absl::StatusOr<int> PosixSocketWrapper::SetSocketRcvLowat(int /*bytes*/) {
  grpc_core::Crash("unimplemented");
}

absl::Status PosixSocketWrapper::SetSocketZeroCopy() {
  grpc_core::Crash("unimplemented");
}

absl::Status PosixSocketWrapper::SetSocketNonBlocking(int /*non_blocking*/) {
  grpc_core::Crash("unimplemented");
}

absl::Status PosixSocketWrapper::SetSocketCloexec(int /*close_on_exec*/) {
  grpc_core::Crash("unimplemented");
}

absl::Status PosixSocketWrapper::SetSocketReuseAddr(int /*reuse*/) {
  grpc_core::Crash("unimplemented");
}

absl::Status PosixSocketWrapper::SetSocketLowLatency(int /*low_latency*/) {
  grpc_core::Crash("unimplemented");
}

absl::Status PosixSocketWrapper::SetSocketReusePort(int /*reuse*/) {
  grpc_core::Crash("unimplemented");
}

absl::Status PosixSocketWrapper::SetSocketDscp(int /*dscp*/) {
  grpc_core::Crash("unimplemented");
}

void PosixSocketWrapper::ConfigureDefaultTcpUserTimeout(bool /*enable*/,
                                                        int /*timeout*/,
                                                        bool /*is_client*/) {}

void PosixSocketWrapper::TrySetSocketTcpUserTimeout(
    const PosixTcpOptions& /*options*/, bool /*is_client*/) {
  grpc_core::Crash("unimplemented");
}

absl::Status PosixSocketWrapper::SetSocketNoSigpipeIfPossible() {
  grpc_core::Crash("unimplemented");
}

absl::Status PosixSocketWrapper::SetSocketIpPktInfoIfPossible() {
  grpc_core::Crash("unimplemented");
}

absl::Status PosixSocketWrapper::SetSocketIpv6RecvPktInfoIfPossible() {
  grpc_core::Crash("unimplemented");
}

absl::Status PosixSocketWrapper::SetSocketSndBuf(int /*buffer_size_bytes*/) {
  grpc_core::Crash("unimplemented");
}

absl::Status PosixSocketWrapper::SetSocketRcvBuf(int /*buffer_size_bytes*/) {
  grpc_core::Crash("unimplemented");
}

absl::Status PosixSocketWrapper::SetSocketMutator(
    grpc_fd_usage /*usage*/, grpc_socket_mutator* /*mutator*/) {
  grpc_core::Crash("unimplemented");
}

absl::Status PosixSocketWrapper::ApplySocketMutatorInOptions(
    grpc_fd_usage /*usage*/, const PosixTcpOptions& /*options*/) {
  grpc_core::Crash("unimplemented");
}

bool PosixSocketWrapper::IsSocketReusePortSupported() {
  grpc_core::Crash("unimplemented");
}

bool PosixSocketWrapper::IsIpv6LoopbackAvailable() {
  grpc_core::Crash("unimplemented");
}

absl::StatusOr<PosixSocketWrapper> PosixSocketWrapper::CreateDualStackSocket(
    std::function<int(int /*domain*/, int /*type*/, int /*protocol*/)>
    /* socket_factory */,
    const experimental::EventEngine::ResolvedAddress& /*addr*/, int /*type*/,
    int /*protocol*/, DSMode& /*dsmode*/) {
  grpc_core::Crash("unimplemented");
}

absl::StatusOr<PosixSocketWrapper::PosixSocketCreateResult>
PosixSocketWrapper::CreateAndPrepareTcpClientSocket(
    const PosixTcpOptions& /*options*/,
    const EventEngine::ResolvedAddress& /*target_addr*/) {
  grpc_core::Crash("unimplemented");
}

#endif  // GRPC_POSIX_SOCKET_UTILS_COMMON

}  // namespace experimental
}  // namespace grpc_event_engine
