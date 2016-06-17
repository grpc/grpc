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

#include <grpc/support/port_platform.h>

#ifdef GPR_POSIX_SOCKET

#include "src/core/lib/iomgr/socket_utils_posix.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include <grpc/support/host_port.h>
#include <grpc/support/log.h>
#include <grpc/support/port_platform.h>
#include <grpc/support/sync.h>
#include "src/core/lib/iomgr/sockaddr_utils.h"
#include "src/core/lib/support/string.h"

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

int grpc_set_socket_no_sigpipe_if_possible(int fd) {
#ifdef GPR_HAVE_SO_NOSIGPIPE
  int val = 1;
  int newval;
  socklen_t intlen = sizeof(newval);
  return 0 == setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &val, sizeof(val)) &&
         0 == getsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &newval, &intlen) &&
         (newval != 0) == val;
#else
  return 1;
#endif
}

int grpc_set_socket_ip_pktinfo_if_possible(int fd) {
#ifdef GPR_HAVE_IP_PKTINFO
  int get_local_ip = 1;
  return 0 == setsockopt(fd, IPPROTO_IP, IP_PKTINFO, &get_local_ip,
                         sizeof(get_local_ip));
#else
  (void)fd;
  return 1;
#endif
}

int grpc_set_socket_ipv6_recvpktinfo_if_possible(int fd) {
#ifdef GPR_HAVE_IPV6_RECVPKTINFO
  int get_local_ip = 1;
  return 0 == setsockopt(fd, IPPROTO_IPV6, IPV6_RECVPKTINFO, &get_local_ip,
                         sizeof(get_local_ip));
#else
  (void)fd;
  return 1;
#endif
}

int grpc_set_socket_sndbuf(int fd, int buffer_size_bytes) {
  return 0 == setsockopt(fd, SOL_SOCKET, SO_SNDBUF, &buffer_size_bytes,
                         sizeof(buffer_size_bytes));
}

int grpc_set_socket_rcvbuf(int fd, int buffer_size_bytes) {
  return 0 == setsockopt(fd, SOL_SOCKET, SO_RCVBUF, &buffer_size_bytes,
                         sizeof(buffer_size_bytes));
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
         (newval != 0) == val;
}

/* disable nagle */
int grpc_set_socket_low_latency(int fd, int low_latency) {
  int val = (low_latency != 0);
  int newval;
  socklen_t intlen = sizeof(newval);
  return 0 == setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &val, sizeof(val)) &&
         0 == getsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &newval, &intlen) &&
         (newval != 0) == val;
}

static gpr_once g_probe_ipv6_once = GPR_ONCE_INIT;
static int g_ipv6_loopback_available;

static void probe_ipv6_once(void) {
  int fd = socket(AF_INET6, SOCK_STREAM, 0);
  g_ipv6_loopback_available = 0;
  if (fd < 0) {
    gpr_log(GPR_INFO, "Disabling AF_INET6 sockets because socket() failed.");
  } else {
    struct sockaddr_in6 addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin6_family = AF_INET6;
    addr.sin6_addr.s6_addr[15] = 1; /* [::1]:0 */
    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) == 0) {
      g_ipv6_loopback_available = 1;
    } else {
      gpr_log(GPR_INFO,
              "Disabling AF_INET6 sockets because ::1 is not available.");
    }
    close(fd);
  }
}

int grpc_ipv6_loopback_available(void) {
  gpr_once_init(&g_probe_ipv6_once, probe_ipv6_once);
  return g_ipv6_loopback_available;
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
    int fd;
    if (grpc_ipv6_loopback_available()) {
      fd = socket(family, type, protocol);
    } else {
      fd = -1;
      errno = EAFNOSUPPORT;
    }
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

#endif
