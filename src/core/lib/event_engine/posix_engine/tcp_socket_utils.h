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

#ifndef GRPC_SRC_CORE_LIB_EVENT_ENGINE_POSIX_ENGINE_TCP_SOCKET_UTILS_H
#define GRPC_SRC_CORE_LIB_EVENT_ENGINE_POSIX_ENGINE_TCP_SOCKET_UTILS_H

#include <grpc/event_engine/endpoint_config.h>
#include <grpc/event_engine/event_engine.h>
#include <grpc/event_engine/memory_allocator.h>
#include <grpc/grpc.h>
#include <grpc/support/port_platform.h>

#include <utility>

#include "src/core/lib/iomgr/port.h"
#include "src/core/lib/iomgr/socket_mutator.h"
#include "src/core/lib/resource_quota/resource_quota.h"
#include "src/core/util/ref_counted_ptr.h"

#ifdef GRPC_POSIX_SOCKET_UTILS_COMMON
#include <sys/socket.h>
#endif

#ifdef GRPC_LINUX_ERRQUEUE
#ifndef SO_ZEROCOPY
#define SO_ZEROCOPY 60
#endif
#ifndef SO_EE_ORIGIN_ZEROCOPY
#define SO_EE_ORIGIN_ZEROCOPY 5
#endif
#endif  // ifdef GRPC_LINUX_ERRQUEUE

namespace grpc_event_engine::experimental {

struct PosixTcpOptions {
  static constexpr int kDefaultReadChunkSize = 8192;
  static constexpr int kDefaultMinReadChunksize = 256;
  static constexpr int kDefaultMaxReadChunksize = 4 * 1024 * 1024;
  static constexpr int kZerocpTxEnabledDefault = 0;
  static constexpr int kMaxChunkSize = 32 * 1024 * 1024;
  static constexpr int kDefaultMaxSends = 4;
  static constexpr size_t kDefaultSendBytesThreshold = 16 * 1024;
  // Let the system decide the proper buffer size.
  static constexpr int kReadBufferSizeUnset = -1;
  static constexpr int kDscpNotSet = -1;
  int tcp_read_chunk_size = kDefaultReadChunkSize;
  int tcp_min_read_chunk_size = kDefaultMinReadChunksize;
  int tcp_max_read_chunk_size = kDefaultMaxReadChunksize;
  int tcp_tx_zerocopy_send_bytes_threshold = kDefaultSendBytesThreshold;
  int tcp_tx_zerocopy_max_simultaneous_sends = kDefaultMaxSends;
  int tcp_receive_buffer_size = kReadBufferSizeUnset;
  bool tcp_tx_zero_copy_enabled = kZerocpTxEnabledDefault;
  int keep_alive_time_ms = 0;
  int keep_alive_timeout_ms = 0;
  bool expand_wildcard_addrs = false;
  bool allow_reuse_port = false;
  int dscp = kDscpNotSet;
  grpc_core::RefCountedPtr<grpc_core::ResourceQuota> resource_quota;
  struct grpc_socket_mutator* socket_mutator = nullptr;
  grpc_event_engine::experimental::MemoryAllocatorFactory*
      memory_allocator_factory = nullptr;
  PosixTcpOptions() = default;
  // Move ctor
  PosixTcpOptions(PosixTcpOptions&& other) noexcept {
    socket_mutator = std::exchange(other.socket_mutator, nullptr);
    resource_quota = std::move(other.resource_quota);
    CopyIntegerOptions(other);
  }
  // Move assignment
  PosixTcpOptions& operator=(PosixTcpOptions&& other) noexcept {
    if (socket_mutator != nullptr) {
      grpc_socket_mutator_unref(socket_mutator);
    }
    socket_mutator = std::exchange(other.socket_mutator, nullptr);
    resource_quota = std::move(other.resource_quota);
    memory_allocator_factory =
        std::exchange(other.memory_allocator_factory, nullptr);
    CopyIntegerOptions(other);
    return *this;
  }
  // Copy ctor
  PosixTcpOptions(const PosixTcpOptions& other) {
    if (other.socket_mutator != nullptr) {
      socket_mutator = grpc_socket_mutator_ref(other.socket_mutator);
    }
    resource_quota = other.resource_quota;
    memory_allocator_factory = other.memory_allocator_factory;
    CopyIntegerOptions(other);
  }
  // Copy assignment
  PosixTcpOptions& operator=(const PosixTcpOptions& other) {
    if (&other == this) {
      return *this;
    }
    if (socket_mutator != nullptr) {
      grpc_socket_mutator_unref(socket_mutator);
      socket_mutator = nullptr;
    }
    if (other.socket_mutator != nullptr) {
      socket_mutator = grpc_socket_mutator_ref(other.socket_mutator);
    }
    resource_quota = other.resource_quota;
    memory_allocator_factory = other.memory_allocator_factory;
    CopyIntegerOptions(other);
    return *this;
  }
  // Destructor.
  ~PosixTcpOptions() {
    if (socket_mutator != nullptr) {
      grpc_socket_mutator_unref(socket_mutator);
    }
  }

 private:
  void CopyIntegerOptions(const PosixTcpOptions& other) {
    tcp_read_chunk_size = other.tcp_read_chunk_size;
    tcp_min_read_chunk_size = other.tcp_min_read_chunk_size;
    tcp_max_read_chunk_size = other.tcp_max_read_chunk_size;
    tcp_tx_zerocopy_send_bytes_threshold =
        other.tcp_tx_zerocopy_send_bytes_threshold;
    tcp_tx_zerocopy_max_simultaneous_sends =
        other.tcp_tx_zerocopy_max_simultaneous_sends;
    tcp_tx_zero_copy_enabled = other.tcp_tx_zero_copy_enabled;
    keep_alive_time_ms = other.keep_alive_time_ms;
    keep_alive_timeout_ms = other.keep_alive_timeout_ms;
    expand_wildcard_addrs = other.expand_wildcard_addrs;
    allow_reuse_port = other.allow_reuse_port;
    dscp = other.dscp;
  }
};

PosixTcpOptions TcpOptionsFromEndpointConfig(
    const grpc_event_engine::experimental::EndpointConfig& config);

// Unlink the path pointed to by the given address if it refers to UDS path.
void UnlinkIfUnixDomainSocket(
    const EventEngine::ResolvedAddress& resolved_addr);

// Returns true if this system can create AF_INET6 sockets bound to ::1.
// The value is probed once, and cached for the life of the process.

// This is more restrictive than checking for socket(AF_INET6) to succeed,
// because Linux with "net.ipv6.conf.all.disable_ipv6 = 1" is able to create
// and bind IPv6 sockets, but cannot connect to a getsockname() of [::]:port
// without a valid loopback interface.  Rather than expose this half-broken
// state to library users, we turn off IPv6 sockets.
bool IsIpv6LoopbackAvailable();

// Return true if SO_REUSEPORT is supported
bool IsSocketReusePortSupported();

bool SetSocketDualStack(int fd);

}  // namespace grpc_event_engine::experimental

#endif  // GRPC_SRC_CORE_LIB_EVENT_ENGINE_POSIX_ENGINE_TCP_SOCKET_UTILS_H
