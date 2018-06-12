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

#include "src/core/ext/filters/client_channel/proxy_mapper.h"

void grpc_proxy_mapper_init(const grpc_proxy_mapper_vtable* vtable,
                            grpc_proxy_mapper* mapper) {
  mapper->vtable = vtable;
}

bool grpc_proxy_mapper_map_name(grpc_proxy_mapper* mapper,
                                const char* server_uri,
                                const grpc_channel_args* args,
                                char** name_to_resolve,
                                grpc_channel_args** new_args) {
  return mapper->vtable->map_name(mapper, server_uri, args, name_to_resolve,
                                  new_args);
}

bool grpc_proxy_mapper_map_address(grpc_proxy_mapper* mapper,
                                   const grpc_resolved_address* address,
                                   const grpc_channel_args* args,
                                   grpc_resolved_address** new_address,
                                   grpc_channel_args** new_args) {
  return mapper->vtable->map_address(mapper, address, args, new_address,
                                     new_args);
}

void grpc_proxy_mapper_destroy(grpc_proxy_mapper* mapper) {
  mapper->vtable->destroy(mapper);
}
