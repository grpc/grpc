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

#include "absl/strings/str_cat.h"

#include "src/core/lib/iomgr/grpc_if_nametoindex.h"
#include "src/core/lib/iomgr/parse_address.h"
#include "src/core/lib/iomgr/sockaddr.h"
#include "src/core/lib/iomgr/socket_utils.h"

#include <stdio.h>
#include <string.h>
#ifdef GRPC_HAVE_UNIX_SOCKET
#include <sys/un.h>
#endif

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>

#include "src/core/lib/gpr/string.h"
#include "src/core/lib/gprpp/host_port.h"

#ifdef GRPC_POSIX_SOCKET
#include <errno.h>
#include <net/if.h>
#endif

#ifdef GRPC_HAVE_UNIX_SOCKET

bool grpc_parse_unix(const grpc_uri* uri,
                     grpc_resolved_address* resolved_addr) {
  if (strcmp("unix", uri->scheme) != 0) {
    gpr_log(GPR_ERROR, "Expected 'unix' scheme, got '%s'", uri->scheme);
    return false;
  }
  grpc_error* error = grpc_core::UnixSockaddrPopulate(uri->path, resolved_addr);
  if (error != GRPC_ERROR_NONE) {
    gpr_log(GPR_ERROR, "%s", grpc_error_string(error));
    GRPC_ERROR_UNREF(error);
    return false;
  }
  return true;
}

bool grpc_parse_unix_abstract(const grpc_uri* uri,
                              grpc_resolved_address* resolved_addr) {
  if (strcmp("unix-abstract", uri->scheme) != 0) {
    gpr_log(GPR_ERROR, "Expected 'unix-abstract' scheme, got '%s'",
            uri->scheme);
    return false;
  }
  grpc_error* error =
      grpc_core::UnixAbstractSockaddrPopulate(uri->path, resolved_addr);
  if (error != GRPC_ERROR_NONE) {
    gpr_log(GPR_ERROR, "%s", grpc_error_string(error));
    GRPC_ERROR_UNREF(error);
    return false;
  }
  return true;
}

namespace grpc_core {

grpc_error* UnixSockaddrPopulate(absl::string_view path,
                                 grpc_resolved_address* resolved_addr) {
  struct sockaddr_un* un =
      reinterpret_cast<struct sockaddr_un*>(resolved_addr->addr);
  const size_t maxlen = sizeof(un->sun_path) - 1;
  if (path.size() > maxlen) {
    return GRPC_ERROR_CREATE_FROM_COPIED_STRING(
        absl::StrCat("Path name should not have more than ", maxlen,
                     " characters")
            .c_str());
  }
  un->sun_family = AF_UNIX;
  path.copy(un->sun_path, path.size());
  un->sun_path[path.size()] = '\0';
  resolved_addr->len = static_cast<socklen_t>(sizeof(*un));
  return GRPC_ERROR_NONE;
}

grpc_error* UnixAbstractSockaddrPopulate(absl::string_view path,
                                         grpc_resolved_address* resolved_addr) {
  struct sockaddr_un* un =
      reinterpret_cast<struct sockaddr_un*>(resolved_addr->addr);
  const size_t maxlen = sizeof(un->sun_path) - 1;
  if (path.size() > maxlen) {
    return GRPC_ERROR_CREATE_FROM_COPIED_STRING(
        absl::StrCat("Path name should not have more than ", maxlen,
                     " characters")
            .c_str());
  }
  un->sun_family = AF_UNIX;
  un->sun_path[0] = '\0';
  path.copy(un->sun_path + 1, path.size());
  resolved_addr->len =
      static_cast<socklen_t>(sizeof(un->sun_family) + path.size() + 1);
  return GRPC_ERROR_NONE;
}

}  // namespace grpc_core

#else  /* GRPC_HAVE_UNIX_SOCKET */

bool grpc_parse_unix(const grpc_uri* uri,
                     grpc_resolved_address* resolved_addr) {
  abort();
}

bool grpc_parse_unix_abstract(const grpc_uri* uri,
                              grpc_resolved_address* resolved_addr) {
  abort();
}

namespace grpc_core {

grpc_error* UnixSockaddrPopulate(absl::string_view path,
                                 grpc_resolved_address* resolved_addr) {
  abort();
}

grpc_error* UnixAbstractSockaddrPopulate(absl::string_view path,
                                         grpc_resolved_address* resolved_addr) {
  abort();
}

}  // namespace grpc_core
#endif /* GRPC_HAVE_UNIX_SOCKET */

bool grpc_parse_ipv4_hostport(const char* hostport, grpc_resolved_address* addr,
                              bool log_errors) {
  bool success = false;
  // Split host and port.
  std::string host;
  std::string port;
  if (!grpc_core::SplitHostPort(hostport, &host, &port)) {
    if (log_errors) {
      gpr_log(GPR_ERROR, "Failed gpr_split_host_port(%s, ...)", hostport);
    }
    return false;
  }
  // Parse IP address.
  memset(addr, 0, sizeof(*addr));
  addr->len = static_cast<socklen_t>(sizeof(grpc_sockaddr_in));
  grpc_sockaddr_in* in = reinterpret_cast<grpc_sockaddr_in*>(addr->addr);
  in->sin_family = GRPC_AF_INET;
  if (grpc_inet_pton(GRPC_AF_INET, host.c_str(), &in->sin_addr) == 0) {
    if (log_errors) {
      gpr_log(GPR_ERROR, "invalid ipv4 address: '%s'", host.c_str());
    }
    goto done;
  }
  // Parse port.
  if (port.empty()) {
    if (log_errors) gpr_log(GPR_ERROR, "no port given for ipv4 scheme");
    goto done;
  }
  int port_num;
  if (sscanf(port.c_str(), "%d", &port_num) != 1 || port_num < 0 ||
      port_num > 65535) {
    if (log_errors) gpr_log(GPR_ERROR, "invalid ipv4 port: '%s'", port.c_str());
    goto done;
  }
  in->sin_port = grpc_htons(static_cast<uint16_t>(port_num));
  success = true;
done:
  return success;
}

bool grpc_parse_ipv4(const grpc_uri* uri,
                     grpc_resolved_address* resolved_addr) {
  if (strcmp("ipv4", uri->scheme) != 0) {
    gpr_log(GPR_ERROR, "Expected 'ipv4' scheme, got '%s'", uri->scheme);
    return false;
  }
  const char* host_port = uri->path;
  if (*host_port == '/') ++host_port;
  return grpc_parse_ipv4_hostport(host_port, resolved_addr,
                                  true /* log_errors */);
}

bool grpc_parse_ipv6_hostport(const char* hostport, grpc_resolved_address* addr,
                              bool log_errors) {
  bool success = false;
  // Split host and port.
  std::string host;
  std::string port;
  if (!grpc_core::SplitHostPort(hostport, &host, &port)) {
    if (log_errors) {
      gpr_log(GPR_ERROR, "Failed gpr_split_host_port(%s, ...)", hostport);
    }
    return false;
  }
  // Parse IP address.
  memset(addr, 0, sizeof(*addr));
  addr->len = static_cast<socklen_t>(sizeof(grpc_sockaddr_in6));
  grpc_sockaddr_in6* in6 = reinterpret_cast<grpc_sockaddr_in6*>(addr->addr);
  in6->sin6_family = GRPC_AF_INET6;
  // Handle the RFC6874 syntax for IPv6 zone identifiers.
  char* host_end =
      static_cast<char*>(gpr_memrchr(host.c_str(), '%', host.size()));
  if (host_end != nullptr) {
    GPR_ASSERT(host_end >= host.c_str());
    char host_without_scope[GRPC_INET6_ADDRSTRLEN + 1];
    size_t host_without_scope_len =
        static_cast<size_t>(host_end - host.c_str());
    uint32_t sin6_scope_id = 0;
    if (host_without_scope_len > GRPC_INET6_ADDRSTRLEN) {
      if (log_errors) {
        gpr_log(
            GPR_ERROR,
            "invalid ipv6 address length %zu. Length cannot be greater than "
            "GRPC_INET6_ADDRSTRLEN i.e %d)",
            host_without_scope_len, GRPC_INET6_ADDRSTRLEN);
      }
      goto done;
    }
    strncpy(host_without_scope, host.c_str(), host_without_scope_len);
    host_without_scope[host_without_scope_len] = '\0';
    if (grpc_inet_pton(GRPC_AF_INET6, host_without_scope, &in6->sin6_addr) ==
        0) {
      if (log_errors) {
        gpr_log(GPR_ERROR, "invalid ipv6 address: '%s'", host_without_scope);
      }
      goto done;
    }
    if (gpr_parse_bytes_to_uint32(host_end + 1,
                                  host.size() - host_without_scope_len - 1,
                                  &sin6_scope_id) == 0) {
      if ((sin6_scope_id = grpc_if_nametoindex(host_end + 1)) == 0) {
        gpr_log(GPR_ERROR,
                "Invalid interface name: '%s'. "
                "Non-numeric and failed if_nametoindex.",
                host_end + 1);
        goto done;
      }
    }
    // Handle "sin6_scope_id" being type "u_long". See grpc issue #10027.
    in6->sin6_scope_id = sin6_scope_id;
  } else {
    if (grpc_inet_pton(GRPC_AF_INET6, host.c_str(), &in6->sin6_addr) == 0) {
      if (log_errors) {
        gpr_log(GPR_ERROR, "invalid ipv6 address: '%s'", host.c_str());
      }
      goto done;
    }
  }
  // Parse port.
  if (port.empty()) {
    if (log_errors) gpr_log(GPR_ERROR, "no port given for ipv6 scheme");
    goto done;
  }
  int port_num;
  if (sscanf(port.c_str(), "%d", &port_num) != 1 || port_num < 0 ||
      port_num > 65535) {
    if (log_errors) gpr_log(GPR_ERROR, "invalid ipv6 port: '%s'", port.c_str());
    goto done;
  }
  in6->sin6_port = grpc_htons(static_cast<uint16_t>(port_num));
  success = true;
done:
  return success;
}

bool grpc_parse_ipv6(const grpc_uri* uri,
                     grpc_resolved_address* resolved_addr) {
  if (strcmp("ipv6", uri->scheme) != 0) {
    gpr_log(GPR_ERROR, "Expected 'ipv6' scheme, got '%s'", uri->scheme);
    return false;
  }
  const char* host_port = uri->path;
  if (*host_port == '/') ++host_port;
  return grpc_parse_ipv6_hostport(host_port, resolved_addr,
                                  true /* log_errors */);
}

bool grpc_parse_uri(const grpc_uri* uri, grpc_resolved_address* resolved_addr) {
  if (strcmp("unix", uri->scheme) == 0) {
    return grpc_parse_unix(uri, resolved_addr);
  } else if (strcmp("unix-abstract", uri->scheme) == 0) {
    return grpc_parse_unix_abstract(uri, resolved_addr);
  } else if (strcmp("ipv4", uri->scheme) == 0) {
    return grpc_parse_ipv4(uri, resolved_addr);
  } else if (strcmp("ipv6", uri->scheme) == 0) {
    return grpc_parse_ipv6(uri, resolved_addr);
  }
  gpr_log(GPR_ERROR, "Can't parse scheme '%s'", uri->scheme);
  return false;
}

uint16_t grpc_strhtons(const char* port) {
  if (strcmp(port, "http") == 0) {
    return htons(80);
  } else if (strcmp(port, "https") == 0) {
    return htons(443);
  }
  return htons(static_cast<unsigned short>(atoi(port)));
}
