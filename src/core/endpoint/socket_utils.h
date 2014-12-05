/*
 *
 * Copyright 2014, Google Inc.
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

#ifndef __GRPC_INTERNAL_ENDPOINT_SOCKET_UTILS_H__
#define __GRPC_INTERNAL_ENDPOINT_SOCKET_UTILS_H__

#include <unistd.h>
#include <sys/socket.h>

struct sockaddr;
struct sockaddr_in;
struct sockaddr_in6;

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

/* Returns true if addr is an IPv4-mapped IPv6 address within the
   ::ffff:0.0.0.0/96 range, or false otherwise.

   If addr4_out is non-NULL, the inner IPv4 address will be copied here when
   returning true. */
int grpc_sockaddr_is_v4mapped(const struct sockaddr *addr,
                              struct sockaddr_in *addr4_out);

/* If addr is an AF_INET address, writes the corresponding ::ffff:0.0.0.0/96
   address to addr6_out and returns true.  Otherwise returns false. */
int grpc_sockaddr_to_v4mapped(const struct sockaddr *addr,
                              struct sockaddr_in6 *addr6_out);

/* If addr is ::, 0.0.0.0, or ::ffff:0.0.0.0, writes the port number to
   *port_out (if not NULL) and returns true, otherwise returns false. */
int grpc_sockaddr_is_wildcard(const struct sockaddr *addr, int *port_out);

/* Writes 0.0.0.0:port and [::]:port to separate sockaddrs. */
void grpc_sockaddr_make_wildcards(int port, struct sockaddr_in *wild4_out,
                                  struct sockaddr_in6 *wild6_out);

/* Converts a sockaddr into a newly-allocated human-readable string.

   Currently, only the AF_INET and AF_INET6 families are recognized.
   If the normalize flag is enabled, ::ffff:0.0.0.0/96 IPv6 addresses are
   displayed as plain IPv4.

   Usage is similar to gpr_asprintf: returns the number of bytes written
   (excluding the final '\0'), and *out points to a string which must later be
   destroyed using gpr_free().

   In the unlikely event of an error, returns -1 and sets *out to NULL.
   The existing value of errno is always preserved. */
int grpc_sockaddr_to_string(char **out, const struct sockaddr *addr,
                            int normalize);

#endif  /* __GRPC_INTERNAL_ENDPOINT_SOCKET_UTILS_H__ */
