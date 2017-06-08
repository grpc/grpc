/*
 *
 * Copyright 2015 gRPC authors.
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

#include "src/core/ext/filters/client_channel/client_channel_factory.h"

void grpc_client_channel_factory_ref(grpc_client_channel_factory* factory) {
  factory->vtable->ref(factory);
}

void grpc_client_channel_factory_unref(grpc_exec_ctx* exec_ctx,
                                       grpc_client_channel_factory* factory) {
  factory->vtable->unref(exec_ctx, factory);
}

grpc_subchannel* grpc_client_channel_factory_create_subchannel(
    grpc_exec_ctx* exec_ctx, grpc_client_channel_factory* factory,
    const grpc_subchannel_args* args) {
  return factory->vtable->create_subchannel(exec_ctx, factory, args);
}

grpc_channel* grpc_client_channel_factory_create_channel(
    grpc_exec_ctx* exec_ctx, grpc_client_channel_factory* factory,
    const char* target, grpc_client_channel_type type,
    const grpc_channel_args* args) {
  return factory->vtable->create_client_channel(exec_ctx, factory, target, type,
                                                args);
}

static void* factory_arg_copy(void* factory) {
  grpc_client_channel_factory_ref(factory);
  return factory;
}

static void factory_arg_destroy(grpc_exec_ctx* exec_ctx, void* factory) {
  // TODO(roth): Remove local exec_ctx when
  // https://github.com/grpc/grpc/pull/8705 is merged.
  grpc_client_channel_factory_unref(exec_ctx, factory);
}

static int factory_arg_cmp(void* factory1, void* factory2) {
  if (factory1 < factory2) return -1;
  if (factory1 > factory2) return 1;
  return 0;
}

static const grpc_arg_pointer_vtable factory_arg_vtable = {
    factory_arg_copy, factory_arg_destroy, factory_arg_cmp};

grpc_arg grpc_client_channel_factory_create_channel_arg(
    grpc_client_channel_factory* factory) {
  grpc_arg arg;
  arg.type = GRPC_ARG_POINTER;
  arg.key = GRPC_ARG_CLIENT_CHANNEL_FACTORY;
  arg.value.pointer.p = factory;
  arg.value.pointer.vtable = &factory_arg_vtable;
  return arg;
}
