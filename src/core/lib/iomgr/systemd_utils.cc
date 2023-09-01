//
//
// Copyright 2022 gRPC authors.
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

#include "src/core/lib/iomgr/port.h"

#ifdef GRPC_POSIX_SOCKET_TCP_SERVER_UTILS_COMMON

#ifdef HAVE_LIBSYSTEMD
#include <systemd/sd-daemon.h>
#endif

#include "src/core/lib/address_utils/sockaddr_utils.h"
#include "src/core/lib/iomgr/resolve_address.h"
#include "src/core/lib/iomgr/sockaddr.h"
#include "src/core/lib/iomgr/systemd_utils.h"

#ifdef HAVE_LIBSYSTEMD
bool set_matching_sd_unix_fd(grpc_tcp_server* s,
                             const grpc_resolved_address* addr,
                             const int fd_start, const int n) {
  absl::StatusOr<std::string> addr_name = grpc_sockaddr_to_string(addr, true);
  for (int i = fd_start; i < fd_start + n; i++) {
    if (sd_is_socket_unix(i, SOCK_STREAM, 1, addr_name.value().c_str(), 0)) {
      grpc_tcp_server_set_pre_allocated_fd(s, i);
      return true;
    }
  }
  return false;
}

bool set_matching_sd_inet_fd(grpc_tcp_server* s,
                             const grpc_resolved_address* addr,
                             const int family, const int port,
                             const int fd_start, const int n) {
  for (int i = fd_start; i < fd_start + n; i++) {
    int r_inet = sd_is_socket_inet(i, family, SOCK_STREAM, 1, (uint16_t)port);
    int r_addr = sd_is_socket_sockaddr(
        i, SOCK_STREAM,
        reinterpret_cast<grpc_sockaddr*>(const_cast<char*>(addr->addr)),
        addr->len, 1);

    if (r_inet > 0 && r_addr > 0) {
      grpc_tcp_server_set_pre_allocated_fd(s, i);
      return true;
    }
  }
  return false;
}

void set_matching_sd_fds(grpc_tcp_server* s, const grpc_resolved_address* addr,
                         int requested_port) {
  int n = sd_listen_fds(0);
  if (n <= 0) {
    return;
  }

  int fd_start = SD_LISTEN_FDS_START;
  grpc_resolved_address addr6_v4mapped;

  if (grpc_sockaddr_to_v4mapped(addr, &addr6_v4mapped)) {
    addr = &addr6_v4mapped;
  }

  int family = grpc_sockaddr_get_family(addr);
  int port = grpc_sockaddr_get_port(addr);

  if (family == AF_UNIX) {
    set_matching_sd_unix_fd(s, addr, fd_start, n);
  } else {
    if (grpc_sockaddr_is_wildcard(addr, &requested_port)) {
      grpc_resolved_address wild4;
      grpc_resolved_address wild6;
      grpc_resolved_address wildcard_addrs[2];

      grpc_sockaddr_make_wildcards(requested_port, &wild4, &wild6);
      wildcard_addrs[0] = wild4;
      wildcard_addrs[1] = wild6;

      for (grpc_resolved_address addr_w : wildcard_addrs) {
        int family_w = grpc_sockaddr_get_family(&addr_w);
        int port_w = grpc_sockaddr_get_port(&addr_w);
        if (set_matching_sd_inet_fd(s, &addr_w, family_w, port_w, fd_start,
                                    n)) {
          return;
        }
      }
      return;
    }

    set_matching_sd_inet_fd(s, addr, family, port, fd_start, n);
  }
}
#else
void set_matching_sd_fds(GRPC_UNUSED grpc_tcp_server* s,
                         GRPC_UNUSED const grpc_resolved_address* addr,
                         GRPC_UNUSED int requested_port) {}
#endif  // HAVE_LIBSYSTEMD

#endif  // GRPC_POSIX_SOCKET_TCP_SERVER_UTILS_COMMON
