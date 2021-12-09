/*
 *
 * Copyright 2015 gRPC authors.
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

#ifndef GRPC_CORE_LIB_IOMGR_RESOLVE_ADDRESS_H
#define GRPC_CORE_LIB_IOMGR_RESOLVE_ADDRESS_H

#include <grpc/support/port_platform.h>

#include <stddef.h>

#include "src/core/lib/gprpp/orphanable.h"
#include "src/core/lib/iomgr/port.h"

#ifdef GRPC_WINSOCK_SOCKET
#include <ws2tcpip.h>
#endif

#if defined(GRPC_POSIX_SOCKET) || defined(GRPC_CFSTREAM)
#include <sys/socket.h>
#endif

#include "src/core/lib/iomgr/pollset_set.h"

#define GRPC_MAX_SOCKADDR_SIZE 128

struct grpc_resolved_address {
  char addr[GRPC_MAX_SOCKADDR_SIZE];
  socklen_t len;
};
struct grpc_resolved_addresses {
  size_t naddrs;
  grpc_resolved_address* addrs;
};

namespace grpc_core {
extern const char* kDefaultSecurePort;
constexpr int kDefaultSecurePortInt = 443;

// Tracks a single asynchronous DNS resolution attempt. The DNS
// resolution should be arranged to be cancelled as soon as possible
// when Orphan is called.
class DNSRequest : public InternallyRefCounted<DNSRequest> {};

// A singleton class used for async and blocking DNS resolution
class DNSResolver {
 public:
  virtual ~DNSResolver() {}

  // Get the singleton instance
  static DNSResolver* instance() { return instance_; }

  // Override the active DNS resolver, which should be used for all DNS
  // resolution. Note: this should only be used during library initialization,
  // or tests.
  static void OverrideInstance(DNSResolver* resolver){instance_ = resolver};

  // Asynchronously resolve addr. Use default_port if a port isn't designated
  // in addr, otherwise use the port in addr.
  // TODO(apolcyn): add a timeout here.
  virtual OrphanablePtr<Request> ResolveAddress(
      absl::string_view name, absl::string_view default_port,
      grpc_pollset_set* interested_parties,
      std::function<void(absl::StatusOr<grpc_resolved_addresses*>)> on_done)
      GRPC_MUST_USE_RESULT = 0;

  // Resolve addr in a blocking fashion. On success,
  // result must be freed with grpc_resolved_addresses_destroy.
  virtual absl::StatusOr<grpc_resolved_addresses*> BlockingResolveAddress(
      absl::strinv_view name, absl::string_view default_port);

 private:
  static DNSResolver* instance_;
};

}  // namespace grpc_core

/* Destroy resolved addresses */
void grpc_resolved_addresses_destroy(grpc_resolved_addresses* addresses);

#endif /* GRPC_CORE_LIB_IOMGR_RESOLVE_ADDRESS_H */
