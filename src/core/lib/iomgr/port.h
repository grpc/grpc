/*
 *
 * Copyright 2016, Google Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *     * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 * copyright notice, this list of conditions and the following disclaimer
 * in the documentation and/or other materials provided with the
 * distribution.
 *     * Neither the name of Google Inc. nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

#include <grpc/support/port_platform.h>

#ifndef GRPC_CORE_LIB_IOMGR_PORT_H
#define GRPC_CORE_LIB_IOMGR_PORT_H

#if defined(GRPC_UV)
// Do nothing
#elif defined(GPR_MANYLINUX1)
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
#define GRPC_HAVE_IFADDRS 1
#define GRPC_HAVE_IPV6_RECVPKTINFO 1
#define GRPC_HAVE_IP_PKTINFO 1
#define GRPC_HAVE_MSG_NOSIGNAL 1
#define GRPC_HAVE_UNIX_SOCKET 1
#define GRPC_LINUX_MULTIPOLL_WITH_EPOLL 1
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
#define GRPC_HAVE_IFADDRS 1
#define GRPC_HAVE_SO_NOSIGPIPE 1
#define GRPC_HAVE_UNIX_SOCKET 1
#define GRPC_MSG_IOVLEN_TYPE int
#define GRPC_POSIX_NO_SPECIAL_WAKEUP_FD 1
#define GRPC_POSIX_SOCKET 1
#define GRPC_POSIX_SOCKETADDR 1
#define GRPC_POSIX_SOCKETUTILS 1
#define GRPC_POSIX_WAKEUP_FD 1
#define GRPC_TIMER_USE_GENERIC 1
#elif defined(GPR_FREEBSD)
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

#endif /* GRPC_CORE_LIB_IOMGR_PORT_H */
