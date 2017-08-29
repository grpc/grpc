/*
 *
 * Copyright 2016-2017 gRPC authors.
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
#if GRPC_ARES != 1 || defined(GRPC_UV)

#include "src/core/ext/filters/client_channel/resolver/dns/c_ares/grpc_ares_wrapper.h"

struct grpc_ares_request {
  char val;
};

static grpc_ares_request *grpc_dns_lookup_ares_impl(
    grpc_exec_ctx *exec_ctx, const char *dns_server, const char *name,
    const char *default_port, grpc_pollset_set *interested_parties,
    grpc_closure *on_done, grpc_lb_addresses **addrs, bool check_grpclb,
    char **service_config_json) {
  return NULL;
}

grpc_ares_request *(*grpc_dns_lookup_ares)(
    grpc_exec_ctx *exec_ctx, const char *dns_server, const char *name,
    const char *default_port, grpc_pollset_set *interested_parties,
    grpc_closure *on_done, grpc_lb_addresses **addrs, bool check_grpclb,
    char **service_config_json) = grpc_dns_lookup_ares_impl;

void grpc_cancel_ares_request(grpc_exec_ctx *exec_ctx, grpc_ares_request *r) {}

grpc_error *grpc_ares_init(void) { return GRPC_ERROR_NONE; }

void grpc_ares_cleanup(void) {}

static void grpc_resolve_address_ares_impl(grpc_exec_ctx *exec_ctx,
                                           const char *name,
                                           const char *default_port,
                                           grpc_pollset_set *interested_parties,
                                           grpc_closure *on_done,
                                           grpc_resolved_addresses **addrs) {}

void (*grpc_resolve_address_ares)(
    grpc_exec_ctx *exec_ctx, const char *name, const char *default_port,
    grpc_pollset_set *interested_parties, grpc_closure *on_done,
    grpc_resolved_addresses **addrs) = grpc_resolve_address_ares_impl;

#endif /* GRPC_ARES != 1 || defined(GRPC_UV) */
