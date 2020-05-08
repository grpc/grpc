/*
 *
 * Copyright 2016 gRPC authors.
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

#include "src/core/lib/iomgr/sockaddr_utils.h"

#include <errno.h>
#include <inttypes.h>
#include <string.h>

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>

#include "src/core/lib/gpr/string.h"
#include "src/core/lib/gprpp/host_port.h"
#include "src/core/lib/iomgr/sockaddr.h"
#include "src/core/lib/iomgr/socket_utils.h"
#include "src/core/lib/iomgr/unix_sockets_posix.h"

static const uint8_t kV4MappedPrefix[] = {0, 0, 0, 0, 0,    0,
                                          0, 0, 0, 0, 0xff, 0xff};

int grpc_sockaddr_is_v4mapped(const grpc_resolved_address* resolved_addr,
                              grpc_resolved_address* resolved_addr4_out) {
  GPR_ASSERT(resolved_addr != resolved_addr4_out);
  const grpc_sockaddr* addr =
      reinterpret_cast<const grpc_sockaddr*>(resolved_addr->addr);
  grpc_sockaddr_in* addr4_out =
      resolved_addr4_out == nullptr
          ? nullptr
          : reinterpret_cast<grpc_sockaddr_in*>(resolved_addr4_out->addr);
  if (addr->sa_family == GRPC_AF_INET6) {
    const grpc_sockaddr_in6* addr6 =
        reinterpret_cast<const grpc_sockaddr_in6*>(addr);
    if (memcmp(addr6->sin6_addr.s6_addr, kV4MappedPrefix,
               sizeof(kV4MappedPrefix)) == 0) {
      if (resolved_addr4_out != nullptr) {
        /* Normalize ::ffff:0.0.0.0/96 to IPv4. */
        memset(resolved_addr4_out, 0, sizeof(*resolved_addr4_out));
        addr4_out->sin_family = GRPC_AF_INET;
        /* s6_addr32 would be nice, but it's non-standard. */
        memcpy(&addr4_out->sin_addr, &addr6->sin6_addr.s6_addr[12], 4);
        addr4_out->sin_port = addr6->sin6_port;
        resolved_addr4_out->len =
            static_cast<socklen_t>(sizeof(grpc_sockaddr_in));
      }
      return 1;
    }
  }
  return 0;
}

int grpc_sockaddr_to_v4mapped(const grpc_resolved_address* resolved_addr,
                              grpc_resolved_address* resolved_addr6_out) {
  GPR_ASSERT(resolved_addr != resolved_addr6_out);
  const grpc_sockaddr* addr =
      reinterpret_cast<const grpc_sockaddr*>(resolved_addr->addr);
  grpc_sockaddr_in6* addr6_out =
      reinterpret_cast<grpc_sockaddr_in6*>(resolved_addr6_out->addr);
  if (addr->sa_family == GRPC_AF_INET) {
    const grpc_sockaddr_in* addr4 =
        reinterpret_cast<const grpc_sockaddr_in*>(addr);
    memset(resolved_addr6_out, 0, sizeof(*resolved_addr6_out));
    addr6_out->sin6_family = GRPC_AF_INET6;
    memcpy(&addr6_out->sin6_addr.s6_addr[0], kV4MappedPrefix, 12);
    memcpy(&addr6_out->sin6_addr.s6_addr[12], &addr4->sin_addr, 4);
    addr6_out->sin6_port = addr4->sin_port;
    resolved_addr6_out->len = static_cast<socklen_t>(sizeof(grpc_sockaddr_in6));
    return 1;
  }
  return 0;
}

int grpc_sockaddr_is_wildcard(const grpc_resolved_address* resolved_addr,
                              int* port_out) {
  const grpc_sockaddr* addr;
  grpc_resolved_address addr4_normalized;
  if (grpc_sockaddr_is_v4mapped(resolved_addr, &addr4_normalized)) {
    resolved_addr = &addr4_normalized;
  }
  addr = reinterpret_cast<const grpc_sockaddr*>(resolved_addr->addr);
  if (addr->sa_family == GRPC_AF_INET) {
    /* Check for 0.0.0.0 */
    const grpc_sockaddr_in* addr4 =
        reinterpret_cast<const grpc_sockaddr_in*>(addr);
    if (addr4->sin_addr.s_addr != 0) {
      return 0;
    }
    *port_out = grpc_ntohs(addr4->sin_port);
    return 1;
  } else if (addr->sa_family == GRPC_AF_INET6) {
    /* Check for :: */
    const grpc_sockaddr_in6* addr6 =
        reinterpret_cast<const grpc_sockaddr_in6*>(addr);
    int i;
    for (i = 0; i < 16; i++) {
      if (addr6->sin6_addr.s6_addr[i] != 0) {
        return 0;
      }
    }
    *port_out = grpc_ntohs(addr6->sin6_port);
    return 1;
  } else {
    return 0;
  }
}

void grpc_sockaddr_make_wildcards(int port, grpc_resolved_address* wild4_out,
                                  grpc_resolved_address* wild6_out) {
  grpc_sockaddr_make_wildcard4(port, wild4_out);
  grpc_sockaddr_make_wildcard6(port, wild6_out);
}

void grpc_sockaddr_make_wildcard4(int port,
                                  grpc_resolved_address* resolved_wild_out) {
  grpc_sockaddr_in* wild_out =
      reinterpret_cast<grpc_sockaddr_in*>(resolved_wild_out->addr);
  GPR_ASSERT(port >= 0 && port < 65536);
  memset(resolved_wild_out, 0, sizeof(*resolved_wild_out));
  wild_out->sin_family = GRPC_AF_INET;
  wild_out->sin_port = grpc_htons(static_cast<uint16_t>(port));
  resolved_wild_out->len = static_cast<socklen_t>(sizeof(grpc_sockaddr_in));
}

void grpc_sockaddr_make_wildcard6(int port,
                                  grpc_resolved_address* resolved_wild_out) {
  grpc_sockaddr_in6* wild_out =
      reinterpret_cast<grpc_sockaddr_in6*>(resolved_wild_out->addr);
  GPR_ASSERT(port >= 0 && port < 65536);
  memset(resolved_wild_out, 0, sizeof(*resolved_wild_out));
  wild_out->sin6_family = GRPC_AF_INET6;
  wild_out->sin6_port = grpc_htons(static_cast<uint16_t>(port));
  resolved_wild_out->len = static_cast<socklen_t>(sizeof(grpc_sockaddr_in6));
}

int grpc_sockaddr_to_string(char** out,
                            const grpc_resolved_address* resolved_addr,
                            int normalize) {
  const grpc_sockaddr* addr;
  const int save_errno = errno;
  grpc_resolved_address addr_normalized;
  char ntop_buf[GRPC_INET6_ADDRSTRLEN];
  const void* ip = nullptr;
  int port = 0;
  uint32_t sin6_scope_id = 0;
  int ret;

  *out = nullptr;
  if (normalize && grpc_sockaddr_is_v4mapped(resolved_addr, &addr_normalized)) {
    resolved_addr = &addr_normalized;
  }
  addr = reinterpret_cast<const grpc_sockaddr*>(resolved_addr->addr);
  if (addr->sa_family == GRPC_AF_INET) {
    const grpc_sockaddr_in* addr4 =
        reinterpret_cast<const grpc_sockaddr_in*>(addr);
    ip = &addr4->sin_addr;
    port = grpc_ntohs(addr4->sin_port);
  } else if (addr->sa_family == GRPC_AF_INET6) {
    const grpc_sockaddr_in6* addr6 =
        reinterpret_cast<const grpc_sockaddr_in6*>(addr);
    ip = &addr6->sin6_addr;
    port = grpc_ntohs(addr6->sin6_port);
    sin6_scope_id = addr6->sin6_scope_id;
  }
  if (ip != nullptr && grpc_inet_ntop(addr->sa_family, ip, ntop_buf,
                                      sizeof(ntop_buf)) != nullptr) {
    grpc_core::UniquePtr<char> tmp_out;
    if (sin6_scope_id != 0) {
      char* host_with_scope;
      /* Enclose sin6_scope_id with the format defined in RFC 6784 section 2. */
      gpr_asprintf(&host_with_scope, "%s%%25%" PRIu32, ntop_buf, sin6_scope_id);
      ret = grpc_core::JoinHostPort(&tmp_out, host_with_scope, port);
      gpr_free(host_with_scope);
    } else {
      ret = grpc_core::JoinHostPort(&tmp_out, ntop_buf, port);
    }
    *out = tmp_out.release();
  } else {
    ret = gpr_asprintf(out, "(sockaddr family=%d)", addr->sa_family);
  }
  /* This is probably redundant, but we wouldn't want to log the wrong error. */
  errno = save_errno;
  return ret;
}

void grpc_string_to_sockaddr(grpc_resolved_address* out, char* addr, int port) {
  memset(out, 0, sizeof(grpc_resolved_address));
  grpc_sockaddr_in6* addr6 = (grpc_sockaddr_in6*)out->addr;
  grpc_sockaddr_in* addr4 = (grpc_sockaddr_in*)out->addr;
  if (grpc_inet_pton(GRPC_AF_INET6, addr, &addr6->sin6_addr) == 1) {
    addr6->sin6_family = GRPC_AF_INET6;
    out->len = sizeof(grpc_sockaddr_in6);
  } else if (grpc_inet_pton(GRPC_AF_INET, addr, &addr4->sin_addr) == 1) {
    addr4->sin_family = GRPC_AF_INET;
    out->len = sizeof(grpc_sockaddr_in);
  } else {
    GPR_ASSERT(0);
  }
  grpc_sockaddr_set_port(out, port);
}

char* grpc_sockaddr_to_uri(const grpc_resolved_address* resolved_addr) {
  if (resolved_addr->len == 0) return nullptr;
  grpc_resolved_address addr_normalized;
  if (grpc_sockaddr_is_v4mapped(resolved_addr, &addr_normalized)) {
    resolved_addr = &addr_normalized;
  }
  const char* scheme = grpc_sockaddr_get_uri_scheme(resolved_addr);
  if (scheme == nullptr || strcmp("unix", scheme) == 0) {
    return grpc_sockaddr_to_uri_unix_if_possible(resolved_addr);
  }
  char* path = nullptr;
  char* uri_str = nullptr;
  if (grpc_sockaddr_to_string(&path, resolved_addr,
                              false /* suppress errors */) &&
      scheme != nullptr) {
    gpr_asprintf(&uri_str, "%s:%s", scheme, path);
  }
  gpr_free(path);
  return uri_str != nullptr ? uri_str : nullptr;
}

const char* grpc_sockaddr_get_uri_scheme(
    const grpc_resolved_address* resolved_addr) {
  const grpc_sockaddr* addr =
      reinterpret_cast<const grpc_sockaddr*>(resolved_addr->addr);
  switch (addr->sa_family) {
    case GRPC_AF_INET:
      return "ipv4";
    case GRPC_AF_INET6:
      return "ipv6";
    case GRPC_AF_UNIX:
      return "unix";
  }
  return nullptr;
}

int grpc_sockaddr_get_family(const grpc_resolved_address* resolved_addr) {
  const grpc_sockaddr* addr =
      reinterpret_cast<const grpc_sockaddr*>(resolved_addr->addr);
  return addr->sa_family;
}

int grpc_sockaddr_get_port(const grpc_resolved_address* resolved_addr) {
  const grpc_sockaddr* addr =
      reinterpret_cast<const grpc_sockaddr*>(resolved_addr->addr);
  switch (addr->sa_family) {
    case GRPC_AF_INET:
      return grpc_ntohs(((grpc_sockaddr_in*)addr)->sin_port);
    case GRPC_AF_INET6:
      return grpc_ntohs(((grpc_sockaddr_in6*)addr)->sin6_port);
    default:
      if (grpc_is_unix_socket(resolved_addr)) {
        return 1;
      }
      gpr_log(GPR_ERROR, "Unknown socket family %d in grpc_sockaddr_get_port",
              addr->sa_family);
      return 0;
  }
}

int grpc_sockaddr_set_port(const grpc_resolved_address* resolved_addr,
                           int port) {
  const grpc_sockaddr* addr =
      reinterpret_cast<const grpc_sockaddr*>(resolved_addr->addr);
  switch (addr->sa_family) {
    case GRPC_AF_INET:
      GPR_ASSERT(port >= 0 && port < 65536);
      ((grpc_sockaddr_in*)addr)->sin_port =
          grpc_htons(static_cast<uint16_t>(port));
      return 1;
    case GRPC_AF_INET6:
      GPR_ASSERT(port >= 0 && port < 65536);
      ((grpc_sockaddr_in6*)addr)->sin6_port =
          grpc_htons(static_cast<uint16_t>(port));
      return 1;
    default:
      gpr_log(GPR_ERROR, "Unknown socket family %d in grpc_sockaddr_set_port",
              addr->sa_family);
      return 0;
  }
}
