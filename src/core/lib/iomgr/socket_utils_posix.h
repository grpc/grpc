/*
 *
 * Copyright 2015, Google Inc.
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

#ifndef GRPC_CORE_LIB_IOMGR_SOCKET_UTILS_POSIX_H
#define GRPC_CORE_LIB_IOMGR_SOCKET_UTILS_POSIX_H

#include <sys/socket.h>
#include <unistd.h>

/* a wrapper for accept or accept4 */
int grpc_accept4(int sockfd, struct sockaddr *addr, socklen_t *addrlen,
                 int nonblock, int cloexec);

/* set a socket to non blocking mode */
int grpc_set_socket_nonblocking(int fd, int non_blocking);

/* set a socket to close on exec */
int grpc_set_socket_cloexec(int fd, int close_on_exec);

/* set a socket to reuse old addresses */
int grpc_set_socket_reuse_addr(int fd, int reuse);

/* disable nagle */
int grpc_set_socket_low_latency(int fd, int low_latency);

/* Returns true if this system can create AF_INET6 sockets bound to ::1.
   The value is probed once, and cached for the life of the process.

   This is more restrictive than checking for socket(AF_INET6) to succeed,
   because Linux with "net.ipv6.conf.all.disable_ipv6 = 1" is able to create
   and bind IPv6 sockets, but cannot connect to a getsockname() of [::]:port
   without a valid loopback interface.  Rather than expose this half-broken
   state to library users, we turn off IPv6 sockets. */
int grpc_ipv6_loopback_available(void);

/* Tries to set SO_NOSIGPIPE if available on this platform.
   Returns 1 on success, 0 on failure.
   If SO_NO_SIGPIPE is not available, returns 1. */
int grpc_set_socket_no_sigpipe_if_possible(int fd);

/* Tries to set IP_PKTINFO if available on this platform.
   Returns 1 on success, 0 on failure.
   If IP_PKTINFO is not available, returns 1. */
int grpc_set_socket_ip_pktinfo_if_possible(int fd);

/* Tries to set IPV6_RECVPKTINFO if available on this platform.
   Returns 1 on success, 0 on failure.
   If IPV6_RECVPKTINFO is not available, returns 1. */
int grpc_set_socket_ipv6_recvpktinfo_if_possible(int fd);

/* Tries to set the socket's send buffer to given size.
   Returns 1 on success, 0 on failure. */
int grpc_set_socket_sndbuf(int fd, int buffer_size_bytes);

/* Tries to set the socket's receive buffer to given size.
   Returns 1 on success, 0 on failure. */
int grpc_set_socket_rcvbuf(int fd, int buffer_size_bytes);

/* An enum to keep track of IPv4/IPv6 socket modes.

   Currently, this information is only used when a socket is first created, but
   in the future we may wish to store it alongside the fd.  This would let calls
   like sendto() know which family to use without asking the kernel first. */
typedef enum grpc_dualstack_mode {
  /* Uninitialized, or a non-IP socket. */
  GRPC_DSMODE_NONE,
  /* AF_INET only. */
  GRPC_DSMODE_IPV4,
  /* AF_INET6 only, because IPV6_V6ONLY could not be cleared. */
  GRPC_DSMODE_IPV6,
  /* AF_INET6, which also supports ::ffff-mapped IPv4 addresses. */
  GRPC_DSMODE_DUALSTACK
} grpc_dualstack_mode;

/* Only tests should use this flag. */
extern int grpc_forbid_dualstack_sockets_for_testing;

/* Creates a new socket for connecting to (or listening on) an address.

   If addr is AF_INET6, this creates an IPv6 socket first.  If that fails,
   and addr is within ::ffff:0.0.0.0/96, then it automatically falls back to
   an IPv4 socket.

   If addr is AF_INET, AF_UNIX, or anything else, then this is similar to
   calling socket() directly.

   Returns an fd on success, otherwise returns -1 with errno set to the result
   of a failed socket() call.

   The *dsmode output indicates which address family was actually created.
   The recommended way to use this is:
   - First convert to IPv6 using grpc_sockaddr_to_v4mapped().
   - Create the socket.
   - If *dsmode is IPV4, use grpc_sockaddr_is_v4mapped() to convert back to
     IPv4, so that bind() or connect() see the correct family.
   Also, it's important to distinguish between DUALSTACK and IPV6 when
   listening on the [::] wildcard address. */
int grpc_create_dualstack_socket(const struct sockaddr *addr, int type,
                                 int protocol, grpc_dualstack_mode *dsmode);

#endif /* GRPC_CORE_LIB_IOMGR_SOCKET_UTILS_POSIX_H */
