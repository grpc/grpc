/*
 *
 * Copyright 2021 gRPC authors.
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
#ifndef GRPC_CORE_LIB_EVENT_ENGINE_PORT_H
#define GRPC_CORE_LIB_EVENT_ENGINE_PORT_H

#include <grpc/support/port_platform.h>

// TODO(hork): Extract required macros from iomgr's port.h and sockaddr_*.h.

#ifdef GRPC_UV
#include <uv.h>
#elif defined(GPR_ANDROID) || defined(GPR_LINUX) || defined(GPR_APPLE) ||   \
    defined(GPR_FREEBSD) || defined(GPR_OPENBSD) || defined(GPR_SOLARIS) || \
    defined(GPR_AIX) || defined(GPR_NACL) || defined(GPR_FUCHSIA) ||        \
    defined(GRPC_POSIX_SOCKET)
#include <sys/socket.h>
#elif defined(GPR_WINDOWS)
#include <winsock2.h>
#else
#error UNKNOWN PLATFORM
#endif

// TODO(hork): should live elsewhere. They are not part of the public API.
typedef struct sockaddr grpc_sockaddr;
typedef struct sockaddr_in grpc_sockaddr_in;
typedef struct sockaddr_in6 grpc_sockaddr_in6;

#endif  // GRPC_CORE_LIB_EVENT_ENGINE_PORT_H
