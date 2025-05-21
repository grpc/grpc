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

#include <grpc/event_engine/event_engine.h>
#include <grpc/event_engine/memory_allocator.h>
#include <grpc/impl/channel_arg_names.h>
#include <grpc/support/port_platform.h>
#include <limits.h>

#include <optional>

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

namespace grpc_event_engine::experimental {

namespace {

int AdjustValue(int default_value, int min_value, int max_value,
                std::optional<int> actual_value) {
  if (!actual_value.has_value() || *actual_value < min_value ||
      *actual_value > max_value) {
    return default_value;
  }
  return *actual_value;
}

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
  options.allow_reuse_port = IsSocketReusePortSupported();
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

bool IsIpv6LoopbackAvailable() {
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

#else  // GRPC_POSIX_SOCKET_UTILS_COMMON

bool IsIpv6LoopbackAvailable() { grpc_core::Crash("unimplemented"); }

#endif  // GRPC_POSIX_SOCKET_UTILS_COMMON

}  // namespace grpc_event_engine::experimental
