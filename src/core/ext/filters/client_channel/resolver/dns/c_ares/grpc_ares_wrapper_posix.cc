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
#if GRPC_ARES == 1 && defined(GRPC_POSIX_SOCKET_ARES_EV_DRIVER)

#include "absl/strings/str_cat.h"

#include "src/core/ext/filters/client_channel/resolver/dns/c_ares/grpc_ares_wrapper.h"
#include "src/core/lib/iomgr/block_annotate.h"
#include "src/core/lib/iomgr/sockaddr.h"
#include "src/core/lib/iomgr/sockaddr_utils.h"
#include "src/core/lib/iomgr/socket_utils_posix.h"

bool grpc_ares_query_ipv6() { return grpc_ipv6_loopback_available(); }

grpc_error* grpc_ares_getaddrinfo(std::string port, struct addrinfo* hints,
                                  struct addrinfo** result) {
  GRPC_SCHEDULING_START_BLOCKING_REGION;
  int s = getaddrinfo(nullptr, port.c_str(), hints, result);
  GRPC_SCHEDULING_END_BLOCKING_REGION;
  if (s != 0) {
    return GRPC_ERROR_CREATE_FROM_COPIED_STRING(
        absl::StrCat("getaddrinfo(nullptr, ", port,
                     ", ...) failed return val: ", std::to_string(s),
                     " gai_strerror: ", gai_strerror(s))
            .c_str());
  }
  return GRPC_ERROR_NONE;
}

#endif /* GRPC_ARES == 1 && defined(GRPC_POSIX_SOCKET_ARES_EV_DRIVER) */
