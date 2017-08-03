/*
 *
 * Copyright 2016 gRPC authors.
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

#include <grpc/support/port_platform.h>

#ifndef GRPC_CORE_LIB_IOMGR_PORT_H
#define GRPC_CORE_LIB_IOMGR_PORT_H

#if defined(GRPC_UV)
// Do nothing
#elif defined(GPR_MANYLINUX1)
#define GRPC_HAVE_ARPA_NAMESER 1
#define GRPC_HAVE_IFADDRS 1
#define GRPC_HAVE_IPV6_RECVPKTINFO 1
#define GRPC_HAVE_IP_PKTINFO 1
#define GRPC_HAVE_MSG_NOSIGNAL 1
#define GRPC_HAVE_UNIX_SOCKET 1
#define GRPC_POSIX_NO_SPECIAL_WAKEUP_FD 1
#define GRPC_POSIX_SOCKET 1
#define GRPC_POSIX_SOCKETADDR 1
#define GRPC_POSIX_SOCKETUTILS 1
#define GRPC_POSIX_WAKEUP_FD 1
#define GRPC_TIMER_USE_GENERIC 1
#elif defined(GPR_WINDOWS)
#define GRPC_TIMER_USE_GENERIC 1
#define GRPC_WINSOCK_SOCKET 1
#define GRPC_WINDOWS_SOCKETUTILS 1
#elif defined(GPR_ANDROID)
#define GRPC_HAVE_IPV6_RECVPKTINFO 1
#define GRPC_HAVE_IP_PKTINFO 1
#define GRPC_HAVE_MSG_NOSIGNAL 1
#define GRPC_HAVE_UNIX_SOCKET 1
#define GRPC_LINUX_EVENTFD 1
#define GRPC_POSIX_SOCKET 1
#define GRPC_POSIX_SOCKETADDR 1
#define GRPC_POSIX_SOCKETUTILS 1
#define GRPC_POSIX_WAKEUP_FD 1
#define GRPC_TIMER_USE_GENERIC 1
#elif defined(GPR_LINUX)
#define GRPC_HAVE_ARPA_NAMESER 1
#define GRPC_HAVE_IFADDRS 1
#define GRPC_HAVE_IPV6_RECVPKTINFO 1
#define GRPC_HAVE_IP_PKTINFO 1
#define GRPC_HAVE_MSG_NOSIGNAL 1
#define GRPC_HAVE_UNIX_SOCKET 1
#define GRPC_LINUX_MULTIPOLL_WITH_EPOLL 1
#define GRPC_POSIX_HOST_NAME_MAX 1
#define GRPC_POSIX_SOCKET 1
#define GRPC_POSIX_SOCKETADDR 1
#define GRPC_POSIX_WAKEUP_FD 1
#define GRPC_TIMER_USE_GENERIC 1
#ifdef __GLIBC_PREREQ
#if __GLIBC_PREREQ(2, 9)
#define GRPC_LINUX_EPOLL 1
#define GRPC_LINUX_EVENTFD 1
#endif
#if __GLIBC_PREREQ(2, 10)
#define GRPC_LINUX_SOCKETUTILS 1
#endif
#endif
#ifndef __GLIBC__
#define GRPC_LINUX_EPOLL 1
#define GRPC_LINUX_EVENTFD 1
#define GRPC_MSG_IOVLEN_TYPE int
#endif
#ifndef GRPC_LINUX_EVENTFD
#define GRPC_POSIX_NO_SPECIAL_WAKEUP_FD 1
#endif
#ifndef GRPC_LINUX_SOCKETUTILS
#define GRPC_POSIX_SOCKETUTILS
#endif
#elif defined(GPR_APPLE)
#define GRPC_HAVE_ARPA_NAMESER 1
#define GRPC_HAVE_IFADDRS 1
#define GRPC_HAVE_SO_NOSIGPIPE 1
#define GRPC_HAVE_UNIX_SOCKET 1
#define GRPC_MSG_IOVLEN_TYPE int
#define GRPC_POSIX_NO_SPECIAL_WAKEUP_FD 1
#define GRPC_POSIX_SOCKET 1
#define GRPC_POSIX_SOCKETADDR 1
#define GRPC_POSIX_SOCKETUTILS 1
#define GRPC_POSIX_SYSCONF 1
#define GRPC_POSIX_WAKEUP_FD 1
#define GRPC_TIMER_USE_GENERIC 1
#elif defined(GPR_FREEBSD)
#define GRPC_HAVE_ARPA_NAMESER 1
#define GRPC_HAVE_IFADDRS 1
#define GRPC_HAVE_IPV6_RECVPKTINFO 1
#define GRPC_HAVE_SO_NOSIGPIPE 1
#define GRPC_HAVE_UNIX_SOCKET 1
#define GRPC_POSIX_NO_SPECIAL_WAKEUP_FD 1
#define GRPC_POSIX_SOCKET 1
#define GRPC_POSIX_SOCKETADDR 1
#define GRPC_POSIX_SOCKETUTILS 1
#define GRPC_POSIX_WAKEUP_FD 1
#define GRPC_TIMER_USE_GENERIC 1
#elif defined(GPR_NACL)
#define GRPC_HAVE_ARPA_NAMESER 1
#define GRPC_POSIX_NO_SPECIAL_WAKEUP_FD 1
#define GRPC_POSIX_SOCKET 1
#define GRPC_POSIX_SOCKETADDR 1
#define GRPC_POSIX_SOCKETUTILS 1
#define GRPC_POSIX_WAKEUP_FD 1
#define GRPC_TIMER_USE_GENERIC 1
#elif !defined(GPR_NO_AUTODETECT_PLATFORM)
#error "Platform not recognized"
#endif

#if defined(GRPC_POSIX_SOCKET) + defined(GRPC_WINSOCK_SOCKET) + \
        defined(GRPC_CUSTOM_SOCKET) + defined(GRPC_UV) !=       \
    1
#error Must define exactly one of GRPC_POSIX_SOCKET, GRPC_WINSOCK_SOCKET, GPR_CUSTOM_SOCKET
#endif

#if defined(GRPC_POSIX_HOST_NAME_MAX) && defined(GRPC_POSIX_SYSCONF)
#error "Cannot define both GRPC_POSIX_HOST_NAME_MAX and GRPC_POSIX_SYSCONF"
#endif
#if !defined(GRPC_POSIX_HOST_NAME_MAX) && !defined(GRPC_POSIX_SYSCONF)
#define GRPC_GETHOSTNAME_FALLBACK 1
#endif

#endif /* GRPC_CORE_LIB_IOMGR_PORT_H */
