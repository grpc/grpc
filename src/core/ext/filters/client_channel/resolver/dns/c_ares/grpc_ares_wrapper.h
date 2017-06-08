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

#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/lib/iomgr/iomgr.h"
#include "src/core/lib/iomgr/polling_entity.h"
#include "src/core/lib/iomgr/resolve_address.h"

/* Asynchronously resolve addr. Use \a default_port if a port isn't designated
   in addr, otherwise use the port in addr. grpc_ares_init() must be called at
   least once before this function. \a on_done may be called directly in this
   function without being scheduled with \a exec_ctx, it must not try to acquire
   locks that are being held by the caller. */
extern void (*grpc_resolve_address_ares)(grpc_exec_ctx *exec_ctx,
                                         const char *addr,
                                         const char *default_port,
                                         grpc_pollset_set *interested_parties,
                                         grpc_closure *on_done,
                                         grpc_resolved_addresses **addresses);

void grpc_dns_lookup_ares(grpc_exec_ctx *exec_ctx, const char *dns_server,
                          const char *addr, const char *default_port,
                          grpc_pollset_set *interested_parties,
                          grpc_closure *on_done,
                          grpc_resolved_addresses **addresses);

/* Initialize gRPC ares wrapper. Must be called at least once before
   grpc_resolve_address_ares(). */
grpc_error *grpc_ares_init(void);

/* Uninitialized gRPC ares wrapper. If there was more than one previous call to
   grpc_ares_init(), this function uninitializes the gRPC ares wrapper only if
   it has been called the same number of times as grpc_ares_init(). */
void grpc_ares_cleanup(void);

#endif /* GRPC_CORE_EXT_FILTERS_CLIENT_CHANNEL_RESOLVER_DNS_C_ARES_GRPC_ARES_WRAPPER_H \
          */
