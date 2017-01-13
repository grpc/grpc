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

#include "src/core/ext/client_channel/client_channel_factory.h"

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
