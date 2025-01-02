//
//
// Copyright 2015 gRPC authors.
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
//
//

#include <grpc/support/port_platform.h>

#include "absl/types/optional.h"
#include "src/core/lib/iomgr/port.h"

#ifdef GRPC_POSIX_SOCKETUTILS
#include <fcntl.h>
#include <grpc/impl/grpc_types.h>
#include <sys/socket.h>
#include <unistd.h>

#include "src/core/lib/iomgr/sockaddr.h"
#include "src/core/lib/iomgr/socket_utils_posix.h"
#include "src/core/util/crash.h"
#endif

#ifdef GRPC_POSIX_SOCKET_TCP

#include "src/core/lib/event_engine/channel_args_endpoint_config.h"
#include "src/core/lib/iomgr/socket_utils_posix.h"
#include "src/core/util/strerror.h"

using ::grpc_event_engine::experimental::EndpointConfig;

using ::grpc_core::PosixTcpOptions;

namespace {

int AdjustValue(int default_value, int min_value, int max_value,
                absl::optional<int> actual_value) {
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
  options.allow_reuse_port =
      (AdjustValue(0, 1, INT_MAX, config.GetInt(GRPC_ARG_ALLOW_REUSEPORT)) !=
       0);
  options.dscp = AdjustValue(PosixTcpOptions::kDscpNotSet, 0, 63,
                             config.GetInt(GRPC_ARG_DSCP));

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

#endif  // GRPC_POSIX_SOCKET_TCP

#ifdef GRPC_POSIX_SOCKETUTILS

int grpc_accept4(int sockfd, grpc_resolved_address* resolved_addr, int nonblock,
                 int cloexec) {
  int fd, flags;
  fd = accept(sockfd, reinterpret_cast<grpc_sockaddr*>(resolved_addr->addr),
              &resolved_addr->len);
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

#endif  // GRPC_POSIX_SOCKETUTILS
