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

#include "src/core/lib/event_engine/tcp_socket_utils.h"

#include "src/core/lib/iomgr/port.h"

#ifdef GRPC_POSIX_SOCKET_UTILS_COMMON
#include <arpa/inet.h>  // IWYU pragma: keep

#ifdef GRPC_LINUX_TCP_H
#include <linux/tcp.h>
#else
#include <netinet/in.h>  // IWYU pragma: keep
#include <netinet/tcp.h>
#endif
#include <fcntl.h>
#include <sys/socket.h>
#include <unistd.h>
#endif  //  GRPC_POSIX_SOCKET_UTILS_COMMON

#ifdef GRPC_HAVE_UNIX_SOCKET
#include <sys/stat.h>  // IWYU pragma: keep
#include <sys/un.h>
#endif

#include <errno.h>
#include <inttypes.h>
#include <stdlib.h>
#include <string.h>

#include <utility>

#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"

#include <grpc/support/log.h>

#include "src/core/lib/gprpp/host_port.h"
#include "src/core/lib/gprpp/status_helper.h"
#include "src/core/lib/iomgr/sockaddr.h"
#include "src/core/lib/uri/uri_parser.h"

namespace grpc_event_engine {
namespace experimental {

namespace {
constexpr uint8_t kV4MappedPrefix[] = {0, 0, 0, 0, 0,    0,
                                       0, 0, 0, 0, 0xff, 0xff};
absl::StatusOr<std::string> GetScheme(
    const EventEngine::ResolvedAddress& resolved_address) {
  switch (resolved_address.address()->sa_family) {
    case AF_INET:
      return "ipv4";
    case AF_INET6:
      return "ipv6";
    case AF_UNIX:
      return "unix";
    default:
      return absl::InvalidArgumentError(absl::StrFormat(
          "Unknown scheme: %d", resolved_address.address()->sa_family));
  }
}
}  // namespace

bool ResolvedAddressIsV4Mapped(
    const EventEngine::ResolvedAddress& resolved_addr,
    EventEngine::ResolvedAddress* resolved_addr4_out) {
  const sockaddr* addr = resolved_addr.address();
  if (addr->sa_family == AF_INET6) {
    const sockaddr_in6* addr6 = reinterpret_cast<const sockaddr_in6*>(addr);
    sockaddr_in* addr4_out =
        resolved_addr4_out == nullptr
            ? nullptr
            : reinterpret_cast<sockaddr_in*>(
                  const_cast<sockaddr*>(resolved_addr4_out->address()));

    if (memcmp(addr6->sin6_addr.s6_addr, kV4MappedPrefix,
               sizeof(kV4MappedPrefix)) == 0) {
      if (resolved_addr4_out != nullptr) {
        // Normalize ::ffff:0.0.0.0/96 to IPv4.
        memset(addr4_out, 0, sizeof(sockaddr_in));
        addr4_out->sin_family = AF_INET;
        // s6_addr32 would be nice, but it's non-standard.
        memcpy(&addr4_out->sin_addr, &addr6->sin6_addr.s6_addr[12], 4);
        addr4_out->sin_port = addr6->sin6_port;
        *resolved_addr4_out = EventEngine::ResolvedAddress(
            reinterpret_cast<sockaddr*>(addr4_out),
            static_cast<socklen_t>(sizeof(sockaddr_in)));
      }
      return true;
    }
  }
  return false;
}

bool ResolvedAddressToV4Mapped(
    const EventEngine::ResolvedAddress& resolved_addr,
    EventEngine::ResolvedAddress* resolved_addr6_out) {
  GPR_ASSERT(&resolved_addr != resolved_addr6_out);
  const sockaddr* addr = resolved_addr.address();
  sockaddr_in6* addr6_out = const_cast<sockaddr_in6*>(
      reinterpret_cast<const sockaddr_in6*>(resolved_addr6_out->address()));
  if (addr->sa_family == AF_INET) {
    const sockaddr_in* addr4 = reinterpret_cast<const sockaddr_in*>(addr);
    memset(resolved_addr6_out, 0, sizeof(*resolved_addr6_out));
    addr6_out->sin6_family = AF_INET6;
    memcpy(&addr6_out->sin6_addr.s6_addr[0], kV4MappedPrefix, 12);
    memcpy(&addr6_out->sin6_addr.s6_addr[12], &addr4->sin_addr, 4);
    addr6_out->sin6_port = addr4->sin_port;
    *resolved_addr6_out = EventEngine::ResolvedAddress(
        reinterpret_cast<sockaddr*>(addr6_out),
        static_cast<socklen_t>(sizeof(sockaddr_in6)));
    return true;
  }
  return false;
}

EventEngine::ResolvedAddress ResolvedAddressMakeWild6(int port) {
  EventEngine::ResolvedAddress resolved_wild_out;
  sockaddr_in6* wild_out = reinterpret_cast<sockaddr_in6*>(
      const_cast<sockaddr*>(resolved_wild_out.address()));
  GPR_ASSERT(port >= 0 && port < 65536);
  memset(wild_out, 0, sizeof(sockaddr_in6));
  wild_out->sin6_family = AF_INET6;
  wild_out->sin6_port = htons(static_cast<uint16_t>(port));
  return EventEngine::ResolvedAddress(
      reinterpret_cast<sockaddr*>(wild_out),
      static_cast<socklen_t>(sizeof(sockaddr_in6)));
}

EventEngine::ResolvedAddress ResolvedAddressMakeWild4(int port) {
  EventEngine::ResolvedAddress resolved_wild_out;
  sockaddr_in* wild_out = reinterpret_cast<sockaddr_in*>(
      const_cast<sockaddr*>(resolved_wild_out.address()));
  GPR_ASSERT(port >= 0 && port < 65536);
  memset(wild_out, 0, sizeof(sockaddr_in));
  wild_out->sin_family = AF_INET;
  wild_out->sin_port = htons(static_cast<uint16_t>(port));
  return EventEngine::ResolvedAddress(
      reinterpret_cast<sockaddr*>(wild_out),
      static_cast<socklen_t>(sizeof(sockaddr_in)));
}

int ResolvedAddressGetPort(const EventEngine::ResolvedAddress& resolved_addr) {
  const sockaddr* addr = resolved_addr.address();
  switch (addr->sa_family) {
    case AF_INET:
      return ntohs((reinterpret_cast<const sockaddr_in*>(addr))->sin_port);
    case AF_INET6:
      return ntohs((reinterpret_cast<const sockaddr_in6*>(addr))->sin6_port);
#ifdef GRPC_HAVE_UNIX_SOCKET
    case AF_UNIX:
      return 1;
#endif
    default:
      gpr_log(GPR_ERROR, "Unknown socket family %d in ResolvedAddressGetPort",
              addr->sa_family);
      abort();
  }
}

void ResolvedAddressSetPort(EventEngine::ResolvedAddress& resolved_addr,
                            int port) {
  sockaddr* addr = const_cast<sockaddr*>(resolved_addr.address());
  switch (addr->sa_family) {
    case AF_INET:
      GPR_ASSERT(port >= 0 && port < 65536);
      (reinterpret_cast<sockaddr_in*>(addr))->sin_port =
          htons(static_cast<uint16_t>(port));
      return;
    case AF_INET6:
      GPR_ASSERT(port >= 0 && port < 65536);
      (reinterpret_cast<sockaddr_in6*>(addr))->sin6_port =
          htons(static_cast<uint16_t>(port));
      return;
    default:
      gpr_log(GPR_ERROR, "Unknown socket family %d in grpc_sockaddr_set_port",
              addr->sa_family);
      abort();
  }
}

absl::optional<int> ResolvedAddressIsWildcard(
    const EventEngine::ResolvedAddress& addr) {
  const EventEngine::ResolvedAddress* resolved_addr = &addr;
  EventEngine::ResolvedAddress addr4_normalized;
  if (ResolvedAddressIsV4Mapped(addr, &addr4_normalized)) {
    resolved_addr = &addr4_normalized;
  }
  if (resolved_addr->address()->sa_family == AF_INET) {
    // Check for 0.0.0.0
    const sockaddr_in* addr4 =
        reinterpret_cast<const sockaddr_in*>(resolved_addr->address());
    if (addr4->sin_addr.s_addr != 0) {
      return absl::nullopt;
    }
    return static_cast<int>(ntohs(addr4->sin_port));
  } else if (resolved_addr->address()->sa_family == AF_INET6) {
    // Check for ::
    const sockaddr_in6* addr6 =
        reinterpret_cast<const sockaddr_in6*>(resolved_addr->address());
    int i;
    for (i = 0; i < 16; i++) {
      if (addr6->sin6_addr.s6_addr[i] != 0) {
        return absl::nullopt;
      }
    }
    return static_cast<int>(ntohs(addr6->sin6_port));
  } else {
    return absl::nullopt;
  }
}

absl::StatusOr<std::string> ResolvedAddressToNormalizedString(
    const EventEngine::ResolvedAddress& resolved_addr) {
  EventEngine::ResolvedAddress addr_normalized;
  if (ResolvedAddressIsV4Mapped(resolved_addr, &addr_normalized)) {
    return ResolvedAddressToString(addr_normalized);
  }
  return ResolvedAddressToString(resolved_addr);
}

absl::StatusOr<std::string> ResolvedAddressToString(
    const EventEngine::ResolvedAddress& resolved_addr) {
  const int save_errno = errno;
  const sockaddr* addr = resolved_addr.address();
  std::string out;
#ifdef GRPC_HAVE_UNIX_SOCKET
  if (addr->sa_family == AF_UNIX) {
    const sockaddr_un* addr_un = reinterpret_cast<const sockaddr_un*>(addr);
    bool abstract = addr_un->sun_path[0] == '\0';
    if (abstract) {
#ifdef GPR_APPLE
      int len = resolved_addr.size() - sizeof(addr_un->sun_family) -
                sizeof(addr_un->sun_len);
#else
      int len = resolved_addr.size() - sizeof(addr_un->sun_family);
#endif
      if (len <= 0) {
        return absl::InvalidArgumentError("Empty UDS abstract path");
      }
      out = std::string(addr_un->sun_path, len);
    } else {
      size_t maxlen = sizeof(addr_un->sun_path);
      if (strnlen(addr_un->sun_path, maxlen) == maxlen) {
        return absl::InvalidArgumentError("UDS path is not null-terminated");
      }
      out = std::string(addr_un->sun_path);
    }
    return out;
  }
#endif  // GRPC_HAVE_UNIX_SOCKET

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
  if (ip != nullptr &&
      inet_ntop(addr->sa_family, ip, ntop_buf, sizeof(ntop_buf)) != nullptr) {
    if (sin6_scope_id != 0) {
      // Enclose sin6_scope_id with the format defined in RFC 6874
      // section 2.
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
  // This is probably redundant, but we wouldn't want to log the wrong
  // error.
  errno = save_errno;
  return out;
}

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

}  // namespace experimental
}  // namespace grpc_event_engine
