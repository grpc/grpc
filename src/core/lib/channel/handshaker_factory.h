/*
 *
 * Copyright 2016 gRPC authors.
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

#ifndef GRPC_CORE_LIB_CHANNEL_HANDSHAKER_FACTORY_H
#define GRPC_CORE_LIB_CHANNEL_HANDSHAKER_FACTORY_H

#include <grpc/impl/codegen/grpc_types.h>

#include "src/core/lib/channel/handshaker.h"
#include "src/core/lib/iomgr/exec_ctx.h"

#ifdef __cplusplus
extern "C" {
#endif

// A handshaker factory is used to create handshakers.

typedef struct grpc_handshaker_factory grpc_handshaker_factory;

typedef struct {
  void (*add_handshakers)(grpc_exec_ctx* exec_ctx,
                          grpc_handshaker_factory* handshaker_factory,
                          const grpc_channel_args* args,
                          grpc_handshake_manager* handshake_mgr);
  void (*destroy)(grpc_exec_ctx* exec_ctx,
                  grpc_handshaker_factory* handshaker_factory);
} grpc_handshaker_factory_vtable;

struct grpc_handshaker_factory {
  const grpc_handshaker_factory_vtable* vtable;
};

void grpc_handshaker_factory_add_handshakers(
    grpc_exec_ctx* exec_ctx, grpc_handshaker_factory* handshaker_factory,
    const grpc_channel_args* args, grpc_handshake_manager* handshake_mgr);

void grpc_handshaker_factory_destroy(
    grpc_exec_ctx* exec_ctx, grpc_handshaker_factory* handshaker_factory);

#ifdef __cplusplus
}
#endif

#endif /* GRPC_CORE_LIB_CHANNEL_HANDSHAKER_FACTORY_H */
