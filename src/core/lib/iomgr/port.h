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

#if defined(GPR_WINDOWS)
#define GPR_WINSOCK_SOCKET 1
#define GPR_WINDOWS_SOCKETUTILS 1
/* #undef GPR_POSIX_SOCKET */
/* #undef GPR_POSIX_WAKEUP_FD */
#elif defined(GPR_MANYLINUX1)
#define GPR_HAVE_IPV6_RECVPKTINFO 1
#define GPR_HAVE_IP_PKTINFO 1
#define GPR_HAVE_MSG_NOSIGNAL 1
#define GPR_HAVE_UNIX_SOCKET 1
#define GPR_POSIX_NO_SPECIAL_WAKEUP_FD 1
#define GPR_POSIX_SOCKET 1
#define GPR_POSIX_SOCKETADDR 1
#define GPR_POSIX_SOCKETUTILS 1
#define GPR_POSIX_WAKEUP_FD 1
#elif defined(GPR_ANDROID)
#define GPR_HAVE_IPV6_RECVPKTINFO 1
#define GPR_HAVE_IP_PKTINFO 1
#define GPR_HAVE_MSG_NOSIGNAL 1
#define GPR_HAVE_UNIX_SOCKET 1
#define GPR_LINUX_EVENTFD 1
#define GPR_POSIX_SOCKET 1
#define GPR_POSIX_SOCKETADDR 1
#define GPR_POSIX_SOCKETUTILS 1
#define GPR_POSIX_WAKEUP_FD 1
#elif defined(GPR_LINUX)
#define GPR_HAVE_IPV6_RECVPKTINFO 1
#define GPR_HAVE_IP_PKTINFO 1
#define GPR_HAVE_MSG_NOSIGNAL 1
#define GPR_HAVE_UNIX_SOCKET 1
#define GPR_LINUX_MULTIPOLL_WITH_EPOLL 1
#define GPR_POSIX_SOCKET 1
#define GPR_POSIX_SOCKETADDR 1
#define GPR_POSIX_WAKEUP_FD 1
#ifdef __GLIBC_PREREQ
#if __GLIBC_PREREQ(2, 9)
#define GPR_LINUX_EPOLL 1
#define GPR_LINUX_EVENTFD 1
#endif
#if __GLIBC_PREREQ(2, 10)
#define GPR_LINUX_SOCKETUTILS 1
#endif
#endif
#ifndef GPR_LINUX_EVENTFD
#define GPR_POSIX_NO_SPECIAL_WAKEUP_FD 1
#endif
#ifndef GPR_LINUX_SOCKETUTILS
#define GPR_POSIX_SOCKETUTILS
#endif
#elif defined(GPR_APPLE)
#define GPR_HAVE_IP_PKTINFO 1
#define GPR_HAVE_SO_NOSIGPIPE 1
#define GPR_HAVE_UNIX_SOCKET 1
#define GPR_MSG_IOVLEN_TYPE int
#define GPR_POSIX_NO_SPECIAL_WAKEUP_FD 1
#define GPR_POSIX_SOCKET 1
#define GPR_POSIX_SOCKETADDR 1
#define GPR_POSIX_SOCKETUTILS 1
#define GPR_POSIX_WAKEUP_FD 1
#elif defined(GPR_FREEBSD)
#define GPR_HAVE_IPV6_RECVPKTINFO 1
#define GPR_HAVE_IP_PKTINFO 1
#define GPR_HAVE_SO_NOSIGPIPE 1
#define GPR_HAVE_UNIX_SOCKET 1
#define GPR_POSIX_NO_SPECIAL_WAKEUP_FD 1
#define GPR_POSIX_SOCKET 1
#define GPR_POSIX_SOCKETADDR 1
#define GPR_POSIX_SOCKETUTILS 1
#define GPR_POSIX_WAKEUP_FD 1
#elif defined(GPR_NACL)
#define GPR_POSIX_NO_SPECIAL_WAKEUP_FD 1
#define GPR_POSIX_SOCKET 1
#define GPR_POSIX_SOCKETADDR 1
#define GPR_POSIX_SOCKETUTILS 1
#define GPR_POSIX_WAKEUP_FD 1
#elif !defined(GPR_NO_AUTODETECT_PLATFORM)
#error "Platform not recognized"
#endif

#if defined(GPR_POSIX_SOCKET) + defined(GPR_WINSOCK_SOCKET) + \
        defined(GPR_CUSTOM_SOCKET) !=                         \
    1
#error Must define exactly one of GPR_POSIX_SOCKET, GPR_WINSOCK_SOCKET, GPR_CUSTOM_SOCKET
#endif

#endif  /* GRPC_CORE_LIB_IOMGR_PORT_H */
