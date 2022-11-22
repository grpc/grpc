// Copyright 2022 gRPC authors.
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
#include <grpc/support/port_platform.h>

#ifdef GPR_WINDOWS

#include <errno.h>
#include <inttypes.h>
#include <winsock2.h>

#include "absl/strings/str_format.h"

#include "src/core/lib/event_engine/windows/resolved_address.h"
#include "src/core/lib/gprpp/host_port.h"
#include "src/core/lib/gprpp/status_helper.h"
#include "src/core/lib/uri/uri_parser.h"

namespace grpc_event_engine {
namespace experimental {

namespace {
absl::StatusOr<std::string> GetScheme(
    const EventEngine::ResolvedAddress& resolved_address) {
  if (resolved_address.address()->sa_family == AF_INET) return "ipv4";
  if (resolved_address.address()->sa_family == AF_INET6) return "ipv6";
  return absl::InvalidArgumentError(absl::StrFormat(
      "Unknown scheme: %d", resolved_address.address()->sa_family));
}
}  // namespace

absl::StatusOr<std::string> ResolvedAddressToURI(
    const EventEngine::ResolvedAddress& resolved_address) {
  if (resolved_address.size() == 0) {
    return absl::InvalidArgumentError("Empty address");
  }
  auto scheme = GetScheme(resolved_address);
  GRPC_RETURN_IF_ERROR(scheme.status());
  auto path = ResolvedAddressToString(resolved_address);
  GRPC_RETURN_IF_ERROR(path.status());
  absl::StatusOr<grpc_core::URI> uri =
      grpc_core::URI::Create(*scheme, /*authority=*/"", std::move(path.value()),
                             /*query_parameter_pairs=*/{}, /*fragment=*/"");
  if (!uri.ok()) return uri.status();
  return uri->ToString();
}

// TODO(hork): implement normalization
absl::StatusOr<std::string> ResolvedAddressToString(
    const EventEngine::ResolvedAddress& resolved_address) {
  const sockaddr* addr = resolved_address.address();
  const void* ip = nullptr;
  int port = 0;
  uint32_t sin6_scope_id = 0;
  if (addr->sa_family == AF_INET) {
    const sockaddr_in* addr4 = reinterpret_cast<const sockaddr_in*>(addr);
    ip = &addr4->sin_addr;
    port = ntohs(addr4->sin_port);
  } else if (addr->sa_family == AF_INET6) {
    const sockaddr_in6* addr6 = reinterpret_cast<const sockaddr_in6*>(addr);
    ip = &addr6->sin6_addr;
    port = ntohs(addr6->sin6_port);
    sin6_scope_id = addr6->sin6_scope_id;
  }
  char ntop_buf[INET6_ADDRSTRLEN];
  std::string out;
  if (ip != nullptr &&
      inet_ntop(addr->sa_family, ip, ntop_buf, sizeof(ntop_buf)) != nullptr) {
    if (sin6_scope_id != 0) {
      // Enclose sin6_scope_id with the format defined in RFC 6874 section 2.
      std::string host_with_scope =
          absl::StrFormat("%s%%%" PRIu32, ntop_buf, sin6_scope_id);
      out = grpc_core::JoinHostPort(host_with_scope, port);
    } else {
      out = grpc_core::JoinHostPort(ntop_buf, port);
    }
  } else {
    return absl::InvalidArgumentError(
        absl::StrCat("Unknown sockaddr family: ", addr->sa_family));
  }
  return out;
}

}  // namespace experimental
}  // namespace grpc_event_engine

#endif  // GPR_WINDOWS
