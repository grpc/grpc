/*
 *
 * Copyright 2017 gRPC authors.
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
  list->list = static_cast<grpc_proxy_mapper**>(gpr_realloc(
      list->list, (list->num_mappers + 1) * sizeof(grpc_proxy_mapper*)));
  if (at_start) {
    memmove(list->list + 1, list->list,
            sizeof(grpc_proxy_mapper*) * list->num_mappers);
    list->list[0] = mapper;
  } else {
    list->list[list->num_mappers] = mapper;
  }
  ++list->num_mappers;
}

static bool grpc_proxy_mapper_list_map_name(grpc_proxy_mapper_list* list,
                                            const char* server_uri,
                                            const grpc_channel_args* args,
                                            char** name_to_resolve,
                                            grpc_channel_args** new_args) {
  for (size_t i = 0; i < list->num_mappers; ++i) {
    if (grpc_proxy_mapper_map_name(list->list[i], server_uri, args,
                                   name_to_resolve, new_args)) {
      return true;
    }
  }
  return false;
}

static bool grpc_proxy_mapper_list_map_address(
    grpc_proxy_mapper_list* list, const grpc_resolved_address* address,
    const grpc_channel_args* args, grpc_resolved_address** new_address,
    grpc_channel_args** new_args) {
  for (size_t i = 0; i < list->num_mappers; ++i) {
    if (grpc_proxy_mapper_map_address(list->list[i], address, args, new_address,
                                      new_args)) {
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

bool grpc_proxy_mappers_map_name(const char* server_uri,
                                 const grpc_channel_args* args,
                                 char** name_to_resolve,
                                 grpc_channel_args** new_args) {
  return grpc_proxy_mapper_list_map_name(&g_proxy_mapper_list, server_uri, args,
                                         name_to_resolve, new_args);
}
bool grpc_proxy_mappers_map_address(const grpc_resolved_address* address,
                                    const grpc_channel_args* args,
                                    grpc_resolved_address** new_address,
                                    grpc_channel_args** new_args) {
  return grpc_proxy_mapper_list_map_address(&g_proxy_mapper_list, address, args,
                                            new_address, new_args);
}
