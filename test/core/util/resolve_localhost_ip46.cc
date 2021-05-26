//
//
// Copyright 2020 gRPC authors.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
//

#include "test/core/util/resolve_localhost_ip46.h"

#include <grpc/support/log.h>

#include "src/core/lib/iomgr/port.h"
#include "src/core/lib/iomgr/resolve_address.h"
#include "src/core/lib/iomgr/sockaddr.h"

namespace grpc_core {
namespace {

bool localhost_to_ipv4 = false;
bool localhost_to_ipv6 = false;
gpr_once g_resolve_localhost_ipv46 = GPR_ONCE_INIT;

void InitResolveLocalhost() {
  grpc_resolved_addresses* addresses;
  grpc_error_handle err =
      grpc_blocking_resolve_address("localhost", "https", &addresses);
  GPR_ASSERT(err == GRPC_ERROR_NONE);
  for (size_t i = 0; i < addresses->naddrs; i++) {
    grpc_sockaddr* addr =
        reinterpret_cast<grpc_sockaddr*>(addresses->addrs[i].addr);
    if (addr->sa_family == GRPC_AF_INET) {
      localhost_to_ipv4 = true;
    } else if (addr->sa_family == GRPC_AF_INET6) {
      localhost_to_ipv6 = true;
    }
  }
  grpc_resolved_addresses_destroy(addresses);
}
}  // namespace

void LocalhostResolves(bool* ipv4, bool* ipv6) {
  gpr_once_init(&g_resolve_localhost_ipv46, InitResolveLocalhost);
  *ipv4 = localhost_to_ipv4;
  *ipv6 = localhost_to_ipv6;
}

}  // namespace grpc_core
