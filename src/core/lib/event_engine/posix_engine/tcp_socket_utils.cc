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

#include "src/core/lib/event_engine/posix_engine/tcp_socket_utils.h"

#include <errno.h>
#include <grpc/event_engine/event_engine.h>
#include <grpc/event_engine/memory_allocator.h>
#include <grpc/impl/channel_arg_names.h>
#include <grpc/support/port_platform.h>
#include <limits.h>

#include "absl/cleanup/cleanup.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/types/optional.h"
#include "src/core/lib/iomgr/port.h"
#include "src/core/util/crash.h"  // IWYU pragma: keep
#include "src/core/util/useful.h"

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

#include <cstring>

#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "src/core/lib/event_engine/tcp_socket_utils.h"
#include "src/core/util/status_helper.h"
#include "src/core/util/strerror.h"

#ifdef GRPC_HAVE_UNIX_SOCKET
#ifdef GPR_WINDOWS
// clang-format off
#include <ws2def.h>
#include <afunix.h>
// clang-format on
#else
#include <sys/stat.h>  // IWYU pragma: keep
#include <sys/un.h>
#endif  // GPR_WINDOWS
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

#ifdef GRPC_POSIX_SOCKET_UTILS_COMMON

absl::Status ErrorForFd(
    FileDescriptor fd, const experimental::EventEngine::ResolvedAddress& addr) {
  if (fd.ready()) return absl::OkStatus();
  const char* addr_str = reinterpret_cast<const char*>(addr.address());
  return absl::Status(absl::StatusCode::kInternal,
                      absl::StrCat("socket: ", grpc_core::StrError(errno),
                                   std::string(addr_str, addr.size())));
}

FileDescriptor CreateSocket(
    const SystemApi& posix_apis,
    std::function<FileDescriptor(int, int, int)> socket_factory, int family,
    int type, int protocol) {
  FileDescriptor res = socket_factory != nullptr
                           ? socket_factory(family, type, protocol)
                           : posix_apis.Socket(family, type, protocol);
  if (!res.ready() && errno == EMFILE) {
    int saved_errno = errno;
    LOG_EVERY_N_SEC(ERROR, 10)
        << "socket(" << family << ", " << type << ", " << protocol
        << ") returned " << res.fd() << " with error: |"
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

absl::Status PrepareTcpClientSocket(const SystemApi& system_api,
                                    FileDescriptor fd,
                                    const EventEngine::ResolvedAddress& addr,
                                    const PosixTcpOptions& options) {
  bool close_fd = true;
  auto sock_cleanup = absl::MakeCleanup([&close_fd, &system_api, fd]() -> void {
    if (close_fd && fd.ready()) {
      system_api.Close(fd);
    }
  });
  GRPC_RETURN_IF_ERROR(system_api.SetSocketNonBlocking(fd, 1));
  GRPC_RETURN_IF_ERROR(system_api.SetSocketCloexec(fd, 1));
  if (options.tcp_receive_buffer_size != options.kReadBufferSizeUnset) {
    GRPC_RETURN_IF_ERROR(
        system_api.SetSocketRcvBuf(fd, options.tcp_receive_buffer_size));
  }
  if (addr.address()->sa_family != AF_UNIX && !ResolvedAddressIsVSock(addr)) {
    // If its not a unix socket or vsock address.
    GRPC_RETURN_IF_ERROR(system_api.SetSocketLowLatency(fd, 1));
    GRPC_RETURN_IF_ERROR(system_api.SetSocketReuseAddr(fd, 1));
    GRPC_RETURN_IF_ERROR(system_api.SetSocketDscp(fd, options.dscp));
    system_api.TrySetSocketTcpUserTimeout(fd, options.keep_alive_time_ms,
                                          options.keep_alive_timeout_ms, true);
  }
  GRPC_RETURN_IF_ERROR(system_api.SetSocketNoSigpipeIfPossible(fd));
  GRPC_RETURN_IF_ERROR(ApplySocketMutatorInOptions(
      fd, GRPC_FD_CLIENT_CONNECTION_USAGE, options));
  // No errors. Set close_fd to false to ensure the socket is not closed.
  close_fd = false;
  return absl::OkStatus();
}

#endif  // GRPC_POSIX_SOCKET_UTILS_COMMON

}  // namespace

#ifdef GRPC_POSIX_SOCKET_UTILS_COMMON
#ifndef GRPC_SET_SOCKET_DUALSTACK_CUSTOM

bool SetSocketDualStack(const SystemApi& posix_apis, FileDescriptor fd) {
  const int off = 0;
  return 0 == posix_apis.SetSockOpt(fd, IPPROTO_IPV6, IPV6_V6ONLY, &off,
                                    sizeof(off));
}

#endif  // GRPC_SET_SOCKET_DUALSTACK_CUSTOM
#endif  // GRPC_POSIX_SOCKET_UTILS_COMMON

PosixTcpOptions TcpOptionsFromEndpointConfig(const SystemApi& system_api,
                                             const EndpointConfig& config) {
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
  options.dscp =
      AdjustValue(SystemApi::kDscpNotSet, 0, 63, config.GetInt(GRPC_ARG_DSCP));
  options.allow_reuse_port = system_api.IsSocketReusePortSupported();
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
  value =
      config.GetVoidPointer(GRPC_ARG_EVENT_ENGINE_USE_MEMORY_ALLOCATOR_FACTORY);
  if (value != nullptr) {
    options.memory_allocator_factory =
        static_cast<grpc_event_engine::experimental::MemoryAllocatorFactory*>(
            value);
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

// Set a socket using a grpc_socket_mutator
absl::Status SetSocketMutator(FileDescriptor fd, grpc_fd_usage usage,
                              grpc_socket_mutator* mutator) {
  CHECK(mutator);
  if (!grpc_socket_mutator_mutate_fd(mutator, fd.fd(), usage)) {
    return absl::Status(absl::StatusCode::kInternal,
                        "grpc_socket_mutator failed.");
  }
  return absl::OkStatus();
}

absl::Status ApplySocketMutatorInOptions(FileDescriptor fd, grpc_fd_usage usage,
                                         const PosixTcpOptions& options) {
  if (options.socket_mutator == nullptr) {
    return absl::OkStatus();
  }
  return SetSocketMutator(fd, usage, options.socket_mutator);
}

bool PosixSocketWrapper::IsIpv6LoopbackAvailable() {
  static bool kIpv6LoopbackAvailable = []() -> bool {
    int fd = socket(AF_INET6, SOCK_STREAM, 0);
    bool loopback_available = false;
    if (fd < 0) {
      GRPC_TRACE_LOG(tcp, INFO)
          << "Disabling AF_INET6 sockets because socket() failed.";
    } else {
      sockaddr_in6 addr;
      memset(&addr, 0, sizeof(addr));
      addr.sin6_family = AF_INET6;
      addr.sin6_addr.s6_addr[15] = 1;  // [::1]:0
      if (bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == 0) {
        loopback_available = true;
      } else {
        GRPC_TRACE_LOG(tcp, INFO)
            << "Disabling AF_INET6 sockets because ::1 is not available.";
      }
      close(fd);
    }
    return loopback_available;
  }();
  return kIpv6LoopbackAvailable;
}

absl::StatusOr<EventEngine::ResolvedAddress> PosixSocketWrapper::LocalAddress(
    const SystemApi& system_api) {
  EventEngine::ResolvedAddress addr;
  socklen_t len = EventEngine::ResolvedAddress::MAX_SIZE_BYTES;
  if (system_api.GetSockName(fd_, const_cast<sockaddr*>(addr.address()), &len) <
      0) {
    return absl::InternalError(
        absl::StrCat("getsockname:", grpc_core::StrError(errno)));
  }
  return EventEngine::ResolvedAddress(addr.address(), len);
}

absl::StatusOr<EventEngine::ResolvedAddress> PosixSocketWrapper::PeerAddress(
    const SystemApi& system_api) {
  EventEngine::ResolvedAddress addr;
  socklen_t len = EventEngine::ResolvedAddress::MAX_SIZE_BYTES;
  if (system_api.GetPeerName(fd_, const_cast<sockaddr*>(addr.address()), &len) <
      0) {
    return absl::InternalError(
        absl::StrCat("getpeername:", grpc_core::StrError(errno)));
  }
  return EventEngine::ResolvedAddress(addr.address(), len);
}

absl::StatusOr<std::string> PosixSocketWrapper::LocalAddressString(
    const SystemApi& system_api) {
  auto status = LocalAddress(system_api);
  if (!status.ok()) {
    return status.status();
  }
  return ResolvedAddressToNormalizedString((*status));
}

absl::StatusOr<std::string> PosixSocketWrapper::PeerAddressString(
    const SystemApi& system_api) {
  auto status = PeerAddress(system_api);
  if (!status.ok()) {
    return status.status();
  }
  return ResolvedAddressToNormalizedString((*status));
}

absl::StatusOr<PosixSocketWrapper> PosixSocketWrapper::CreateDualStackSocket(
    const SystemApi& posix_apis,
    std::function<FileDescriptor(int, int, int)> socket_factory,
    const experimental::EventEngine::ResolvedAddress& addr, int type,
    int protocol, PosixSocketWrapper::DSMode& dsmode) {
  const sockaddr* sock_addr = addr.address();
  int family = sock_addr->sa_family;
  FileDescriptor newfd;
  if (family == AF_INET6) {
    if (IsIpv6LoopbackAvailable()) {
      newfd = CreateSocket(posix_apis, socket_factory, family, type, protocol);
    } else {
      newfd.invalidate();
      errno = EAFNOSUPPORT;
    }
    // Check if we've got a valid dualstack socket.
    if (newfd.ready() && SetSocketDualStack(posix_apis, newfd)) {
      dsmode = PosixSocketWrapper::DSMode::DSMODE_DUALSTACK;
      return PosixSocketWrapper(newfd);
    }
    // If this isn't an IPv4 address, then return whatever we've got.
    if (!ResolvedAddressIsV4Mapped(addr, nullptr)) {
      if (!newfd.ready()) {
        return ErrorForFd(newfd, addr);
      }
      dsmode = PosixSocketWrapper::DSMode::DSMODE_IPV6;
      return PosixSocketWrapper(newfd);
    }
    // Fall back to AF_INET.
    if (newfd.ready()) {
      posix_apis.Close(newfd);
    }
    family = AF_INET;
  }
  dsmode = family == AF_INET ? PosixSocketWrapper::DSMode::DSMODE_IPV4
                             : PosixSocketWrapper::DSMode::DSMODE_NONE;
  newfd = CreateSocket(posix_apis, socket_factory, family, type, protocol);
  if (!newfd.ready()) {
    return ErrorForFd(newfd, addr);
  }
  return PosixSocketWrapper(newfd);
}

absl::StatusOr<PosixSocketWrapper::PosixSocketCreateResult>
PosixSocketWrapper::CreateAndPrepareTcpClientSocket(
    const SystemApi& posix_apis, const PosixTcpOptions& options,
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
      PosixSocketWrapper::CreateDualStackSocket(
          posix_apis, nullptr, mapped_target_addr, SOCK_STREAM, 0, dsmode);
  if (!posix_socket_wrapper.ok()) {
    return posix_socket_wrapper.status();
  }

  if (dsmode == PosixSocketWrapper::DSMode::DSMODE_IPV4) {
    // Original addr is either v4 or v4 mapped to v6. Set mapped_addr to v4.
    if (!ResolvedAddressIsV4Mapped(target_addr, &mapped_target_addr)) {
      mapped_target_addr = target_addr;
    }
  }

  auto error = PrepareTcpClientSocket(posix_apis, posix_socket_wrapper->Fd(),
                                      mapped_target_addr, options);
  if (!error.ok()) {
    return error;
  }
  return PosixSocketWrapper::PosixSocketCreateResult{posix_socket_wrapper->Fd(),
                                                     mapped_target_addr};
}

#else  // GRPC_POSIX_SOCKET_UTILS_COMMON

bool PosixSocketWrapper::IsIpv6LoopbackAvailable() {
  grpc_core::Crash("unimplemented");
}

absl::StatusOr<PosixSocketWrapper> PosixSocketWrapper::CreateDualStackSocket(
    const SystemApi& /*system_api*/,
    std::function<FileDescriptor(int /*domain*/, int /*type*/,
                                 int /*protocol*/)>
    /* socket_factory */,
    const experimental::EventEngine::ResolvedAddress& /*addr*/, int /*type*/,
    int /*protocol*/, DSMode& /*dsmode*/) {
  grpc_core::Crash("unimplemented");
}

absl::StatusOr<PosixSocketWrapper::PosixSocketCreateResult>
PosixSocketWrapper::CreateAndPrepareTcpClientSocket(
    const SystemApi& /*system_api*/, const PosixTcpOptions& /*options*/,
    const EventEngine::ResolvedAddress& /*target_addr*/) {
  grpc_core::Crash("unimplemented");
}

#endif  // GRPC_POSIX_SOCKET_UTILS_COMMON

}  // namespace experimental
}  // namespace grpc_event_engine
