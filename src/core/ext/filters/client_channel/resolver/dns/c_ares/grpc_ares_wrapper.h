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

#ifndef GRPC_CORE_EXT_FILTERS_CLIENT_CHANNEL_RESOLVER_DNS_C_ARES_GRPC_ARES_WRAPPER_H
#define GRPC_CORE_EXT_FILTERS_CLIENT_CHANNEL_RESOLVER_DNS_C_ARES_GRPC_ARES_WRAPPER_H

#include <grpc/support/port_platform.h>

#include <ares.h>

#include "src/core/ext/filters/client_channel/server_address.h"
#include "src/core/lib/iomgr/iomgr.h"
#include "src/core/lib/iomgr/polling_entity.h"
#include "src/core/lib/iomgr/resolve_address.h"
#include "src/core/lib/iomgr/work_serializer.h"

#define GRPC_DNS_ARES_DEFAULT_QUERY_TIMEOUT_MS 120000

extern grpc_core::TraceFlag grpc_trace_cares_address_sorting;

extern grpc_core::TraceFlag grpc_trace_cares_resolver;

#define GRPC_CARES_TRACE_LOG(format, ...)                           \
  do {                                                              \
    if (GRPC_TRACE_FLAG_ENABLED(grpc_trace_cares_resolver)) {       \
      gpr_log(GPR_DEBUG, "(c-ares resolver) " format, __VA_ARGS__); \
    }                                                               \
  } while (0)

namespace grpc_core {

typedef struct grpc_ares_ev_driver grpc_ares_ev_driver;

struct AresRequest {
  AresRequest(
      grpc_closure* on_done,
      std::unique_ptr<grpc_core::ServerAddressList>* addresses_out,
      std::unique_ptr<grpc_core::ServerAddressList>* balancer_addresses_out,
      char** service_config_json_out);

  /// Cancel the pending request. Must be called while holding the
  /// WorkSerializer that was used to call \a LookupAresLocked.
  void CancelLocked();

  /// Initialize the gRPC ares wrapper. Must be called at least once before
  /// grpc_core::ResolveAddressAres().
  static grpc_error* Init(void);

  /// Uninitialized the gRPC ares wrapper. If there was more than one previous
  /// call to grpc_core::AresInit(), this function uninitializes the gRPC ares
  /// wrapper only if it has been called the same number of times as
  /// grpc_core::AresInit().
  static void Cleanup(void);

  // indicates the DNS server to use, if specified
  struct ares_addr_port_node dns_server_addr;
  // following members are set in grpc_resolve_address_ares_impl
  // closure to call when the request completes
  grpc_closure* on_done;
  // the pointer to receive the resolved addresses
  std::unique_ptr<grpc_core::ServerAddressList>* addresses_out;
  // the pointer to receive the resolved balancer addresses
  std::unique_ptr<grpc_core::ServerAddressList>* balancer_addresses_out;
  // the pointer to receive the service config in JSON
  char** service_config_json_out;
  // the evernt driver used by this request
  grpc_ares_ev_driver* ev_driver = nullptr;
  // number of ongoing queries
  size_t pending_queries = 0;

  // the errors explaining query failures, appended to in query callbacks
  grpc_error* error = GRPC_ERROR_NONE;
};

/// Asynchronously resolve \a name. Use \a default_port if a port isn't
/// designated in \a name, otherwise use the port in \a name.
/// grpc_core::AresInit() must be called at least once before this function.
extern void (*ResolveAddressAres)(const char* name, const char* default_port,
                                  grpc_pollset_set* interested_parties,
                                  grpc_closure* on_done,
                                  grpc_resolved_addresses** addresses);

/// Asynchronously resolve \a name. It will try to resolve grpclb SRV records in
/// addition to the normal address records if \a balancer_addresses is not
/// nullptr. For normal address records, it uses \a default_port if a port isn't
/// designated in \a name, otherwise it uses the port in \a name. AresInit()
/// must be called at least once before this function. The returned
/// AresRequest object is owned by the caller and it is safe to destroy
/// after \a on_done is called back.
extern std::unique_ptr<AresRequest> (*LookupAresLocked)(
    const char* dns_server, const char* name, const char* default_port,
    grpc_pollset_set* interested_parties, grpc_closure* on_done,
    std::unique_ptr<grpc_core::ServerAddressList>* addresses,
    std::unique_ptr<grpc_core::ServerAddressList>* balancer_addresses,
    char** service_config_json, int query_timeout_ms,
    std::shared_ptr<grpc_core::WorkSerializer> work_serializer);

/// Indicates whether or not AAAA queries should be attempted.
/// E.g., return false if ipv6 is known to not be available.
bool AresQueryIPv6();

/// Sorts destinations in \a addresses according to RFC 6724.
void AddressSortingSort(const AresRequest* r, ServerAddressList* addresses,
                        const std::string& logging_prefix);

namespace internal {

/// Exposed in this header for C-core tests only
extern void (*AresTestOnlyInjectConfig)(ares_channel channel);

}  // namespace internal

}  // namespace grpc_core

#endif /* GRPC_CORE_EXT_FILTERS_CLIENT_CHANNEL_RESOLVER_DNS_C_ARES_GRPC_ARES_WRAPPER_H \
        */
