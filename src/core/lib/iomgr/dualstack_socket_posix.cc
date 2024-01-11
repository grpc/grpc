//
//
// Copyright 2020 gRPC authors.
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

#ifdef GRPC_POSIX_SOCKET_UTILS_COMMON

#include <netinet/in.h>

#include "src/core/lib/iomgr/socket_utils_posix.h"

#ifndef GRPC_SET_SOCKET_DUALSTACK_CUSTOM

// This should be 0 in production, but it may be enabled for testing or
// debugging purposes, to simulate an environment where IPv6 sockets can't
// also speak IPv4.
int grpc_forbid_dualstack_sockets_for_testing = 0;

int grpc_set_socket_dualstack(int fd) {
  if (!grpc_forbid_dualstack_sockets_for_testing) {
    const int off = 0;
    return 0 == setsockopt(fd, IPPROTO_IPV6, IPV6_V6ONLY, &off, sizeof(off));
  } else {
    // Force an IPv6-only socket, for testing purposes.
    const int on = 1;
    setsockopt(fd, IPPROTO_IPV6, IPV6_V6ONLY, &on, sizeof(on));
    return 0;
  }
}
#endif  // GRPC_SET_SOCKET_DUALSTACK_CUSTOM
#endif  // GRPC_POSIX_SOCKET_UTILS_COMMON
