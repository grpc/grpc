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

#ifndef GRPC_CORE_EXT_FILTERS_CLIENT_CHANNEL_PROXY_MAPPER_H
#define GRPC_CORE_EXT_FILTERS_CLIENT_CHANNEL_PROXY_MAPPER_H

#include <stdbool.h>

#include <grpc/impl/codegen/grpc_types.h>

#include "src/core/lib/iomgr/resolve_address.h"

typedef struct grpc_proxy_mapper grpc_proxy_mapper;

typedef struct {
  /// Determines the proxy name to resolve for \a server_uri.
  /// If no proxy is needed, returns false.
  /// Otherwise, sets \a name_to_resolve, optionally sets \a new_args,
  /// and returns true.
  bool (*map_name)(grpc_proxy_mapper* mapper, const char* server_uri,
                   const grpc_channel_args* args, char** name_to_resolve,
                   grpc_channel_args** new_args);
  /// Determines the proxy address to use to contact \a address.
  /// If no proxy is needed, returns false.
  /// Otherwise, sets \a new_address, optionally sets \a new_args, and
  /// returns true.
  bool (*map_address)(grpc_proxy_mapper* mapper,
                      const grpc_resolved_address* address,
                      const grpc_channel_args* args,
                      grpc_resolved_address** new_address,
                      grpc_channel_args** new_args);
  /// Destroys \a mapper.
  void (*destroy)(grpc_proxy_mapper* mapper);
} grpc_proxy_mapper_vtable;

struct grpc_proxy_mapper {
  const grpc_proxy_mapper_vtable* vtable;
};

void grpc_proxy_mapper_init(const grpc_proxy_mapper_vtable* vtable,
                            grpc_proxy_mapper* mapper);

bool grpc_proxy_mapper_map_name(grpc_proxy_mapper* mapper,
                                const char* server_uri,
                                const grpc_channel_args* args,
                                char** name_to_resolve,
                                grpc_channel_args** new_args);

bool grpc_proxy_mapper_map_address(grpc_proxy_mapper* mapper,
                                   const grpc_resolved_address* address,
                                   const grpc_channel_args* args,
                                   grpc_resolved_address** new_address,
                                   grpc_channel_args** new_args);

void grpc_proxy_mapper_destroy(grpc_proxy_mapper* mapper);

#endif /* GRPC_CORE_EXT_FILTERS_CLIENT_CHANNEL_PROXY_MAPPER_H */
