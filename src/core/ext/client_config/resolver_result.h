/*
 *
 * Copyright 2015, Google Inc.
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

#ifndef GRPC_CORE_EXT_CLIENT_CONFIG_RESOLVER_RESULT_H
#define GRPC_CORE_EXT_CLIENT_CONFIG_RESOLVER_RESULT_H

#include <stdbool.h>

#include "src/core/ext/client_config/lb_policy.h"
#include "src/core/lib/iomgr/resolve_address.h"

/** Used to represent addresses returned by the resolver. */
typedef struct grpc_address {
  grpc_resolved_address address;
  bool is_balancer;
} grpc_address;

typedef struct grpc_addresses {
  size_t num_addresses;
  grpc_address *addresses;
} grpc_addresses;

/** Returns a grpc_addresses struct with enough space for
    \a num_addresses addresses. */
grpc_addresses *grpc_addresses_create(size_t num_addresses);

void grpc_addresses_set_address(grpc_addresses *addresses, size_t index,
                                void *address, size_t address_len,
                                bool is_balancer);

void grpc_addresses_destroy(grpc_addresses *addresses);

/** Results reported from a grpc_resolver. */
typedef struct grpc_resolver_result grpc_resolver_result;

/** Takes ownership of \a addresses. */
grpc_resolver_result *grpc_resolver_result_create(grpc_addresses *addresses,
                                                  const char *lb_policy_name);
void grpc_resolver_result_ref(grpc_resolver_result *result);
void grpc_resolver_result_unref(grpc_exec_ctx *exec_ctx,
                                grpc_resolver_result *result);

/** Caller does NOT take ownership of result. */
grpc_addresses *grpc_resolver_result_get_addresses(
    grpc_resolver_result *result);

/** Caller does NOT take ownership of result. */
const char *grpc_resolver_result_get_lb_policy_name(
    grpc_resolver_result *result);

#endif /* GRPC_CORE_EXT_CLIENT_CONFIG_RESOLVER_RESULT_H */
