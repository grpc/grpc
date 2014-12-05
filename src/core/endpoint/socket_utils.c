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

#include "src/core/endpoint/socket_utils.h"

#include <arpa/inet.h>
#include <limits.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>

#include <grpc/support/host_port.h>
#include <grpc/support/string.h>
#include <grpc/support/log.h>
#include <grpc/support/port_platform.h>

/* set a socket to non blocking mode */
int grpc_set_socket_nonblocking(int fd, int non_blocking) {
  int oldflags = fcntl(fd, F_GETFL, 0);
  if (oldflags < 0) {
    return 0;
  }

  if (non_blocking) {
    oldflags |= O_NONBLOCK;
  } else {
    oldflags &= ~O_NONBLOCK;
  }

  if (fcntl(fd, F_SETFL, oldflags) != 0) {
    return 0;
  }

  return 1;
}

/* set a socket to close on exec */
int grpc_set_socket_cloexec(int fd, int close_on_exec) {
  int oldflags = fcntl(fd, F_GETFD, 0);
  if (oldflags < 0) {
    return 0;
  }

  if (close_on_exec) {
    oldflags |= FD_CLOEXEC;
  } else {
    oldflags &= ~FD_CLOEXEC;
  }

  if (fcntl(fd, F_SETFD, oldflags) != 0) {
    return 0;
  }

  return 1;
}

/* set a socket to reuse old addresses */
int grpc_set_socket_reuse_addr(int fd, int reuse) {
  int val = (reuse != 0);
  int newval;
  socklen_t intlen = sizeof(newval);
  return 0 == setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &val, sizeof(val)) &&
         0 == getsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &newval, &intlen) &&
         newval == val;
}

/* disable nagle */
int grpc_set_socket_low_latency(int fd, int low_latency) {
  int val = (low_latency != 0);
  int newval;
  socklen_t intlen = sizeof(newval);
  return 0 == setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &val, sizeof(val)) &&
         0 == getsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &newval, &intlen) &&
         newval == val;
}

/* This should be 0 in production, but it may be enabled for testing or
   debugging purposes, to simulate an environment where IPv6 sockets can't
   also speak IPv4. */
int grpc_forbid_dualstack_sockets_for_testing = 0;

static int set_socket_dualstack(int fd) {
  if (!grpc_forbid_dualstack_sockets_for_testing) {
    const int off = 0;
    return 0 == setsockopt(fd, IPPROTO_IPV6, IPV6_V6ONLY, &off, sizeof(off));
  } else {
    /* Force an IPv6-only socket, for testing purposes. */
    const int on = 1;
    setsockopt(fd, IPPROTO_IPV6, IPV6_V6ONLY, &on, sizeof(on));
    return 0;
  }
}

int grpc_create_dualstack_socket(const struct sockaddr *addr, int type,
                                 int protocol, grpc_dualstack_mode *dsmode) {
  int family = addr->sa_family;
  if (family == AF_INET6) {
    int fd = socket(family, type, protocol);
    /* Check if we've got a valid dualstack socket. */
    if (fd >= 0 && set_socket_dualstack(fd)) {
      *dsmode = GRPC_DSMODE_DUALSTACK;
      return fd;
    }
    /* If this isn't an IPv4 address, then return whatever we've got. */
    if (!grpc_sockaddr_is_v4mapped(addr, NULL)) {
      *dsmode = GRPC_DSMODE_IPV6;
      return fd;
    }
    /* Fall back to AF_INET. */
    if (fd >= 0) {
      close(fd);
    }
    family = AF_INET;
  }
  *dsmode = family == AF_INET ? GRPC_DSMODE_IPV4 : GRPC_DSMODE_NONE;
  return socket(family, type, protocol);
}

static const gpr_uint8 kV4MappedPrefix[] = {0, 0, 0, 0, 0,    0,
                                            0, 0, 0, 0, 0xff, 0xff};

int grpc_sockaddr_is_v4mapped(const struct sockaddr *addr,
                              struct sockaddr_in *addr4_out) {
  GPR_ASSERT(addr != (struct sockaddr *)addr4_out);
  if (addr->sa_family == AF_INET6) {
    const struct sockaddr_in6 *addr6 = (const struct sockaddr_in6 *)addr;
    if (memcmp(addr6->sin6_addr.s6_addr, kV4MappedPrefix,
               sizeof(kV4MappedPrefix)) == 0) {
      if (addr4_out != NULL) {
        /* Normalize ::ffff:0.0.0.0/96 to IPv4. */
        memset(addr4_out, 0, sizeof(*addr4_out));
        addr4_out->sin_family = AF_INET;
        /* s6_addr32 would be nice, but it's non-standard. */
        memcpy(&addr4_out->sin_addr, &addr6->sin6_addr.s6_addr[12], 4);
        addr4_out->sin_port = addr6->sin6_port;
      }
      return 1;
    }
  }
  return 0;
}

int grpc_sockaddr_to_v4mapped(const struct sockaddr *addr,
                              struct sockaddr_in6 *addr6_out) {
  GPR_ASSERT(addr != (struct sockaddr *)addr6_out);
  if (addr->sa_family == AF_INET) {
    const struct sockaddr_in *addr4 = (const struct sockaddr_in *)addr;
    memset(addr6_out, 0, sizeof(*addr6_out));
    addr6_out->sin6_family = AF_INET6;
    memcpy(&addr6_out->sin6_addr.s6_addr[0], kV4MappedPrefix, 12);
    memcpy(&addr6_out->sin6_addr.s6_addr[12], &addr4->sin_addr, 4);
    addr6_out->sin6_port = addr4->sin_port;
    return 1;
  }
  return 0;
}

int grpc_sockaddr_is_wildcard(const struct sockaddr *addr, int *port_out) {
  struct sockaddr_in addr4_normalized;
  if (grpc_sockaddr_is_v4mapped(addr, &addr4_normalized)) {
    addr = (struct sockaddr *)&addr4_normalized;
  }
  if (addr->sa_family == AF_INET) {
    /* Check for 0.0.0.0 */
    const struct sockaddr_in *addr4 = (const struct sockaddr_in *)addr;
    if (addr4->sin_addr.s_addr != 0) {
      return 0;
    }
    *port_out = ntohs(addr4->sin_port);
    return 1;
  } else if (addr->sa_family == AF_INET6) {
    /* Check for :: */
    const struct sockaddr_in6 *addr6 = (const struct sockaddr_in6 *)addr;
    int i;
    for (i = 0; i < 16; i++) {
      if (addr6->sin6_addr.s6_addr[i] != 0) {
        return 0;
      }
    }
    *port_out = ntohs(addr6->sin6_port);
    return 1;
  } else {
    return 0;
  }
}

void grpc_sockaddr_make_wildcards(int port, struct sockaddr_in *wild4_out,
                                  struct sockaddr_in6 *wild6_out) {
  memset(wild4_out, 0, sizeof(*wild4_out));
  wild4_out->sin_family = AF_INET;
  wild4_out->sin_port = htons(port);

  memset(wild6_out, 0, sizeof(*wild6_out));
  wild6_out->sin6_family = AF_INET6;
  wild6_out->sin6_port = htons(port);
}

int grpc_sockaddr_to_string(char **out, const struct sockaddr *addr,
                            int normalize) {
  const int save_errno = errno;
  struct sockaddr_in addr_normalized;
  char ntop_buf[INET6_ADDRSTRLEN];
  const void *ip = NULL;
  int port;
  int ret;

  *out = NULL;
  if (normalize && grpc_sockaddr_is_v4mapped(addr, &addr_normalized)) {
    addr = (const struct sockaddr *)&addr_normalized;
  }
  if (addr->sa_family == AF_INET) {
    const struct sockaddr_in *addr4 = (const struct sockaddr_in *)addr;
    ip = &addr4->sin_addr;
    port = ntohs(addr4->sin_port);
  } else if (addr->sa_family == AF_INET6) {
    const struct sockaddr_in6 *addr6 = (const struct sockaddr_in6 *)addr;
    ip = &addr6->sin6_addr;
    port = ntohs(addr6->sin6_port);
  }
  if (ip != NULL &&
      inet_ntop(addr->sa_family, ip, ntop_buf, sizeof(ntop_buf)) != NULL) {
    ret = gpr_join_host_port(out, ntop_buf, port);
  } else {
    ret = gpr_asprintf(out, "(sockaddr family=%d)", addr->sa_family);
  }
  /* This is probably redundant, but we wouldn't want to log the wrong error. */
  errno = save_errno;
  return ret;
}
