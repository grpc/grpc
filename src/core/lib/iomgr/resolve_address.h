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

#include "src/core/lib/iomgr/port.h"

#include <grpc/event_engine/event_engine.h>

#ifdef GRPC_WINSOCK_SOCKET
#include <ws2tcpip.h>
#endif

#if defined(GRPC_POSIX_SOCKET) || defined(GRPC_CFSTREAM)
#include <sys/socket.h>
#endif

#include "src/core/lib/iomgr/pollset_set.h"

#define GRPC_MAX_SOCKADDR_SIZE 128
#define GRPC_DNS_DEFAULT_QUERY_TIMEOUT_MS 120000
#define GRPC_DNS_INITIAL_CONNECT_BACKOFF_SECONDS 1
#define GRPC_DNS_RECONNECT_BACKOFF_MULTIPLIER 1.6
#define GRPC_DNS_RECONNECT_MAX_BACKOFF_SECONDS 120
#define GRPC_DNS_RECONNECT_JITTER 0.2

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
}  // namespace grpc_core

typedef struct grpc_address_resolver_vtable {
  void (*resolve_address)(const char* addr, const char* default_port,
                          grpc_pollset_set* interested_parties,
                          grpc_closure* on_done,
                          grpc_resolved_addresses** addresses);
  grpc_error_handle (*blocking_resolve_address)(
      const char* name, const char* default_port,
      grpc_resolved_addresses** addresses);
  grpc_event_engine::experimental::EventEngine::DNSResolver::LookupTaskHandle (
      *lookup_hostname)(grpc_event_engine::experimental::EventEngine::
                            DNSResolver::LookupHostnameCallback on_resolved,
                        absl::string_view address,
                        absl::string_view default_port, absl::Time deadline,
                        grpc_pollset_set* interested_parties);
  grpc_event_engine::experimental::EventEngine::DNSResolver::LookupTaskHandle (
      *lookup_srv)(grpc_event_engine::experimental::EventEngine::DNSResolver::
                       LookupSRVCallback on_resolved,
                   absl::string_view name, absl::Time deadline,
                   grpc_pollset_set* interested_parties);
  grpc_event_engine::experimental::EventEngine::DNSResolver::LookupTaskHandle (
      *lookup_txt)(grpc_event_engine::experimental::EventEngine::DNSResolver::
                       LookupTXTCallback on_resolved,
                   absl::string_view name, absl::Time deadline,
                   grpc_pollset_set* interested_parties);
  void (*try_cancel_lookup)(grpc_event_engine::experimental::EventEngine::
                                DNSResolver::LookupTaskHandle handle);
} grpc_address_resolver_vtable;

void grpc_set_resolver_impl(grpc_address_resolver_vtable* vtable);

/* Asynchronously resolve addr. Use default_port if a port isn't designated
   in addr, otherwise use the port in addr. */
/* TODO(apolcyn): add a timeout here */
void grpc_resolve_address(const char* addr, const char* default_port,
                          grpc_pollset_set* interested_parties,
                          grpc_closure* on_done,
                          grpc_resolved_addresses** addresses);

/* Destroy resolved addresses */
void grpc_resolved_addresses_destroy(grpc_resolved_addresses* addresses);

/* Resolve addr in a blocking fashion. On success,
   result must be freed with grpc_resolved_addresses_destroy. */
grpc_error_handle grpc_blocking_resolve_address(
    const char* name, const char* default_port,
    grpc_resolved_addresses** addresses);

/// Asynchronously resolve an address.
///
/// \a default_port may be a non-numeric named service port, and will only
/// be used if \a address does not already contain a port component.
///
/// When the lookup is complete, the \a on_resolved callback will be invoked
/// with a status indicating the success or failure of the lookup.
grpc_event_engine::experimental::EventEngine::DNSResolver::LookupTaskHandle
grpc_dns_lookup_hostname(grpc_event_engine::experimental::EventEngine::
                             DNSResolver::LookupHostnameCallback on_resolved,
                         absl::string_view address,
                         absl::string_view default_port, absl::Time deadline,
                         grpc_pollset_set* interested_parties);

/// Asynchronously perform an SRV record lookup.
///
/// \a on_resolve has the same meaning and expectations as \a
/// LookupHostname's \a on_resolve callback.
grpc_event_engine::experimental::EventEngine::DNSResolver::LookupTaskHandle
grpc_dns_lookup_srv_record(
    grpc_event_engine::experimental::EventEngine::DNSResolver::LookupSRVCallback
        on_resolved,
    absl::string_view name, absl::Time deadline,
    grpc_pollset_set* interested_parties);

/// Asynchronously perform a TXT record lookup.
///
/// \a on_resolve has the same meaning and expectations as \a
/// LookupHostname's \a on_resolve callback.
grpc_event_engine::experimental::EventEngine::DNSResolver::LookupTaskHandle
grpc_dns_lookup_txt_record(
    grpc_event_engine::experimental::EventEngine::DNSResolver::LookupTXTCallback
        on_resolved,
    absl::string_view name, absl::Time deadline,
    grpc_pollset_set* interested_parties);

/// Cancel an asynchronous lookup operation.
///
/// Note that this is a "best effort" cancellation. No guarantee is made that
/// the lookup will be cancelled, the lookup could be in any stage. In all
/// cases, the \a on_resolved callback will be run exactly once from either
/// cancellation or from its activation.
void grpc_dns_try_cancel(
    grpc_event_engine::experimental::EventEngine::DNSResolver::LookupTaskHandle
        handle);

#endif /* GRPC_CORE_LIB_IOMGR_RESOLVE_ADDRESS_H */
