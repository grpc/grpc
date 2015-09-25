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

#include "src/core/client_config/subchannel_factory_decorators/merge_channel_args.h"
#include <grpc/support/alloc.h>
#include "src/core/channel/channel_args.h"

typedef struct {
  grpc_subchannel_factory base;
  gpr_refcount refs;
  grpc_subchannel_factory *wrapped;
  grpc_channel_args *merge_args;
} merge_args_factory;

static void merge_args_factory_ref(grpc_subchannel_factory *scf) {
  merge_args_factory *f = (merge_args_factory *)scf;
  gpr_ref(&f->refs);
}

static void merge_args_factory_unref(grpc_exec_ctx *exec_ctx,
                                     grpc_subchannel_factory *scf) {
  merge_args_factory *f = (merge_args_factory *)scf;
  if (gpr_unref(&f->refs)) {
    grpc_subchannel_factory_unref(exec_ctx, f->wrapped);
    grpc_channel_args_destroy(f->merge_args);
    gpr_free(f);
  }
}

static grpc_subchannel *merge_args_factory_create_subchannel(
    grpc_exec_ctx *exec_ctx, grpc_subchannel_factory *scf,
    grpc_subchannel_args *args) {
  merge_args_factory *f = (merge_args_factory *)scf;
  grpc_channel_args *final_args =
      grpc_channel_args_merge(args->args, f->merge_args);
  grpc_subchannel *s;
  args->args = final_args;
  s = grpc_subchannel_factory_create_subchannel(exec_ctx, f->wrapped, args);
  grpc_channel_args_destroy(final_args);
  return s;
}

static const grpc_subchannel_factory_vtable merge_args_factory_vtable = {
    merge_args_factory_ref, merge_args_factory_unref,
    merge_args_factory_create_subchannel};

grpc_subchannel_factory *grpc_subchannel_factory_merge_channel_args(
    grpc_subchannel_factory *input, const grpc_channel_args *args) {
  merge_args_factory *f = gpr_malloc(sizeof(*f));
  f->base.vtable = &merge_args_factory_vtable;
  gpr_ref_init(&f->refs, 1);
  grpc_subchannel_factory_ref(input);
  f->wrapped = input;
  f->merge_args = grpc_channel_args_copy(args);
  return &f->base;
}
