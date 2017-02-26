/*
 *
 * Copyright 2016, gRPC authors
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
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
