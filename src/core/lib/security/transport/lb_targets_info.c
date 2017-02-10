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

#include <grpc/support/log.h>

#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/security/transport/lb_targets_info.h"

/* Channel arg key for the mapping of LB server addresses to their names for
 * secure naming purposes. */
#define GRPC_ARG_LB_SECURE_NAMING_MAP "grpc.lb_secure_naming_map"

static void *targets_info_copy(void *p) { return grpc_slice_hash_table_ref(p); }
static void targets_info_destroy(grpc_exec_ctx *exec_ctx, void *p) {
  grpc_slice_hash_table_unref(exec_ctx, p);
}
static int targets_info_cmp(void *a, void *b) { return GPR_ICMP(a, b); }
static const grpc_arg_pointer_vtable server_to_balancer_names_vtable = {
    targets_info_copy, targets_info_destroy, targets_info_cmp};

grpc_arg grpc_lb_targets_info_create_channel_arg(
    grpc_slice_hash_table *targets_info) {
  grpc_arg arg;
  arg.type = GRPC_ARG_POINTER;
  arg.key = GRPC_ARG_LB_SECURE_NAMING_MAP;
  arg.value.pointer.p = targets_info;
  arg.value.pointer.vtable = &server_to_balancer_names_vtable;
  return arg;
}

grpc_slice_hash_table *grpc_lb_targets_info_find_in_args(
    const grpc_channel_args *args) {
  const grpc_arg *targets_info_arg =
      grpc_channel_args_find(args, GRPC_ARG_LB_SECURE_NAMING_MAP);
  if (targets_info_arg != NULL) {
    GPR_ASSERT(targets_info_arg->type == GRPC_ARG_POINTER);
    return targets_info_arg->value.pointer.p;
  }
  return NULL;
}
