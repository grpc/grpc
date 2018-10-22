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

#include "src/core/lib/iomgr/port.h"
#if GRPC_ARES == 1 && defined(GPR_WINDOWS)

#include <grpc/support/string_util.h>

#include "src/core/ext/filters/client_channel/lb_policy_factory.h"
#include "src/core/ext/filters/client_channel/parse_address.h"
#include "src/core/ext/filters/client_channel/resolver/dns/c_ares/grpc_ares_wrapper.h"
#include "src/core/lib/gpr/host_port.h"
#include "src/core/lib/gpr/string.h"
#include "src/core/lib/iomgr/socket_windows.h"

bool grpc_ares_query_ipv6() { return grpc_ipv6_loopback_available(); }

static bool inner_maybe_resolve_localhost_manually_locked(
    const char* name, const char* default_port, grpc_lb_addresses** addrs,
    char** host, char** port) {
  gpr_split_host_port(name, host, port);
  if (*host == nullptr) {
    gpr_log(GPR_ERROR,
            "Failed to parse %s into host:port during Windows localhost "
            "resolution check.",
            name);
    return false;
  }
  if (*port == nullptr) {
    if (default_port == nullptr) {
      gpr_log(GPR_ERROR,
              "No port or default port for %s during Windows localhost "
              "resolution check.",
              name);
      return false;
    }
    *port = gpr_strdup(default_port);
  }
  if (gpr_stricmp(*host, "localhost") == 0) {
    GPR_ASSERT(*addrs == nullptr);
    *addrs = grpc_lb_addresses_create(2, nullptr);
    uint16_t numeric_port = grpc_strhtons(*port);
    // Append the ipv6 loopback address.
    struct sockaddr_in6 ipv6_loopback_addr;
    memset(&ipv6_loopback_addr, 0, sizeof(ipv6_loopback_addr));
    ((char*)&ipv6_loopback_addr.sin6_addr)[15] = 1;
    ipv6_loopback_addr.sin6_family = AF_INET6;
    ipv6_loopback_addr.sin6_port = numeric_port;
    grpc_lb_addresses_set_address(
        *addrs, 0, &ipv6_loopback_addr, sizeof(ipv6_loopback_addr),
        false /* is_balancer */, nullptr /* balancer_name */,
        nullptr /* user_data */);
    // Append the ipv4 loopback address.
    struct sockaddr_in ipv4_loopback_addr;
    memset(&ipv4_loopback_addr, 0, sizeof(ipv4_loopback_addr));
    ((char*)&ipv4_loopback_addr.sin_addr)[0] = 0x7f;
    ((char*)&ipv4_loopback_addr.sin_addr)[3] = 0x01;
    ipv4_loopback_addr.sin_family = AF_INET;
    ipv4_loopback_addr.sin_port = numeric_port;
    grpc_lb_addresses_set_address(
        *addrs, 1, &ipv4_loopback_addr, sizeof(ipv4_loopback_addr),
        false /* is_balancer */, nullptr /* balancer_name */,
        nullptr /* user_data */);
    // Let the address sorter figure out which one should be tried first.
    grpc_cares_wrapper_address_sorting_sort(*addrs);
    return true;
  }
  return false;
}

bool grpc_ares_maybe_resolve_localhost_manually_locked(
    const char* name, const char* default_port, grpc_lb_addresses** addrs) {
  char* host = nullptr;
  char* port = nullptr;
  bool out = inner_maybe_resolve_localhost_manually_locked(name, default_port,
                                                           addrs, &host, &port);
  gpr_free(host);
  gpr_free(port);
  return out;
}

#endif /* GRPC_ARES == 1 && defined(GPR_WINDOWS) */
