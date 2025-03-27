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

#include <errno.h>
#include <grpc/event_engine/event_engine.h>
#include <grpc/event_engine/memory_allocator.h>
#include <grpc/impl/channel_arg_names.h>
#include <grpc/support/port_platform.h>
#include <limits.h>

#include "src/core/lib/event_engine/posix_engine/tcp_socket_utils.h"
#include "src/core/lib/iomgr/port.h"
#include "src/core/util/crash.h"  // IWYU pragma: keep

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

#ifdef GRPC_POSIX_SOCKET_UTILS_COMMON
#ifndef GRPC_SET_SOCKET_DUALSTACK_CUSTOM

bool SetSocketDualStack(int fd) {
  const int off = 0;
  return 0 == setsockopt(fd, IPPROTO_IPV6, IPV6_V6ONLY, &off, sizeof(off));
}

#endif  // GRPC_SET_SOCKET_DUALSTACK_CUSTOM
#endif  // GRPC_POSIX_SOCKET_UTILS_COMMON

}  // namespace grpc_event_engine::experimental
