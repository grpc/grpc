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

#include "src/core/ext/client_channel/parse_address.h"
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

#ifdef GRPC_HAVE_UNIX_SOCKET

int parse_unix(grpc_uri *uri, grpc_resolved_address *resolved_addr) {
  struct sockaddr_un *un = (struct sockaddr_un *)resolved_addr->addr;

  un->sun_family = AF_UNIX;
  strcpy(un->sun_path, uri->path);
  resolved_addr->len = strlen(un->sun_path) + sizeof(un->sun_family) + 1;

  return 1;
}

#else /* GRPC_HAVE_UNIX_SOCKET */

int parse_unix(grpc_uri *uri, grpc_resolved_address *resolved_addr) { abort(); }

#endif /* GRPC_HAVE_UNIX_SOCKET */

int parse_ipv4(grpc_uri *uri, grpc_resolved_address *resolved_addr) {
  const char *host_port = uri->path;
  char *host;
  char *port;
  int port_num;
  int result = 0;
  struct sockaddr_in *in = (struct sockaddr_in *)resolved_addr->addr;

  if (*host_port == '/') ++host_port;
  if (!gpr_split_host_port(host_port, &host, &port)) {
    return 0;
  }

  memset(resolved_addr, 0, sizeof(grpc_resolved_address));
  resolved_addr->len = sizeof(struct sockaddr_in);
  in->sin_family = AF_INET;
  if (inet_pton(AF_INET, host, &in->sin_addr) == 0) {
    gpr_log(GPR_ERROR, "invalid ipv4 address: '%s'", host);
    goto done;
  }

  if (port != NULL) {
    if (sscanf(port, "%d", &port_num) != 1 || port_num < 0 ||
        port_num > 65535) {
      gpr_log(GPR_ERROR, "invalid ipv4 port: '%s'", port);
      goto done;
    }
    in->sin_port = htons((uint16_t)port_num);
  } else {
    gpr_log(GPR_ERROR, "no port given for ipv4 scheme");
    goto done;
  }

  result = 1;
done:
  gpr_free(host);
  gpr_free(port);
  return result;
}

int parse_ipv6(grpc_uri *uri, grpc_resolved_address *resolved_addr) {
  const char *host_port = uri->path;
  char *host;
  char *port;
  int port_num;
  int result = 0;
  struct sockaddr_in6 *in6 = (struct sockaddr_in6 *)resolved_addr->addr;

  if (*host_port == '/') ++host_port;
  if (!gpr_split_host_port(host_port, &host, &port)) {
    return 0;
  }

  memset(in6, 0, sizeof(*in6));
  resolved_addr->len = sizeof(*in6);
  in6->sin6_family = AF_INET6;
  if (inet_pton(AF_INET6, host, &in6->sin6_addr) == 0) {
    gpr_log(GPR_ERROR, "invalid ipv6 address: '%s'", host);
    goto done;
  }

  if (port != NULL) {
    if (sscanf(port, "%d", &port_num) != 1 || port_num < 0 ||
        port_num > 65535) {
      gpr_log(GPR_ERROR, "invalid ipv6 port: '%s'", port);
      goto done;
    }
    in6->sin6_port = htons((uint16_t)port_num);
  } else {
    gpr_log(GPR_ERROR, "no port given for ipv6 scheme");
    goto done;
  }

  result = 1;
done:
  gpr_free(host);
  gpr_free(port);
  return result;
}
