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
#ifndef LOCAL_GOOGLE_HOME_HORK_PROJ_GRPC_SRC_CORE_LIB_EVENT_ENGINE_PORT_H_
#define LOCAL_GOOGLE_HOME_HORK_PROJ_GRPC_SRC_CORE_LIB_EVENT_ENGINE_PORT_H_

#include <grpc/support/port_platform.h>

// TODO(hork): Extract required macros from iomgr's port.h and sockaddr_*.h.
// TODO(hork): These are EventEngine implementation-specific. Move these to our
// impl. They are here for reference at the moment.

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

typedef struct sockaddr grpc_sockaddr;
typedef struct sockaddr_in grpc_sockaddr_in;
typedef struct sockaddr_in6 grpc_sockaddr_in6;

#endif  // LOCAL_GOOGLE_HOME_HORK_PROJ_GRPC_SRC_CORE_LIB_EVENT_ENGINE_PORT_H_
