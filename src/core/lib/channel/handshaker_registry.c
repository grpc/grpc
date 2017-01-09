/*
 *
 * Copyright 2016, Google Inc.
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

#include "src/core/lib/channel/handshaker_registry.h"

#include <string.h>

#include <grpc/support/alloc.h>

//
// grpc_handshaker_factory_list
//

typedef struct {
  grpc_handshaker_factory** list;
  size_t num_factories;
} grpc_handshaker_factory_list;

static void grpc_handshaker_factory_list_register(
    grpc_handshaker_factory_list* list, bool at_start,
    grpc_handshaker_factory* factory) {
  list->list = gpr_realloc(
      list->list, (list->num_factories + 1) * sizeof(grpc_handshaker_factory*));
  if (at_start) {
    memmove(list->list + 1, list->list,
            sizeof(grpc_handshaker_factory*) * list->num_factories);
    list->list[0] = factory;
  } else {
    list->list[list->num_factories] = factory;
  }
  ++list->num_factories;
}

static void grpc_handshaker_factory_list_add_handshakers(
    grpc_exec_ctx* exec_ctx, grpc_handshaker_factory_list* list,
    const grpc_channel_args* args, grpc_handshake_manager* handshake_mgr) {
  for (size_t i = 0; i < list->num_factories; ++i) {
    grpc_handshaker_factory_add_handshakers(exec_ctx, list->list[i], args,
                                            handshake_mgr);
  }
}

static void grpc_handshaker_factory_list_destroy(
    grpc_exec_ctx* exec_ctx, grpc_handshaker_factory_list* list) {
  for (size_t i = 0; i < list->num_factories; ++i) {
    grpc_handshaker_factory_destroy(exec_ctx, list->list[i]);
  }
  gpr_free(list->list);
}

//
// plugin
//

static grpc_handshaker_factory_list
    g_handshaker_factory_lists[NUM_HANDSHAKER_TYPES];

void grpc_handshaker_factory_registry_init() {
  memset(g_handshaker_factory_lists, 0, sizeof(g_handshaker_factory_lists));
}

void grpc_handshaker_factory_registry_shutdown(grpc_exec_ctx* exec_ctx) {
  for (size_t i = 0; i < NUM_HANDSHAKER_TYPES; ++i) {
    grpc_handshaker_factory_list_destroy(exec_ctx,
                                         &g_handshaker_factory_lists[i]);
  }
}

void grpc_handshaker_factory_register(bool at_start,
                                      grpc_handshaker_type handshaker_type,
                                      grpc_handshaker_factory* factory) {
  grpc_handshaker_factory_list_register(
      &g_handshaker_factory_lists[handshaker_type], at_start, factory);
}

void grpc_handshakers_add(grpc_exec_ctx* exec_ctx,
                          grpc_handshaker_type handshaker_type,
                          const grpc_channel_args* args,
                          grpc_handshake_manager* handshake_mgr) {
  grpc_handshaker_factory_list_add_handshakers(
      exec_ctx, &g_handshaker_factory_lists[handshaker_type], args,
      handshake_mgr);
}
