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

#include "src/core/ext/filters/client_channel/proxy_mapper_registry.h"

#include <string.h>

#include <grpc/support/alloc.h>

//
// grpc_proxy_mapper_list
//

typedef struct {
  grpc_proxy_mapper** list;
  size_t num_mappers;
} grpc_proxy_mapper_list;

static void grpc_proxy_mapper_list_register(grpc_proxy_mapper_list* list,
                                            bool at_start,
                                            grpc_proxy_mapper* mapper) {
  list->list = gpr_realloc(
      list->list, (list->num_mappers + 1) * sizeof(grpc_proxy_mapper*));
  if (at_start) {
    memmove(list->list + 1, list->list,
            sizeof(grpc_proxy_mapper*) * list->num_mappers);
    list->list[0] = mapper;
  } else {
    list->list[list->num_mappers] = mapper;
  }
  ++list->num_mappers;
}

static bool grpc_proxy_mapper_list_map_name(grpc_exec_ctx* exec_ctx,
                                            grpc_proxy_mapper_list* list,
                                            const char* server_uri,
                                            const grpc_channel_args* args,
                                            char** name_to_resolve,
                                            grpc_channel_args** new_args) {
  for (size_t i = 0; i < list->num_mappers; ++i) {
    if (grpc_proxy_mapper_map_name(exec_ctx, list->list[i], server_uri, args,
                                   name_to_resolve, new_args)) {
      return true;
    }
  }
  return false;
}

static bool grpc_proxy_mapper_list_map_address(
    grpc_exec_ctx* exec_ctx, grpc_proxy_mapper_list* list,
    const grpc_resolved_address* address, const grpc_channel_args* args,
    grpc_resolved_address** new_address, grpc_channel_args** new_args) {
  for (size_t i = 0; i < list->num_mappers; ++i) {
    if (grpc_proxy_mapper_map_address(exec_ctx, list->list[i], address, args,
                                      new_address, new_args)) {
      return true;
    }
  }
  return false;
}

static void grpc_proxy_mapper_list_destroy(grpc_proxy_mapper_list* list) {
  for (size_t i = 0; i < list->num_mappers; ++i) {
    grpc_proxy_mapper_destroy(list->list[i]);
  }
  gpr_free(list->list);
  // Clean up in case we re-initialze later.
  // TODO(ctiller): This should ideally live in
  // grpc_proxy_mapper_registry_init().  However, if we did this there,
  // then we would do it AFTER we start registering proxy mappers from
  // third-party plugins, so they'd never show up (and would leak memory).
  // We probably need some sort of dependency system for plugins to fix
  // this.
  memset(list, 0, sizeof(*list));
}

//
// plugin
//

static grpc_proxy_mapper_list g_proxy_mapper_list;

void grpc_proxy_mapper_registry_init() {}

void grpc_proxy_mapper_registry_shutdown() {
  grpc_proxy_mapper_list_destroy(&g_proxy_mapper_list);
}

void grpc_proxy_mapper_register(bool at_start, grpc_proxy_mapper* mapper) {
  grpc_proxy_mapper_list_register(&g_proxy_mapper_list, at_start, mapper);
}

bool grpc_proxy_mappers_map_name(grpc_exec_ctx* exec_ctx,
                                 const char* server_uri,
                                 const grpc_channel_args* args,
                                 char** name_to_resolve,
                                 grpc_channel_args** new_args) {
  return grpc_proxy_mapper_list_map_name(exec_ctx, &g_proxy_mapper_list,
                                         server_uri, args, name_to_resolve,
                                         new_args);
}
bool grpc_proxy_mappers_map_address(grpc_exec_ctx* exec_ctx,
                                    const grpc_resolved_address* address,
                                    const grpc_channel_args* args,
                                    grpc_resolved_address** new_address,
                                    grpc_channel_args** new_args) {
  return grpc_proxy_mapper_list_map_address(
      exec_ctx, &g_proxy_mapper_list, address, args, new_address, new_args);
}
