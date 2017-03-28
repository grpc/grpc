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

#include "src/core/ext/client_channel/subchannel.h"
#include "src/core/lib/channel/channel_registry.h"
#include "src/core/lib/surface/channel.h"

#include <grpc/support/alloc.h>
#include <grpc/support/avl.h>
#include <grpc/support/log.h>

// file global lock and avl.
static gpr_avl g_avl;
static intptr_t g_uuid = 0;

// avl vtable for uuid (intptr_t) -> channel/subchannel ptr.
// this table is only looking, it does not own anything.
static void destroy_intptr(void* not_used) { }
static void* copy_intptr(void* key) { return key; }
static long compare_intptr(void* key1, void* key2) { return key1 > key2; }
static void destroy_cptr(void* not_used) { }
static void* copy_cptr(void* value) { return value; }
static const gpr_avl_vtable avl_vtable = {
    destroy_intptr, copy_intptr, compare_intptr,
    destroy_cptr, copy_cptr};

void grpc_channel_registry_init() {
  g_avl = gpr_avl_create(&avl_vtable);
}

void grpc_channel_registry_shutdown() {
  gpr_avl_unref(g_avl);
}

intptr_t grpc_channel_registry_register_channel(void* channel) {
  intptr_t prior = gpr_atm_no_barrier_fetch_add(&g_uuid, 1);
  g_avl = gpr_avl_add(g_avl, (void*)prior, channel);
  return prior;
}

void grpc_channel_registry_unregister_channel(intptr_t uuid) {
  g_avl = gpr_avl_remove(g_avl, (void*)uuid);
}

void* grpc_channel_registry_get_channel(intptr_t uuid) {
  void* channel = gpr_avl_get(g_avl, (void*)uuid);
  GPR_ASSERT(channel);
  return channel;
}
