//
//
// Copyright 2016 gRPC authors.
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

#include <grpc/support/port_platform.h>

#include "src/core/lib/address_utils/sockaddr_utils.h"

#include <errno.h>
#include <inttypes.h>
#ifdef GRPC_HAVE_VSOCK
#include <linux/vm_sockets.h>
#endif
#include <string.h>

#include <string>
#include <utility>

#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"

#include <grpc/support/log.h>

#include "src/core/lib/gprpp/crash.h"
#include "src/core/lib/gprpp/host_port.h"
#include "src/core/lib/iomgr/port.h"
#include "src/core/lib/iomgr/sockaddr.h"
#include "src/core/lib/iomgr/socket_utils.h"
#include "src/core/lib/uri/uri_parser.h"

#ifdef GRPC_HAVE_UNIX_SOCKET
#include <sys/un.h>
#endif

#ifdef GRPC_HAVE_UNIX_SOCKET
static absl::StatusOr<std::string> grpc_sockaddr_to_uri_unix_if_possible(
    const grpc_resolved_address* resolved_addr) {
  const grpc_sockaddr* addr =
      reinterpret_cast<const grpc_sockaddr*>(resolved_addr->addr);
  if (addr->sa_family != AF_UNIX) {
    return absl::InvalidArgumentError(
        absl::StrCat("Socket family is not AF_UNIX: ", addr->sa_family));
  }
  const auto* unix_addr = reinterpret_cast<const struct sockaddr_un*>(addr);
  std::string scheme, path;
  if (unix_addr->sun_path[0] == '\0' && unix_addr->sun_path[1] != '\0') {
    scheme = "unix-abstract";
    path = std::string(unix_addr->sun_path + 1,
                       resolved_addr->len - sizeof(unix_addr->sun_family) - 1);
  } else {
    scheme = "unix";
    path = unix_addr->sun_path;
  }
  absl::StatusOr<grpc_core::URI> uri = grpc_core::URI::Create(
      std::move(scheme), /*authority=*/"", std::move(path),
      /*query_parameter_pairs=*/{}, /*fragment=*/"");
  if (!uri.ok()) return uri.status();
  return uri->ToString();
}
#else
static absl::StatusOr<std::string> grpc_sockaddr_to_uri_unix_if_possible(
    const grpc_resolved_address* /* addr */) {
  return absl::InvalidArgumentError("Unix socket is not supported.");
}
#endif

#ifdef GRPC_HAVE_VSOCK
static absl::StatusOr<std::string> grpc_sockaddr_to_uri_vsock_if_possible(
    const grpc_resolved_address* resolved_addr) {
  const grpc_sockaddr* addr =
      reinterpret_cast<const grpc_sockaddr*>(resolved_addr->addr);
  if (addr->sa_family != AF_VSOCK) {
    return absl::InvalidArgumentError(
        absl::StrCat("Socket family is not AF_VSOCK: ", addr->sa_family));
  }
  const auto* vsock_addr = reinterpret_cast<const struct sockaddr_vm*>(addr);
  return absl::StrCat("vsock:", vsock_addr->svm_cid, ":", vsock_addr->svm_port);
}
#else
static absl::StatusOr<std::string> grpc_sockaddr_to_uri_vsock_if_possible(
    const grpc_resolved_address* /* addr */) {
  return absl::InvalidArgumentError("VSOCK is not supported.");
}
#endif

static const uint8_t kV4MappedPrefix[] = {0, 0, 0, 0, 0,    0,
                                          0, 0, 0, 0, 0xff, 0xff};

int grpc_sockaddr_is_v4mapped(const grpc_resolved_address* resolved_addr,
                              grpc_resolved_address* resolved_addr4_out) {
  GPR_ASSERT(resolved_addr != resolved_addr4_out);
  const grpc_sockaddr* addr =
      reinterpret_cast<const grpc_sockaddr*>(resolved_addr->addr);
  grpc_sockaddr_in* addr4_out =
      resolved_addr4_out == nullptr
          ? nullptr
          : reinterpret_cast<grpc_sockaddr_in*>(resolved_addr4_out->addr);
  if (addr->sa_family == GRPC_AF_INET6) {
    const grpc_sockaddr_in6* addr6 =
        reinterpret_cast<const grpc_sockaddr_in6*>(addr);
    if (memcmp(addr6->sin6_addr.s6_addr, kV4MappedPrefix,
               sizeof(kV4MappedPrefix)) == 0) {
      if (resolved_addr4_out != nullptr) {
        // Normalize ::ffff:0.0.0.0/96 to IPv4.
        memset(resolved_addr4_out, 0, sizeof(*resolved_addr4_out));
        addr4_out->sin_family = GRPC_AF_INET;
        // s6_addr32 would be nice, but it's non-standard.
        memcpy(&addr4_out->sin_addr, &addr6->sin6_addr.s6_addr[12], 4);
        addr4_out->sin_port = addr6->sin6_port;
        resolved_addr4_out->len =
            static_cast<socklen_t>(sizeof(grpc_sockaddr_in));
      }
      return 1;
    }
  }
  return 0;
}

int grpc_sockaddr_to_v4mapped(const grpc_resolved_address* resolved_addr,
                              grpc_resolved_address* resolved_addr6_out) {
  GPR_ASSERT(resolved_addr != resolved_addr6_out);
  const grpc_sockaddr* addr =
      reinterpret_cast<const grpc_sockaddr*>(resolved_addr->addr);
  grpc_sockaddr_in6* addr6_out =
      reinterpret_cast<grpc_sockaddr_in6*>(resolved_addr6_out->addr);
  if (addr->sa_family == GRPC_AF_INET) {
    const grpc_sockaddr_in* addr4 =
        reinterpret_cast<const grpc_sockaddr_in*>(addr);
    memset(resolved_addr6_out, 0, sizeof(*resolved_addr6_out));
    addr6_out->sin6_family = GRPC_AF_INET6;
    memcpy(&addr6_out->sin6_addr.s6_addr[0], kV4MappedPrefix, 12);
    memcpy(&addr6_out->sin6_addr.s6_addr[12], &addr4->sin_addr, 4);
    addr6_out->sin6_port = addr4->sin_port;
    resolved_addr6_out->len = static_cast<socklen_t>(sizeof(grpc_sockaddr_in6));
    return 1;
  }
  return 0;
}

int grpc_sockaddr_is_wildcard(const grpc_resolved_address* resolved_addr,
                              int* port_out) {
  const grpc_sockaddr* addr;
  grpc_resolved_address addr4_normalized;
  if (grpc_sockaddr_is_v4mapped(resolved_addr, &addr4_normalized)) {
    resolved_addr = &addr4_normalized;
  }
  addr = reinterpret_cast<const grpc_sockaddr*>(resolved_addr->addr);
  if (addr->sa_family == GRPC_AF_INET) {
    // Check for 0.0.0.0
    const grpc_sockaddr_in* addr4 =
        reinterpret_cast<const grpc_sockaddr_in*>(addr);
    if (addr4->sin_addr.s_addr != 0) {
      return 0;
    }
    *port_out = grpc_ntohs(addr4->sin_port);
    return 1;
  } else if (addr->sa_family == GRPC_AF_INET6) {
    // Check for ::
    const grpc_sockaddr_in6* addr6 =
        reinterpret_cast<const grpc_sockaddr_in6*>(addr);
    int i;
    for (i = 0; i < 16; i++) {
      if (addr6->sin6_addr.s6_addr[i] != 0) {
        return 0;
      }
    }
    *port_out = grpc_ntohs(addr6->sin6_port);
    return 1;
  } else {
    return 0;
  }
}

void grpc_sockaddr_make_wildcards(int port, grpc_resolved_address* wild4_out,
                                  grpc_resolved_address* wild6_out) {
  grpc_sockaddr_make_wildcard4(port, wild4_out);
  grpc_sockaddr_make_wildcard6(port, wild6_out);
}

void grpc_sockaddr_make_wildcard4(int port,
                                  grpc_resolved_address* resolved_wild_out) {
  grpc_sockaddr_in* wild_out =
      reinterpret_cast<grpc_sockaddr_in*>(resolved_wild_out->addr);
  GPR_ASSERT(port >= 0 && port < 65536);
  memset(resolved_wild_out, 0, sizeof(*resolved_wild_out));
  wild_out->sin_family = GRPC_AF_INET;
  wild_out->sin_port = grpc_htons(static_cast<uint16_t>(port));
  resolved_wild_out->len = static_cast<socklen_t>(sizeof(grpc_sockaddr_in));
}

void grpc_sockaddr_make_wildcard6(int port,
                                  grpc_resolved_address* resolved_wild_out) {
  grpc_sockaddr_in6* wild_out =
      reinterpret_cast<grpc_sockaddr_in6*>(resolved_wild_out->addr);
  GPR_ASSERT(port >= 0 && port < 65536);
  memset(resolved_wild_out, 0, sizeof(*resolved_wild_out));
  wild_out->sin6_family = GRPC_AF_INET6;
  wild_out->sin6_port = grpc_htons(static_cast<uint16_t>(port));
  resolved_wild_out->len = static_cast<socklen_t>(sizeof(grpc_sockaddr_in6));
}

absl::StatusOr<std::string> grpc_sockaddr_to_string(
    const grpc_resolved_address* resolved_addr, bool normalize) {
  const int save_errno = errno;
  grpc_resolved_address addr_normalized;
  if (normalize && grpc_sockaddr_is_v4mapped(resolved_addr, &addr_normalized)) {
    resolved_addr = &addr_normalized;
  }
  const grpc_sockaddr* addr =
      reinterpret_cast<const grpc_sockaddr*>(resolved_addr->addr);
  std::string out;
#ifdef GRPC_HAVE_UNIX_SOCKET
  if (addr->sa_family == GRPC_AF_UNIX) {
    const sockaddr_un* addr_un = reinterpret_cast<const sockaddr_un*>(addr);
    bool abstract = addr_un->sun_path[0] == '\0';
    if (abstract) {
      int len = resolved_addr->len - sizeof(addr->sa_family);
      if (len <= 0) {
        return absl::InvalidArgumentError("empty UDS abstract path");
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
#endif

#ifdef GRPC_HAVE_VSOCK
  if (addr->sa_family == GRPC_AF_VSOCK) {
    const sockaddr_vm* addr_vm = reinterpret_cast<const sockaddr_vm*>(addr);
    out = absl::StrCat(addr_vm->svm_cid, ":", addr_vm->svm_port);
    return out;
  }
#endif

  const void* ip = nullptr;
  int port = 0;
  uint32_t sin6_scope_id = 0;
  if (addr->sa_family == GRPC_AF_INET) {
    const grpc_sockaddr_in* addr4 =
        reinterpret_cast<const grpc_sockaddr_in*>(addr);
    ip = &addr4->sin_addr;
    port = grpc_ntohs(addr4->sin_port);
  } else if (addr->sa_family == GRPC_AF_INET6) {
    const grpc_sockaddr_in6* addr6 =
        reinterpret_cast<const grpc_sockaddr_in6*>(addr);
    ip = &addr6->sin6_addr;
    port = grpc_ntohs(addr6->sin6_port);
    sin6_scope_id = addr6->sin6_scope_id;
  }
  char ntop_buf[GRPC_INET6_ADDRSTRLEN];
  if (ip != nullptr && grpc_inet_ntop(addr->sa_family, ip, ntop_buf,
                                      sizeof(ntop_buf)) != nullptr) {
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
  // This is probably redundant, but we wouldn't want to log the wrong error.
  errno = save_errno;
  return out;
}

absl::StatusOr<std::string> grpc_sockaddr_to_uri(
    const grpc_resolved_address* resolved_addr) {
  if (resolved_addr->len == 0) {
    return absl::InvalidArgumentError("Empty address");
  }
  grpc_resolved_address addr_normalized;
  if (grpc_sockaddr_is_v4mapped(resolved_addr, &addr_normalized)) {
    resolved_addr = &addr_normalized;
  }
  const char* scheme = grpc_sockaddr_get_uri_scheme(resolved_addr);
  if (scheme == nullptr) {
    return absl::InvalidArgumentError("Unknown address type");
  }
  if (strcmp("unix", scheme) == 0) {
    return grpc_sockaddr_to_uri_unix_if_possible(resolved_addr);
  }
  if (strcmp("vsock", scheme) == 0) {
    return grpc_sockaddr_to_uri_vsock_if_possible(resolved_addr);
  }

  auto path = grpc_sockaddr_to_string(resolved_addr, false /* normalize */);
  if (!path.ok()) return path;
  absl::StatusOr<grpc_core::URI> uri =
      grpc_core::URI::Create(scheme, /*authority=*/"", std::move(path.value()),
                             /*query_parameter_pairs=*/{}, /*fragment=*/"");
  if (!uri.ok()) return uri.status();
  return uri->ToString();
}

const char* grpc_sockaddr_get_uri_scheme(
    const grpc_resolved_address* resolved_addr) {
  const grpc_sockaddr* addr =
      reinterpret_cast<const grpc_sockaddr*>(resolved_addr->addr);
  switch (addr->sa_family) {
    case GRPC_AF_INET:
      return "ipv4";
    case GRPC_AF_INET6:
      return "ipv6";
    case GRPC_AF_UNIX:
      return "unix";
#ifdef GRPC_HAVE_VSOCK
    case GRPC_AF_VSOCK:
      return "vsock";
#endif
  }
  return nullptr;
}

int grpc_sockaddr_get_family(const grpc_resolved_address* resolved_addr) {
  const grpc_sockaddr* addr =
      reinterpret_cast<const grpc_sockaddr*>(resolved_addr->addr);
  return addr->sa_family;
}

int grpc_sockaddr_get_port(const grpc_resolved_address* resolved_addr) {
  const grpc_sockaddr* addr =
      reinterpret_cast<const grpc_sockaddr*>(resolved_addr->addr);
  switch (addr->sa_family) {
    case GRPC_AF_INET:
      return grpc_ntohs(
          (reinterpret_cast<const grpc_sockaddr_in*>(addr))->sin_port);
    case GRPC_AF_INET6:
      return grpc_ntohs(
          (reinterpret_cast<const grpc_sockaddr_in6*>(addr))->sin6_port);
#ifdef GRPC_HAVE_UNIX_SOCKET
    case AF_UNIX:
      return 1;
#endif
#ifdef GRPC_HAVE_VSOCK
    case AF_VSOCK:
      return 1;
#endif
    default:
      gpr_log(GPR_ERROR, "Unknown socket family %d in grpc_sockaddr_get_port",
              addr->sa_family);
      return 0;
  }
}

int grpc_sockaddr_set_port(grpc_resolved_address* resolved_addr, int port) {
  grpc_sockaddr* addr = reinterpret_cast<grpc_sockaddr*>(resolved_addr->addr);
  switch (addr->sa_family) {
    case GRPC_AF_INET:
      GPR_ASSERT(port >= 0 && port < 65536);
      (reinterpret_cast<grpc_sockaddr_in*>(addr))->sin_port =
          grpc_htons(static_cast<uint16_t>(port));
      return 1;
    case GRPC_AF_INET6:
      GPR_ASSERT(port >= 0 && port < 65536);
      (reinterpret_cast<grpc_sockaddr_in6*>(addr))->sin6_port =
          grpc_htons(static_cast<uint16_t>(port));
      return 1;
    default:
      gpr_log(GPR_ERROR, "Unknown socket family %d in grpc_sockaddr_set_port",
              addr->sa_family);
      return 0;
  }
}

std::string grpc_sockaddr_get_packed_host(
    const grpc_resolved_address* resolved_addr) {
  const grpc_sockaddr* addr =
      reinterpret_cast<const grpc_sockaddr*>(resolved_addr->addr);
  if (addr->sa_family == GRPC_AF_INET) {
    const grpc_sockaddr_in* addr4 =
        reinterpret_cast<const grpc_sockaddr_in*>(addr);
    const char* addr_bytes = reinterpret_cast<const char*>(&addr4->sin_addr);
    return std::string(addr_bytes, 4);
  } else if (addr->sa_family == GRPC_AF_INET6) {
    const grpc_sockaddr_in6* addr6 =
        reinterpret_cast<const grpc_sockaddr_in6*>(addr);
    const char* addr_bytes = reinterpret_cast<const char*>(&addr6->sin6_addr);
    return std::string(addr_bytes, 16);
  } else {
    grpc_core::Crash("unknown socket family");
  }
}

void grpc_sockaddr_mask_bits(grpc_resolved_address* address,
                             uint32_t mask_bits) {
  grpc_sockaddr* addr = reinterpret_cast<grpc_sockaddr*>(address->addr);
  if (addr->sa_family == GRPC_AF_INET) {
    grpc_sockaddr_in* addr4 = reinterpret_cast<grpc_sockaddr_in*>(addr);
    if (mask_bits == 0) {
      memset(&addr4->sin_addr, 0, sizeof(addr4->sin_addr));
      return;
    } else if (mask_bits >= 32) {
      return;
    }
    uint32_t mask_ip_addr = (~(uint32_t{0})) << (32 - mask_bits);
    addr4->sin_addr.s_addr &= grpc_htonl(mask_ip_addr);
  } else if (addr->sa_family == GRPC_AF_INET6) {
    grpc_sockaddr_in6* addr6 = reinterpret_cast<grpc_sockaddr_in6*>(addr);
    if (mask_bits == 0) {
      memset(&addr6->sin6_addr, 0, sizeof(addr6->sin6_addr));
      return;
    } else if (mask_bits >= 128) {
      return;
    }
    // We cannot use s6_addr32 since it is not defined on all platforms that we
    // need it on.
    uint32_t address_parts[4];
    GPR_ASSERT(sizeof(addr6->sin6_addr) == sizeof(address_parts));
    memcpy(address_parts, &addr6->sin6_addr, sizeof(grpc_in6_addr));
    if (mask_bits <= 32) {
      uint32_t mask_ip_addr = (~(uint32_t{0})) << (32 - mask_bits);
      address_parts[0] &= grpc_htonl(mask_ip_addr);
      memset(&address_parts[1], 0, sizeof(uint32_t));
      memset(&address_parts[2], 0, sizeof(uint32_t));
      memset(&address_parts[3], 0, sizeof(uint32_t));
    } else if (mask_bits <= 64) {
      mask_bits -= 32;
      uint32_t mask_ip_addr = (~(uint32_t{0})) << (32 - mask_bits);
      address_parts[1] &= grpc_htonl(mask_ip_addr);
      memset(&address_parts[2], 0, sizeof(uint32_t));
      memset(&address_parts[3], 0, sizeof(uint32_t));
    } else if (mask_bits <= 96) {
      mask_bits -= 64;
      uint32_t mask_ip_addr = (~(uint32_t{0})) << (32 - mask_bits);
      address_parts[2] &= grpc_htonl(mask_ip_addr);
      memset(&address_parts[3], 0, sizeof(uint32_t));
    } else {
      mask_bits -= 96;
      uint32_t mask_ip_addr = (~(uint32_t{0})) << (32 - mask_bits);
      address_parts[3] &= grpc_htonl(mask_ip_addr);
    }
    memcpy(&addr6->sin6_addr, address_parts, sizeof(grpc_in6_addr));
  }
}

bool grpc_sockaddr_match_subnet(const grpc_resolved_address* address,
                                const grpc_resolved_address* subnet_address,
                                uint32_t mask_bits) {
  auto* addr = reinterpret_cast<const grpc_sockaddr*>(address->addr);
  auto* subnet_addr =
      reinterpret_cast<const grpc_sockaddr*>(subnet_address->addr);
  if (addr->sa_family != subnet_addr->sa_family) return false;
  grpc_resolved_address masked_address;
  memcpy(&masked_address, address, sizeof(grpc_resolved_address));
  addr = reinterpret_cast<grpc_sockaddr*>((&masked_address)->addr);
  grpc_sockaddr_mask_bits(&masked_address, mask_bits);
  if (addr->sa_family == GRPC_AF_INET) {
    auto* addr4 = reinterpret_cast<const grpc_sockaddr_in*>(addr);
    auto* subnet_addr4 = reinterpret_cast<const grpc_sockaddr_in*>(subnet_addr);
    if (memcmp(&addr4->sin_addr, &subnet_addr4->sin_addr,
               sizeof(addr4->sin_addr)) == 0) {
      return true;
    }
  } else if (addr->sa_family == GRPC_AF_INET6) {
    auto* addr6 = reinterpret_cast<const grpc_sockaddr_in6*>(addr);
    auto* subnet_addr6 =
        reinterpret_cast<const grpc_sockaddr_in6*>(subnet_addr);
    if (memcmp(&addr6->sin6_addr, &subnet_addr6->sin6_addr,
               sizeof(addr6->sin6_addr)) == 0) {
      return true;
    }
  }
  return false;
}
