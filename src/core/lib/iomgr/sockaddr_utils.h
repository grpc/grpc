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

#ifndef GRPC_CORE_LIB_IOMGR_SOCKADDR_UTILS_H
#define GRPC_CORE_LIB_IOMGR_SOCKADDR_UTILS_H

#include "src/core/lib/iomgr/sockaddr.h"

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

/* Writes 0.0.0.0:port. */
void grpc_sockaddr_make_wildcard4(int port, struct sockaddr_in *wild_out);

/* Writes [::]:port. */
void grpc_sockaddr_make_wildcard6(int port, struct sockaddr_in6 *wild_out);

/* Return the IP port number of a sockaddr */
int grpc_sockaddr_get_port(const struct sockaddr *addr);

/* Set IP port number of a sockaddr */
int grpc_sockaddr_set_port(const struct sockaddr *addr, int port);

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

char *grpc_sockaddr_to_uri(const struct sockaddr *addr);

#endif /* GRPC_CORE_LIB_IOMGR_SOCKADDR_UTILS_H */
