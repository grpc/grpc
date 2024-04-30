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

#include "src/core/lib/iomgr/port.h"

#ifdef GRPC_POSIX_SOCKET_UTILS_COMMON

#include "src/core/lib/iomgr/socket_utils.h"
// sys/types.h must precede netinet/tcp.h for compatibility.
#include <sys/types.h>
#ifdef GRPC_LINUX_TCP_H
#include <linux/tcp.h>
#else
#include <netinet/tcp.h>
#endif
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include <string>

#include "absl/log/check.h"

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/sync.h>

#include "src/core/lib/gprpp/crash.h"
#include "src/core/lib/iomgr/sockaddr.h"

uint16_t grpc_htons(uint16_t hostshort) { return htons(hostshort); }

uint16_t grpc_ntohs(uint16_t netshort) { return ntohs(netshort); }

uint32_t grpc_htonl(uint32_t hostlong) { return htonl(hostlong); }

uint32_t grpc_ntohl(uint32_t netlong) { return ntohl(netlong); }

int grpc_inet_pton(int af, const char* src, void* dst) {
  return inet_pton(af, src, dst);
}

const char* grpc_inet_ntop(int af, const void* src, char* dst, size_t size) {
  CHECK(size <= (socklen_t)-1);
  return inet_ntop(af, src, dst, static_cast<socklen_t>(size));
}

#endif
