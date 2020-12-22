/*
 *
 * Copyright 2015 gRPC authors.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

#include <stdio.h>
#include <string.h>

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>

#include "test/cpp/naming/dns_test_util.h"

#ifdef GPR_WINDOWS
#include "src/core/lib/iomgr/sockaddr_windows.h"
#include "src/core/lib/iomgr/socket_windows.h"
#define BAD_SOCKET_RETURN_VAL INVALID_SOCKET
#else
#include "src/core/lib/iomgr/sockaddr_posix.h"
#define BAD_SOCKET_RETURN_VAL (-1)
#endif

namespace grpc {
namespace testing {

FakeNonResponsiveDNSServer::FakeNonResponsiveDNSServer(int port) {
  udp_socket_ = socket(AF_INET6, SOCK_DGRAM, 0);
  tcp_socket_ = socket(AF_INET6, SOCK_STREAM, 0);
  if (udp_socket_ == BAD_SOCKET_RETURN_VAL) {
    gpr_log(GPR_DEBUG, "Failed to create UDP ipv6 socket");
    abort();
  }
  if (tcp_socket_ == BAD_SOCKET_RETURN_VAL) {
    gpr_log(GPR_DEBUG, "Failed to create TCP ipv6 socket");
    abort();
  }
  sockaddr_in6 addr;
  memset(&addr, 0, sizeof(addr));
  addr.sin6_family = AF_INET6;
  addr.sin6_port = htons(port);
  (reinterpret_cast<char*>(&addr.sin6_addr))[15] = 1;
  if (bind(udp_socket_, reinterpret_cast<const sockaddr*>(&addr),
           sizeof(addr)) != 0) {
    gpr_log(GPR_DEBUG, "Failed to bind UDP ipv6 socket to [::1]:%d", port);
    abort();
  }
#ifdef GPR_WINDOWS
  char val = 1;
  if (setsockopt(tcp_socket_, SOL_SOCKET, SO_REUSEADDR, &val, sizeof(val)) ==
      SOCKET_ERROR) {
    gpr_log(GPR_DEBUG,
            "Failed to set SO_REUSEADDR on TCP ipv6 socket to [::1]:%d", port);
    abort();
  }
#else
  int val = 1;
  if (setsockopt(tcp_socket_, SOL_SOCKET, SO_REUSEADDR, &val, sizeof(val)) !=
      0) {
    gpr_log(GPR_DEBUG,
            "Failed to set SO_REUSEADDR on TCP ipv6 socket to [::1]:%d", port);
    abort();
  }
#endif
  if (bind(tcp_socket_, reinterpret_cast<const sockaddr*>(&addr),
           sizeof(addr)) != 0) {
    gpr_log(GPR_DEBUG, "Failed to bind TCP ipv6 socket to [::1]:%d", port);
    abort();
  }
  if (listen(tcp_socket_, 100)) {
    gpr_log(GPR_DEBUG, "Failed to listen on TCP ipv6 socket to [::1]:%d", port);
    abort();
  }
}

FakeNonResponsiveDNSServer::~FakeNonResponsiveDNSServer() {
#ifdef GPR_WINDOWS
  closesocket(udp_socket_);
  closesocket(tcp_socket_);
#else
  close(udp_socket_);
  close(tcp_socket_);
#endif
}

}  // namespace testing
}  // namespace grpc
