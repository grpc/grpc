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

#include "src/core/iomgr/sockaddr_utils.h"

#include <errno.h>
#include <string.h>

#ifdef GPR_POSIX_SOCKET
#include <sys/un.h>
#endif

#include <grpc/support/alloc.h>
#include <grpc/support/host_port.h>
#include <grpc/support/log.h>
#include <grpc/support/port_platform.h>
#include <grpc/support/string_util.h>

#include "src/core/support/string.h"

static const uint8_t kV4MappedPrefix[] = {0, 0, 0, 0, 0,    0,
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
  grpc_sockaddr_make_wildcard4(port, wild4_out);
  grpc_sockaddr_make_wildcard6(port, wild6_out);
}

void grpc_sockaddr_make_wildcard4(int port, struct sockaddr_in *wild_out) {
  GPR_ASSERT(port >= 0 && port < 65536);
  memset(wild_out, 0, sizeof(*wild_out));
  wild_out->sin_family = AF_INET;
  wild_out->sin_port = htons((uint16_t)port);
}

void grpc_sockaddr_make_wildcard6(int port, struct sockaddr_in6 *wild_out) {
  GPR_ASSERT(port >= 0 && port < 65536);
  memset(wild_out, 0, sizeof(*wild_out));
  wild_out->sin6_family = AF_INET6;
  wild_out->sin6_port = htons((uint16_t)port);
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
  /* Windows inet_ntop wants a mutable ip pointer */
  if (ip != NULL &&
      inet_ntop(addr->sa_family, (void *)ip, ntop_buf, sizeof(ntop_buf)) !=
          NULL) {
    ret = gpr_join_host_port(out, ntop_buf, port);
  } else {
    ret = gpr_asprintf(out, "(sockaddr family=%d)", addr->sa_family);
  }
  /* This is probably redundant, but we wouldn't want to log the wrong error. */
  errno = save_errno;
  return ret;
}

char *grpc_sockaddr_to_uri(const struct sockaddr *addr) {
  char *temp;
  char *result;
  struct sockaddr_in addr_normalized;

  if (grpc_sockaddr_is_v4mapped(addr, &addr_normalized)) {
    addr = (const struct sockaddr *)&addr_normalized;
  }

  switch (addr->sa_family) {
    case AF_INET:
      grpc_sockaddr_to_string(&temp, addr, 0);
      gpr_asprintf(&result, "ipv4:%s", temp);
      gpr_free(temp);
      return result;
    case AF_INET6:
      grpc_sockaddr_to_string(&temp, addr, 0);
      gpr_asprintf(&result, "ipv6:%s", temp);
      gpr_free(temp);
      return result;
#ifdef GPR_POSIX_SOCKET
    case AF_UNIX:
      gpr_asprintf(&result, "unix:%s", ((struct sockaddr_un *)addr)->sun_path);
      return result;
#endif
  }

  return NULL;
}

int grpc_sockaddr_get_port(const struct sockaddr *addr) {
  switch (addr->sa_family) {
    case AF_INET:
      return ntohs(((struct sockaddr_in *)addr)->sin_port);
    case AF_INET6:
      return ntohs(((struct sockaddr_in6 *)addr)->sin6_port);
    case AF_UNIX:
      return 1;
    default:
      gpr_log(GPR_ERROR, "Unknown socket family %d in grpc_sockaddr_get_port",
              addr->sa_family);
      return 0;
  }
}

int grpc_sockaddr_set_port(const struct sockaddr *addr, int port) {
  switch (addr->sa_family) {
    case AF_INET:
      GPR_ASSERT(port >= 0 && port < 65536);
      ((struct sockaddr_in *)addr)->sin_port = htons((uint16_t)port);
      return 1;
    case AF_INET6:
      GPR_ASSERT(port >= 0 && port < 65536);
      ((struct sockaddr_in6 *)addr)->sin6_port = htons((uint16_t)port);
      return 1;
    default:
      gpr_log(GPR_ERROR, "Unknown socket family %d in grpc_sockaddr_set_port",
              addr->sa_family);
      return 0;
  }
}
