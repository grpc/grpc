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

#include "src/core/lib/iomgr/sockaddr_utils.h"

#include <errno.h>
#include <string.h>

#include <grpc/support/alloc.h>
#include <grpc/support/host_port.h>
#include <grpc/support/log.h>
#include <grpc/support/port_platform.h>
#include <grpc/support/string_util.h>

#include "src/core/lib/iomgr/sockaddr.h"
#include "src/core/lib/iomgr/socket_utils.h"
#include "src/core/lib/iomgr/unix_sockets_posix.h"
#include "src/core/lib/support/string.h"

static const uint8_t kV4MappedPrefix[] = {0, 0, 0, 0, 0,    0,
                                          0, 0, 0, 0, 0xff, 0xff};

int grpc_sockaddr_is_v4mapped(const grpc_resolved_address *resolved_addr,
                              grpc_resolved_address *resolved_addr4_out) {
  GPR_ASSERT(resolved_addr != resolved_addr4_out);
  const struct sockaddr *addr = (const struct sockaddr *)resolved_addr->addr;
  struct sockaddr_in *addr4_out =
      resolved_addr4_out == NULL
          ? NULL
          : (struct sockaddr_in *)resolved_addr4_out->addr;
  if (addr->sa_family == AF_INET6) {
    const struct sockaddr_in6 *addr6 = (const struct sockaddr_in6 *)addr;
    if (memcmp(addr6->sin6_addr.s6_addr, kV4MappedPrefix,
               sizeof(kV4MappedPrefix)) == 0) {
      if (resolved_addr4_out != NULL) {
        /* Normalize ::ffff:0.0.0.0/96 to IPv4. */
        memset(resolved_addr4_out, 0, sizeof(*resolved_addr4_out));
        addr4_out->sin_family = AF_INET;
        /* s6_addr32 would be nice, but it's non-standard. */
        memcpy(&addr4_out->sin_addr, &addr6->sin6_addr.s6_addr[12], 4);
        addr4_out->sin_port = addr6->sin6_port;
        resolved_addr4_out->len = sizeof(struct sockaddr_in);
      }
      return 1;
    }
  }
  return 0;
}

int grpc_sockaddr_to_v4mapped(const grpc_resolved_address *resolved_addr,
                              grpc_resolved_address *resolved_addr6_out) {
  GPR_ASSERT(resolved_addr != resolved_addr6_out);
  const struct sockaddr *addr = (const struct sockaddr *)resolved_addr->addr;
  struct sockaddr_in6 *addr6_out =
      (struct sockaddr_in6 *)resolved_addr6_out->addr;
  if (addr->sa_family == AF_INET) {
    const struct sockaddr_in *addr4 = (const struct sockaddr_in *)addr;
    memset(resolved_addr6_out, 0, sizeof(*resolved_addr6_out));
    addr6_out->sin6_family = AF_INET6;
    memcpy(&addr6_out->sin6_addr.s6_addr[0], kV4MappedPrefix, 12);
    memcpy(&addr6_out->sin6_addr.s6_addr[12], &addr4->sin_addr, 4);
    addr6_out->sin6_port = addr4->sin_port;
    resolved_addr6_out->len = sizeof(struct sockaddr_in6);
    return 1;
  }
  return 0;
}

int grpc_sockaddr_is_wildcard(const grpc_resolved_address *resolved_addr,
                              int *port_out) {
  const struct sockaddr *addr;
  grpc_resolved_address addr4_normalized;
  if (grpc_sockaddr_is_v4mapped(resolved_addr, &addr4_normalized)) {
    resolved_addr = &addr4_normalized;
  }
  addr = (const struct sockaddr *)resolved_addr->addr;
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

void grpc_sockaddr_make_wildcards(int port, grpc_resolved_address *wild4_out,
                                  grpc_resolved_address *wild6_out) {
  grpc_sockaddr_make_wildcard4(port, wild4_out);
  grpc_sockaddr_make_wildcard6(port, wild6_out);
}

void grpc_sockaddr_make_wildcard4(int port,
                                  grpc_resolved_address *resolved_wild_out) {
  struct sockaddr_in *wild_out = (struct sockaddr_in *)resolved_wild_out->addr;
  GPR_ASSERT(port >= 0 && port < 65536);
  memset(resolved_wild_out, 0, sizeof(*resolved_wild_out));
  wild_out->sin_family = AF_INET;
  wild_out->sin_port = htons((uint16_t)port);
  resolved_wild_out->len = sizeof(struct sockaddr_in);
}

void grpc_sockaddr_make_wildcard6(int port,
                                  grpc_resolved_address *resolved_wild_out) {
  struct sockaddr_in6 *wild_out =
      (struct sockaddr_in6 *)resolved_wild_out->addr;
  GPR_ASSERT(port >= 0 && port < 65536);
  memset(resolved_wild_out, 0, sizeof(*resolved_wild_out));
  wild_out->sin6_family = AF_INET6;
  wild_out->sin6_port = htons((uint16_t)port);
  resolved_wild_out->len = sizeof(struct sockaddr_in6);
}

int grpc_sockaddr_to_string(char **out,
                            const grpc_resolved_address *resolved_addr,
                            int normalize) {
  const struct sockaddr *addr;
  const int save_errno = errno;
  grpc_resolved_address addr_normalized;
  char ntop_buf[INET6_ADDRSTRLEN];
  const void *ip = NULL;
  int port;
  uint32_t sin6_scope_id = 0;
  int ret;

  *out = NULL;
  if (normalize && grpc_sockaddr_is_v4mapped(resolved_addr, &addr_normalized)) {
    resolved_addr = &addr_normalized;
  }
  addr = (const struct sockaddr *)resolved_addr->addr;
  if (addr->sa_family == AF_INET) {
    const struct sockaddr_in *addr4 = (const struct sockaddr_in *)addr;
    ip = &addr4->sin_addr;
    port = ntohs(addr4->sin_port);
  } else if (addr->sa_family == AF_INET6) {
    const struct sockaddr_in6 *addr6 = (const struct sockaddr_in6 *)addr;
    ip = &addr6->sin6_addr;
    port = ntohs(addr6->sin6_port);
    sin6_scope_id = addr6->sin6_scope_id;
  }
  if (ip != NULL &&
      grpc_inet_ntop(addr->sa_family, ip, ntop_buf, sizeof(ntop_buf)) != NULL) {
    if (sin6_scope_id != 0) {
      char *host_with_scope;
      /* Enclose sin6_scope_id with the format defined in RFC 6784 section 2. */
      gpr_asprintf(&host_with_scope, "%s%%25%" PRIu32, ntop_buf, sin6_scope_id);
      ret = gpr_join_host_port(out, host_with_scope, port);
      gpr_free(host_with_scope);
    } else {
      ret = gpr_join_host_port(out, ntop_buf, port);
    }
  } else {
    ret = gpr_asprintf(out, "(sockaddr family=%d)", addr->sa_family);
  }
  /* This is probably redundant, but we wouldn't want to log the wrong error. */
  errno = save_errno;
  return ret;
}

char *grpc_sockaddr_to_uri(const grpc_resolved_address *resolved_addr) {
  grpc_resolved_address addr_normalized;
  if (grpc_sockaddr_is_v4mapped(resolved_addr, &addr_normalized)) {
    resolved_addr = &addr_normalized;
  }
  const char *scheme = grpc_sockaddr_get_uri_scheme(resolved_addr);
  if (scheme == NULL || strcmp("unix", scheme) == 0) {
    return grpc_sockaddr_to_uri_unix_if_possible(resolved_addr);
  }
  char *path = NULL;
  char *uri_str = NULL;
  if (grpc_sockaddr_to_string(&path, resolved_addr,
                              false /* suppress errors */) &&
      scheme != NULL) {
    gpr_asprintf(&uri_str, "%s:%s", scheme, path);
  }
  gpr_free(path);
  return uri_str != NULL ? uri_str : NULL;
}

const char *grpc_sockaddr_get_uri_scheme(
    const grpc_resolved_address *resolved_addr) {
  const struct sockaddr *addr = (const struct sockaddr *)resolved_addr->addr;
  switch (addr->sa_family) {
    case AF_INET:
      return "ipv4";
    case AF_INET6:
      return "ipv6";
    case AF_UNIX:
      return "unix";
  }
  return NULL;
}

int grpc_sockaddr_get_port(const grpc_resolved_address *resolved_addr) {
  const struct sockaddr *addr = (const struct sockaddr *)resolved_addr->addr;
  switch (addr->sa_family) {
    case AF_INET:
      return ntohs(((struct sockaddr_in *)addr)->sin_port);
    case AF_INET6:
      return ntohs(((struct sockaddr_in6 *)addr)->sin6_port);
    default:
      if (grpc_is_unix_socket(resolved_addr)) {
        return 1;
      }
      gpr_log(GPR_ERROR, "Unknown socket family %d in grpc_sockaddr_get_port",
              addr->sa_family);
      return 0;
  }
}

int grpc_sockaddr_set_port(const grpc_resolved_address *resolved_addr,
                           int port) {
  const struct sockaddr *addr = (const struct sockaddr *)resolved_addr->addr;
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
