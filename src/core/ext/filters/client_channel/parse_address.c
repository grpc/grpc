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

#include "src/core/ext/filters/client_channel/parse_address.h"
#include "src/core/lib/iomgr/sockaddr.h"

#include <stdio.h>
#include <string.h>
#ifdef GRPC_HAVE_UNIX_SOCKET
#include <sys/un.h>
#endif

#include <grpc/support/alloc.h>
#include <grpc/support/host_port.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>
#include "src/core/lib/support/string.h"

#ifdef GRPC_HAVE_UNIX_SOCKET

bool grpc_parse_unix(const grpc_uri *uri,
                     grpc_resolved_address *resolved_addr) {
  if (strcmp("unix", uri->scheme) != 0) {
    gpr_log(GPR_ERROR, "Expected 'unix' scheme, got '%s'", uri->scheme);
    return false;
  }
  struct sockaddr_un *un = (struct sockaddr_un *)resolved_addr->addr;
  const size_t maxlen = sizeof(un->sun_path);
  const size_t path_len = strnlen(uri->path, maxlen);
  if (path_len == maxlen) return false;
  un->sun_family = AF_UNIX;
  strcpy(un->sun_path, uri->path);
  resolved_addr->len = sizeof(*un);
  return true;
}

#else /* GRPC_HAVE_UNIX_SOCKET */

bool grpc_parse_unix(const grpc_uri *uri,
                     grpc_resolved_address *resolved_addr) {
  abort();
}

#endif /* GRPC_HAVE_UNIX_SOCKET */

bool grpc_parse_ipv4_hostport(const char *hostport, grpc_resolved_address *addr,
                              bool log_errors) {
  bool success = false;
  // Split host and port.
  char *host;
  char *port;
  if (!gpr_split_host_port(hostport, &host, &port)) return false;
  // Parse IP address.
  memset(addr, 0, sizeof(*addr));
  addr->len = sizeof(struct sockaddr_in);
  struct sockaddr_in *in = (struct sockaddr_in *)addr->addr;
  in->sin_family = AF_INET;
  if (inet_pton(AF_INET, host, &in->sin_addr) == 0) {
    if (log_errors) gpr_log(GPR_ERROR, "invalid ipv4 address: '%s'", host);
    goto done;
  }
  // Parse port.
  if (port == NULL) {
    if (log_errors) gpr_log(GPR_ERROR, "no port given for ipv4 scheme");
    goto done;
  }
  int port_num;
  if (sscanf(port, "%d", &port_num) != 1 || port_num < 0 || port_num > 65535) {
    if (log_errors) gpr_log(GPR_ERROR, "invalid ipv4 port: '%s'", port);
    goto done;
  }
  in->sin_port = htons((uint16_t)port_num);
  success = true;
done:
  gpr_free(host);
  gpr_free(port);
  return success;
}

bool grpc_parse_ipv4(const grpc_uri *uri,
                     grpc_resolved_address *resolved_addr) {
  if (strcmp("ipv4", uri->scheme) != 0) {
    gpr_log(GPR_ERROR, "Expected 'ipv4' scheme, got '%s'", uri->scheme);
    return false;
  }
  const char *host_port = uri->path;
  if (*host_port == '/') ++host_port;
  return grpc_parse_ipv4_hostport(host_port, resolved_addr,
                                  true /* log_errors */);
}

bool grpc_parse_ipv6_hostport(const char *hostport, grpc_resolved_address *addr,
                              bool log_errors) {
  bool success = false;
  // Split host and port.
  char *host;
  char *port;
  if (!gpr_split_host_port(hostport, &host, &port)) return false;
  // Parse IP address.
  memset(addr, 0, sizeof(*addr));
  addr->len = sizeof(struct sockaddr_in6);
  struct sockaddr_in6 *in6 = (struct sockaddr_in6 *)addr->addr;
  in6->sin6_family = AF_INET6;
  // Handle the RFC6874 syntax for IPv6 zone identifiers.
  char *host_end = (char *)gpr_memrchr(host, '%', strlen(host));
  if (host_end != NULL) {
    GPR_ASSERT(host_end >= host);
    char host_without_scope[INET6_ADDRSTRLEN];
    size_t host_without_scope_len = (size_t)(host_end - host);
    uint32_t sin6_scope_id = 0;
    strncpy(host_without_scope, host, host_without_scope_len);
    host_without_scope[host_without_scope_len] = '\0';
    if (inet_pton(AF_INET6, host_without_scope, &in6->sin6_addr) == 0) {
      gpr_log(GPR_ERROR, "invalid ipv6 address: '%s'", host_without_scope);
      goto done;
    }
    if (gpr_parse_bytes_to_uint32(host_end + 1,
                                  strlen(host) - host_without_scope_len - 1,
                                  &sin6_scope_id) == 0) {
      gpr_log(GPR_ERROR, "invalid ipv6 scope id: '%s'", host_end + 1);
      goto done;
    }
    // Handle "sin6_scope_id" being type "u_long". See grpc issue #10027.
    in6->sin6_scope_id = sin6_scope_id;
  } else {
    if (inet_pton(AF_INET6, host, &in6->sin6_addr) == 0) {
      gpr_log(GPR_ERROR, "invalid ipv6 address: '%s'", host);
      goto done;
    }
  }
  // Parse port.
  if (port == NULL) {
    if (log_errors) gpr_log(GPR_ERROR, "no port given for ipv6 scheme");
    goto done;
  }
  int port_num;
  if (sscanf(port, "%d", &port_num) != 1 || port_num < 0 || port_num > 65535) {
    if (log_errors) gpr_log(GPR_ERROR, "invalid ipv6 port: '%s'", port);
    goto done;
  }
  in6->sin6_port = htons((uint16_t)port_num);
  success = true;
done:
  gpr_free(host);
  gpr_free(port);
  return success;
}

bool grpc_parse_ipv6(const grpc_uri *uri,
                     grpc_resolved_address *resolved_addr) {
  if (strcmp("ipv6", uri->scheme) != 0) {
    gpr_log(GPR_ERROR, "Expected 'ipv6' scheme, got '%s'", uri->scheme);
    return false;
  }
  const char *host_port = uri->path;
  if (*host_port == '/') ++host_port;
  return grpc_parse_ipv6_hostport(host_port, resolved_addr,
                                  true /* log_errors */);
}

bool grpc_parse_uri(const grpc_uri *uri, grpc_resolved_address *resolved_addr) {
  if (strcmp("unix", uri->scheme) == 0) {
    return grpc_parse_unix(uri, resolved_addr);
  } else if (strcmp("ipv4", uri->scheme) == 0) {
    return grpc_parse_ipv4(uri, resolved_addr);
  } else if (strcmp("ipv6", uri->scheme) == 0) {
    return grpc_parse_ipv6(uri, resolved_addr);
  }
  gpr_log(GPR_ERROR, "Can't parse scheme '%s'", uri->scheme);
  return false;
}
