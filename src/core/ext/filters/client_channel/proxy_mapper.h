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

#ifndef GRPC_CORE_EXT_CLIENT_CHANNEL_PROXY_MAPPER_H
#define GRPC_CORE_EXT_CLIENT_CHANNEL_PROXY_MAPPER_H

#include <stdbool.h>

#include <grpc/impl/codegen/grpc_types.h>

#include "src/core/lib/iomgr/resolve_address.h"

typedef struct grpc_proxy_mapper grpc_proxy_mapper;

typedef struct {
  /// Determines the proxy name to resolve for \a server_uri.
  /// If no proxy is needed, returns false.
  /// Otherwise, sets \a name_to_resolve, optionally sets \a new_args,
  /// and returns true.
  bool (*map_name)(grpc_exec_ctx* exec_ctx, grpc_proxy_mapper* mapper,
                   const char* server_uri, const grpc_channel_args* args,
                   char** name_to_resolve, grpc_channel_args** new_args);
  /// Determines the proxy address to use to contact \a address.
  /// If no proxy is needed, returns false.
  /// Otherwise, sets \a new_address, optionally sets \a new_args, and
  /// returns true.
  bool (*map_address)(grpc_exec_ctx* exec_ctx, grpc_proxy_mapper* mapper,
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

bool grpc_proxy_mapper_map_name(grpc_exec_ctx* exec_ctx,
                                grpc_proxy_mapper* mapper,
                                const char* server_uri,
                                const grpc_channel_args* args,
                                char** name_to_resolve,
                                grpc_channel_args** new_args);

bool grpc_proxy_mapper_map_address(grpc_exec_ctx* exec_ctx,
                                   grpc_proxy_mapper* mapper,
                                   const grpc_resolved_address* address,
                                   const grpc_channel_args* args,
                                   grpc_resolved_address** new_address,
                                   grpc_channel_args** new_args);

void grpc_proxy_mapper_destroy(grpc_proxy_mapper* mapper);

#endif /* GRPC_CORE_EXT_CLIENT_CHANNEL_PROXY_MAPPER_H */
