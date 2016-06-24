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

#ifndef GRPC_CORE_LIB_IOMGR_RESOLVE_ADDRESS_H
#define GRPC_CORE_LIB_IOMGR_RESOLVE_ADDRESS_H

#include <stddef.h>
#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/lib/iomgr/iomgr.h"

#define GRPC_MAX_SOCKADDR_SIZE 128

typedef struct {
  char addr[GRPC_MAX_SOCKADDR_SIZE];
  size_t len;
} grpc_resolved_address;

typedef struct {
  size_t naddrs;
  grpc_resolved_address *addrs;
} grpc_resolved_addresses;

/* Asynchronously resolve addr. Use default_port if a port isn't designated
   in addr, otherwise use the port in addr. */
/* TODO(ctiller): add a timeout here */
extern void (*grpc_resolve_address)(grpc_exec_ctx *exec_ctx, const char *addr,
                                    const char *default_port,
                                    grpc_closure *on_done,
                                    grpc_resolved_addresses **addresses);
/* Destroy resolved addresses */
void grpc_resolved_addresses_destroy(grpc_resolved_addresses *addresses);

/* Resolve addr in a blocking fashion. Returns NULL on failure. On success,
   result must be freed with grpc_resolved_addresses_destroy. */
extern grpc_error *(*grpc_blocking_resolve_address)(
    const char *name, const char *default_port,
    grpc_resolved_addresses **addresses);

#endif /* GRPC_CORE_LIB_IOMGR_RESOLVE_ADDRESS_H */
