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
#if GRPC_ARES == 1 && defined(GRPC_WINDOWS_SOCKET_ARES_EV_DRIVER)

#include "absl/strings/str_cat.h"

#include <grpc/support/string_util.h>

#include "src/core/ext/filters/client_channel/resolver/dns/c_ares/grpc_ares_wrapper.h"
#include "src/core/ext/filters/client_channel/server_address.h"
#include "src/core/lib/gpr/string.h"
#include "src/core/lib/iomgr/block_annotate.h"
#include "src/core/lib/iomgr/error.h"
#include "src/core/lib/iomgr/parse_address.h"
#include "src/core/lib/iomgr/socket_windows.h"

bool grpc_ares_query_ipv6() { return grpc_ipv6_loopback_available(); }

grpc_error* grpc_ares_getaddrinfo(std::string port, struct addrinfo* hints,
                                  struct addrinfo** result) {
  GRPC_SCHEDULING_START_BLOCKING_REGION;
  int s = getaddrinfo(nullptr, port.c_str(), hints, result);
  GRPC_SCHEDULING_END_BLOCKING_REGION;
  if (s != 0) {
    return GRPC_WSA_ERROR(WSAGetLastError(), "getaddrinfo");
  }
  return GRPC_ERROR_NONE;
}

#endif /* GRPC_ARES == 1 && defined(GRPC_WINDOWS_SOCKET_ARES_EV_DRIVER) */
