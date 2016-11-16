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

#include "src/core/lib/iomgr/port.h"

#ifdef GRPC_POSIX_SOCKET

#include "src/core/lib/iomgr/socket_utils.h"
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

#include <grpc/support/alloc.h>
#include <grpc/support/host_port.h>
#include <grpc/support/log.h>
#include <grpc/support/port_platform.h>
#include <grpc/support/sync.h>
#include "src/core/lib/iomgr/sockaddr_utils.h"
#include "src/core/lib/support/string.h"

/* set a socket to non blocking mode */
grpc_error *grpc_set_socket_nonblocking(int fd, int non_blocking) {
  int oldflags = fcntl(fd, F_GETFL, 0);
  if (oldflags < 0) {
    return GRPC_OS_ERROR(errno, "fcntl");
  }

  if (non_blocking) {
    oldflags |= O_NONBLOCK;
  } else {
    oldflags &= ~O_NONBLOCK;
  }

  if (fcntl(fd, F_SETFL, oldflags) != 0) {
    return GRPC_OS_ERROR(errno, "fcntl");
  }

  return GRPC_ERROR_NONE;
}

grpc_error *grpc_set_socket_no_sigpipe_if_possible(int fd) {
#ifdef GRPC_HAVE_SO_NOSIGPIPE
  int val = 1;
  int newval;
  socklen_t intlen = sizeof(newval);
  if (0 != setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &val, sizeof(val))) {
    return GRPC_OS_ERROR(errno, "setsockopt(SO_NOSIGPIPE)");
  }
  if (0 != getsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &newval, &intlen)) {
    return GRPC_OS_ERROR(errno, "getsockopt(SO_NOSIGPIPE)");
  }
  if ((newval != 0) != (val != 0)) {
    return GRPC_ERROR_CREATE("Failed to set SO_NOSIGPIPE");
  }
#endif
  return GRPC_ERROR_NONE;
}

grpc_error *grpc_set_socket_ip_pktinfo_if_possible(int fd) {
#ifdef GRPC_HAVE_IP_PKTINFO
  int get_local_ip = 1;
  if (0 != setsockopt(fd, IPPROTO_IP, IP_PKTINFO, &get_local_ip,
                      sizeof(get_local_ip))) {
    return GRPC_OS_ERROR(errno, "setsockopt(IP_PKTINFO)");
  }
#endif
  return GRPC_ERROR_NONE;
}

grpc_error *grpc_set_socket_ipv6_recvpktinfo_if_possible(int fd) {
#ifdef GRPC_HAVE_IPV6_RECVPKTINFO
  int get_local_ip = 1;
  if (0 != setsockopt(fd, IPPROTO_IPV6, IPV6_RECVPKTINFO, &get_local_ip,
                      sizeof(get_local_ip))) {
    return GRPC_OS_ERROR(errno, "setsockopt(IPV6_RECVPKTINFO)");
  }
#endif
  return GRPC_ERROR_NONE;
}

grpc_error *grpc_set_socket_sndbuf(int fd, int buffer_size_bytes) {
  return 0 == setsockopt(fd, SOL_SOCKET, SO_SNDBUF, &buffer_size_bytes,
                         sizeof(buffer_size_bytes))
             ? GRPC_ERROR_NONE
             : GRPC_OS_ERROR(errno, "setsockopt(SO_SNDBUF)");
}

grpc_error *grpc_set_socket_rcvbuf(int fd, int buffer_size_bytes) {
  return 0 == setsockopt(fd, SOL_SOCKET, SO_RCVBUF, &buffer_size_bytes,
                         sizeof(buffer_size_bytes))
             ? GRPC_ERROR_NONE
             : GRPC_OS_ERROR(errno, "setsockopt(SO_RCVBUF)");
}

/* set a socket to close on exec */
grpc_error *grpc_set_socket_cloexec(int fd, int close_on_exec) {
  int oldflags = fcntl(fd, F_GETFD, 0);
  if (oldflags < 0) {
    return GRPC_OS_ERROR(errno, "fcntl");
  }

  if (close_on_exec) {
    oldflags |= FD_CLOEXEC;
  } else {
    oldflags &= ~FD_CLOEXEC;
  }

  if (fcntl(fd, F_SETFD, oldflags) != 0) {
    return GRPC_OS_ERROR(errno, "fcntl");
  }

  return GRPC_ERROR_NONE;
}

/* set a socket to reuse old addresses */
grpc_error *grpc_set_socket_reuse_addr(int fd, int reuse) {
  int val = (reuse != 0);
  int newval;
  socklen_t intlen = sizeof(newval);
  if (0 != setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &val, sizeof(val))) {
    return GRPC_OS_ERROR(errno, "setsockopt(SO_REUSEADDR)");
  }
  if (0 != getsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &newval, &intlen)) {
    return GRPC_OS_ERROR(errno, "getsockopt(SO_REUSEADDR)");
  }
  if ((newval != 0) != val) {
    return GRPC_ERROR_CREATE("Failed to set SO_REUSEADDR");
  }

  return GRPC_ERROR_NONE;
}

/* set a socket to reuse old addresses */
grpc_error *grpc_set_socket_reuse_port(int fd, int reuse) {
#ifndef SO_REUSEPORT
  return GRPC_ERROR_CREATE("SO_REUSEPORT unavailable on compiling system");
#else
  int val = (reuse != 0);
  int newval;
  socklen_t intlen = sizeof(newval);
  if (0 != setsockopt(fd, SOL_SOCKET, SO_REUSEPORT, &val, sizeof(val))) {
    return GRPC_OS_ERROR(errno, "setsockopt(SO_REUSEPORT)");
  }
  if (0 != getsockopt(fd, SOL_SOCKET, SO_REUSEPORT, &newval, &intlen)) {
    return GRPC_OS_ERROR(errno, "getsockopt(SO_REUSEPORT)");
  }
  if ((newval != 0) != val) {
    return GRPC_ERROR_CREATE("Failed to set SO_REUSEPORT");
  }

  return GRPC_ERROR_NONE;
#endif
}

/* disable nagle */
grpc_error *grpc_set_socket_low_latency(int fd, int low_latency) {
  int val = (low_latency != 0);
  int newval;
  socklen_t intlen = sizeof(newval);
  if (0 != setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &val, sizeof(val))) {
    return GRPC_OS_ERROR(errno, "setsockopt(TCP_NODELAY)");
  }
  if (0 != getsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &newval, &intlen)) {
    return GRPC_OS_ERROR(errno, "getsockopt(TCP_NODELAY)");
  }
  if ((newval != 0) != val) {
    return GRPC_ERROR_CREATE("Failed to set TCP_NODELAY");
  }
  return GRPC_ERROR_NONE;
}

/* set a socket using a grpc_socket_mutator */
grpc_error *grpc_set_socket_with_mutator(int fd, grpc_socket_mutator *mutator) {
  GPR_ASSERT(mutator);
  if (!grpc_socket_mutator_mutate_fd(mutator, fd)) {
    return GRPC_ERROR_CREATE("grpc_socket_mutator failed.");
  }
  return GRPC_ERROR_NONE;
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

static grpc_error *error_for_fd(int fd, const grpc_resolved_address *addr) {
  if (fd >= 0) return GRPC_ERROR_NONE;
  char *addr_str;
  grpc_sockaddr_to_string(&addr_str, addr, 0);
  grpc_error *err = grpc_error_set_str(GRPC_OS_ERROR(errno, "socket"),
                                       GRPC_ERROR_STR_TARGET_ADDRESS, addr_str);
  gpr_free(addr_str);
  return err;
}

grpc_error *grpc_create_dualstack_socket(
    const grpc_resolved_address *resolved_addr, int type, int protocol,
    grpc_dualstack_mode *dsmode, int *newfd) {
  const struct sockaddr *addr = (const struct sockaddr *)resolved_addr->addr;
  int family = addr->sa_family;
  if (family == AF_INET6) {
    if (grpc_ipv6_loopback_available()) {
      *newfd = socket(family, type, protocol);
    } else {
      *newfd = -1;
      errno = EAFNOSUPPORT;
    }
    /* Check if we've got a valid dualstack socket. */
    if (*newfd >= 0 && set_socket_dualstack(*newfd)) {
      *dsmode = GRPC_DSMODE_DUALSTACK;
      return GRPC_ERROR_NONE;
    }
    /* If this isn't an IPv4 address, then return whatever we've got. */
    if (!grpc_sockaddr_is_v4mapped(resolved_addr, NULL)) {
      *dsmode = GRPC_DSMODE_IPV6;
      return error_for_fd(*newfd, resolved_addr);
    }
    /* Fall back to AF_INET. */
    if (*newfd >= 0) {
      close(*newfd);
    }
    family = AF_INET;
  }
  *dsmode = family == AF_INET ? GRPC_DSMODE_IPV4 : GRPC_DSMODE_NONE;
  *newfd = socket(family, type, protocol);
  return error_for_fd(*newfd, resolved_addr);
}

const char *grpc_inet_ntop(int af, const void *src, char *dst, size_t size) {
  GPR_ASSERT(size <= (socklen_t)-1);
  return inet_ntop(af, src, dst, (socklen_t)size);
}

#endif
