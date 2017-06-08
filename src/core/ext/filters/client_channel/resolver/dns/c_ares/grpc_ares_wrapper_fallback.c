/*
 *
 * Copyright 2017, Google Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *     * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 * copyright notice, this list of conditions and the following disclaimer
 * in the documentation and/or other materials provided with the
 * distribution.
 *     * Neither the name of Google Inc. nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
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
    grpc_closure *on_done, grpc_lb_addresses **addrs, bool check_grpclb) {
  return NULL;
}

grpc_ares_request *(*grpc_dns_lookup_ares)(
    grpc_exec_ctx *exec_ctx, const char *dns_server, const char *name,
    const char *default_port, grpc_pollset_set *interested_parties,
    grpc_closure *on_done, grpc_lb_addresses **addrs,
    bool check_grpclb) = grpc_dns_lookup_ares_impl;

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
