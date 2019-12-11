/*
 *
 * Copyright 2015 gRPC authors.
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

#include "src/core/lib/iomgr/port.h"

#ifdef GRPC_POSIX_SOCKET_UTILS_COMMON

#include "src/core/lib/iomgr/socket_utils.h"
#include "src/core/lib/iomgr/socket_utils_posix.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <netinet/in.h>
#ifdef GRPC_LINUX_TCP_H
#include <linux/tcp.h>
#else
#include <netinet/tcp.h>
#endif
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/sync.h>

#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/gpr/string.h"
#include "src/core/lib/iomgr/sockaddr.h"
#include "src/core/lib/iomgr/sockaddr_utils.h"

/* set a socket to non blocking mode */
grpc_error* grpc_set_socket_nonblocking(int fd, int non_blocking) {
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

grpc_error* grpc_set_socket_no_sigpipe_if_possible(int fd) {
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
    return GRPC_ERROR_CREATE_FROM_STATIC_STRING("Failed to set SO_NOSIGPIPE");
  }
#else
  // Avoid unused parameter warning for conditional parameter
  (void)fd;
#endif
  return GRPC_ERROR_NONE;
}

grpc_error* grpc_set_socket_ip_pktinfo_if_possible(int fd) {
  // Use conditionally-important parameter to avoid warning
  (void)fd;
#ifdef GRPC_HAVE_IP_PKTINFO
  int get_local_ip = 1;
  if (0 != setsockopt(fd, IPPROTO_IP, IP_PKTINFO, &get_local_ip,
                      sizeof(get_local_ip))) {
    return GRPC_OS_ERROR(errno, "setsockopt(IP_PKTINFO)");
  }
#endif
  return GRPC_ERROR_NONE;
}

grpc_error* grpc_set_socket_ipv6_recvpktinfo_if_possible(int fd) {
  // Use conditionally-important parameter to avoid warning
  (void)fd;
#ifdef GRPC_HAVE_IPV6_RECVPKTINFO
  int get_local_ip = 1;
  if (0 != setsockopt(fd, IPPROTO_IPV6, IPV6_RECVPKTINFO, &get_local_ip,
                      sizeof(get_local_ip))) {
    return GRPC_OS_ERROR(errno, "setsockopt(IPV6_RECVPKTINFO)");
  }
#endif
  return GRPC_ERROR_NONE;
}

grpc_error* grpc_set_socket_sndbuf(int fd, int buffer_size_bytes) {
  return 0 == setsockopt(fd, SOL_SOCKET, SO_SNDBUF, &buffer_size_bytes,
                         sizeof(buffer_size_bytes))
             ? GRPC_ERROR_NONE
             : GRPC_OS_ERROR(errno, "setsockopt(SO_SNDBUF)");
}

grpc_error* grpc_set_socket_rcvbuf(int fd, int buffer_size_bytes) {
  return 0 == setsockopt(fd, SOL_SOCKET, SO_RCVBUF, &buffer_size_bytes,
                         sizeof(buffer_size_bytes))
             ? GRPC_ERROR_NONE
             : GRPC_OS_ERROR(errno, "setsockopt(SO_RCVBUF)");
}

/* set a socket to close on exec */
grpc_error* grpc_set_socket_cloexec(int fd, int close_on_exec) {
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
grpc_error* grpc_set_socket_reuse_addr(int fd, int reuse) {
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
    return GRPC_ERROR_CREATE_FROM_STATIC_STRING("Failed to set SO_REUSEADDR");
  }

  return GRPC_ERROR_NONE;
}

/* set a socket to reuse old addresses */
grpc_error* grpc_set_socket_reuse_port(int fd, int reuse) {
#ifndef SO_REUSEPORT
  return GRPC_ERROR_CREATE_FROM_STATIC_STRING(
      "SO_REUSEPORT unavailable on compiling system");
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
    return GRPC_ERROR_CREATE_FROM_STATIC_STRING("Failed to set SO_REUSEPORT");
  }

  return GRPC_ERROR_NONE;
#endif
}

static gpr_once g_probe_so_reuesport_once = GPR_ONCE_INIT;
static int g_support_so_reuseport = false;

void probe_so_reuseport_once(void) {
#ifndef GPR_MANYLINUX1
  int s = socket(AF_INET, SOCK_STREAM, 0);
  if (s < 0) {
    /* This might be an ipv6-only environment in which case 'socket(AF_INET,..)'
       call would fail. Try creating IPv6 socket in that case */
    s = socket(AF_INET6, SOCK_STREAM, 0);
  }
  if (s >= 0) {
    g_support_so_reuseport = GRPC_LOG_IF_ERROR(
        "check for SO_REUSEPORT", grpc_set_socket_reuse_port(s, 1));
    close(s);
  }
#endif
}

bool grpc_is_socket_reuse_port_supported() {
  gpr_once_init(&g_probe_so_reuesport_once, probe_so_reuseport_once);
  return g_support_so_reuseport;
}

/* disable nagle */
grpc_error* grpc_set_socket_low_latency(int fd, int low_latency) {
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
    return GRPC_ERROR_CREATE_FROM_STATIC_STRING("Failed to set TCP_NODELAY");
  }
  return GRPC_ERROR_NONE;
}

/* The default values for TCP_USER_TIMEOUT are currently configured to be in
 * line with the default values of KEEPALIVE_TIMEOUT as proposed in
 * https://github.com/grpc/proposal/blob/master/A18-tcp-user-timeout.md */
#define DEFAULT_CLIENT_TCP_USER_TIMEOUT_MS 20000 /* 20 seconds */
#define DEFAULT_SERVER_TCP_USER_TIMEOUT_MS 20000 /* 20 seconds */

static int g_default_client_tcp_user_timeout_ms =
    DEFAULT_CLIENT_TCP_USER_TIMEOUT_MS;
static int g_default_server_tcp_user_timeout_ms =
    DEFAULT_SERVER_TCP_USER_TIMEOUT_MS;
static bool g_default_client_tcp_user_timeout_enabled = false;
static bool g_default_server_tcp_user_timeout_enabled = true;

void config_default_tcp_user_timeout(bool enable, int timeout, bool is_client) {
  if (is_client) {
    g_default_client_tcp_user_timeout_enabled = enable;
    if (timeout > 0) {
      g_default_client_tcp_user_timeout_ms = timeout;
    }
  } else {
    g_default_server_tcp_user_timeout_enabled = enable;
    if (timeout > 0) {
      g_default_server_tcp_user_timeout_ms = timeout;
    }
  }
}

/* Set TCP_USER_TIMEOUT */
grpc_error* grpc_set_socket_tcp_user_timeout(
    int fd, const grpc_channel_args* channel_args, bool is_client) {
  // Use conditionally-important parameter to avoid warning
  (void)fd;
  (void)channel_args;
  (void)is_client;
#ifdef GRPC_HAVE_TCP_USER_TIMEOUT
  bool enable;
  int timeout;
  if (is_client) {
    enable = g_default_client_tcp_user_timeout_enabled;
    timeout = g_default_client_tcp_user_timeout_ms;
  } else {
    enable = g_default_server_tcp_user_timeout_enabled;
    timeout = g_default_server_tcp_user_timeout_ms;
  }
  if (channel_args) {
    for (unsigned int i = 0; i < channel_args->num_args; i++) {
      if (0 == strcmp(channel_args->args[i].key, GRPC_ARG_KEEPALIVE_TIME_MS)) {
        const int value = grpc_channel_arg_get_integer(
            &channel_args->args[i], grpc_integer_options{0, 1, INT_MAX});
        /* Continue using default if value is 0 */
        if (value == 0) {
          continue;
        }
        /* Disable if value is INT_MAX */
        enable = value != INT_MAX;
      } else if (0 == strcmp(channel_args->args[i].key,
                             GRPC_ARG_KEEPALIVE_TIMEOUT_MS)) {
        const int value = grpc_channel_arg_get_integer(
            &channel_args->args[i], grpc_integer_options{0, 1, INT_MAX});
        /* Continue using default if value is 0 */
        if (value == 0) {
          continue;
        }
        timeout = value;
      }
    }
  }
  if (enable) {
    extern grpc_core::TraceFlag grpc_tcp_trace;
    if (GRPC_TRACE_FLAG_ENABLED(grpc_tcp_trace)) {
      gpr_log(GPR_INFO, "Enabling TCP_USER_TIMEOUT with a timeout of %d ms",
              timeout);
    }
    int newval;
    socklen_t len = sizeof(newval);
    if (0 != setsockopt(fd, IPPROTO_TCP, TCP_USER_TIMEOUT, &timeout,
                        sizeof(timeout))) {
      gpr_log(GPR_ERROR, "setsockopt(TCP_USER_TIMEOUT) %s", strerror(errno));
      return GRPC_ERROR_NONE;
    }
    if (0 != getsockopt(fd, IPPROTO_TCP, TCP_USER_TIMEOUT, &newval, &len)) {
      gpr_log(GPR_ERROR, "getsockopt(TCP_USER_TIMEOUT) %s", strerror(errno));
      return GRPC_ERROR_NONE;
    }
    if (newval != timeout) {
      /* Do not fail on failing to set TCP_USER_TIMEOUT for now. */
      gpr_log(GPR_ERROR, "Failed to set TCP_USER_TIMEOUT");
      return GRPC_ERROR_NONE;
    }
  }
#else
  extern grpc_core::TraceFlag grpc_tcp_trace;
  if (GRPC_TRACE_FLAG_ENABLED(grpc_tcp_trace)) {
    gpr_log(GPR_INFO, "TCP_USER_TIMEOUT not supported for this platform");
  }
#endif /* GRPC_HAVE_TCP_USER_TIMEOUT */
  return GRPC_ERROR_NONE;
}

/* set a socket using a grpc_socket_mutator */
grpc_error* grpc_set_socket_with_mutator(int fd, grpc_socket_mutator* mutator) {
  GPR_ASSERT(mutator);
  if (!grpc_socket_mutator_mutate_fd(mutator, fd)) {
    return GRPC_ERROR_CREATE_FROM_STATIC_STRING("grpc_socket_mutator failed.");
  }
  return GRPC_ERROR_NONE;
}

grpc_error* grpc_apply_socket_mutator_in_args(int fd,
                                              const grpc_channel_args* args) {
  const grpc_arg* socket_mutator_arg =
      grpc_channel_args_find(args, GRPC_ARG_SOCKET_MUTATOR);
  if (socket_mutator_arg == nullptr) {
    return GRPC_ERROR_NONE;
  }
  GPR_DEBUG_ASSERT(socket_mutator_arg->type == GRPC_ARG_POINTER);
  grpc_socket_mutator* mutator =
      static_cast<grpc_socket_mutator*>(socket_mutator_arg->value.pointer.p);
  return grpc_set_socket_with_mutator(fd, mutator);
}

static gpr_once g_probe_ipv6_once = GPR_ONCE_INIT;
static int g_ipv6_loopback_available;

static void probe_ipv6_once(void) {
  int fd = socket(AF_INET6, SOCK_STREAM, 0);
  g_ipv6_loopback_available = 0;
  if (fd < 0) {
    gpr_log(GPR_INFO, "Disabling AF_INET6 sockets because socket() failed.");
  } else {
    grpc_sockaddr_in6 addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin6_family = AF_INET6;
    addr.sin6_addr.s6_addr[15] = 1; /* [::1]:0 */
    if (bind(fd, reinterpret_cast<grpc_sockaddr*>(&addr), sizeof(addr)) == 0) {
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

static grpc_error* error_for_fd(int fd, const grpc_resolved_address* addr) {
  if (fd >= 0) return GRPC_ERROR_NONE;
  char* addr_str;
  grpc_sockaddr_to_string(&addr_str, addr, 0);
  grpc_error* err = grpc_error_set_str(GRPC_OS_ERROR(errno, "socket"),
                                       GRPC_ERROR_STR_TARGET_ADDRESS,
                                       grpc_slice_from_copied_string(addr_str));
  gpr_free(addr_str);
  return err;
}

grpc_error* grpc_create_dualstack_socket(
    const grpc_resolved_address* resolved_addr, int type, int protocol,
    grpc_dualstack_mode* dsmode, int* newfd) {
  return grpc_create_dualstack_socket_using_factory(
      nullptr, resolved_addr, type, protocol, dsmode, newfd);
}

static int create_socket(grpc_socket_factory* factory, int domain, int type,
                         int protocol) {
  return (factory != nullptr)
             ? grpc_socket_factory_socket(factory, domain, type, protocol)
             : socket(domain, type, protocol);
}

grpc_error* grpc_create_dualstack_socket_using_factory(
    grpc_socket_factory* factory, const grpc_resolved_address* resolved_addr,
    int type, int protocol, grpc_dualstack_mode* dsmode, int* newfd) {
  const grpc_sockaddr* addr =
      reinterpret_cast<const grpc_sockaddr*>(resolved_addr->addr);
  int family = addr->sa_family;
  if (family == AF_INET6) {
    if (grpc_ipv6_loopback_available()) {
      *newfd = create_socket(factory, family, type, protocol);
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
    if (!grpc_sockaddr_is_v4mapped(resolved_addr, nullptr)) {
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
  *newfd = create_socket(factory, family, type, protocol);
  return error_for_fd(*newfd, resolved_addr);
}

uint16_t grpc_htons(uint16_t hostshort) { return htons(hostshort); }

uint16_t grpc_ntohs(uint16_t netshort) { return ntohs(netshort); }

uint32_t grpc_htonl(uint32_t hostlong) { return htonl(hostlong); }

uint32_t grpc_ntohl(uint32_t netlong) { return ntohl(netlong); }

int grpc_inet_pton(int af, const char* src, void* dst) {
  return inet_pton(af, src, dst);
}

const char* grpc_inet_ntop(int af, const void* src, char* dst, size_t size) {
  GPR_ASSERT(size <= (socklen_t)-1);
  return inet_ntop(af, src, dst, static_cast<socklen_t>(size));
}

#endif
