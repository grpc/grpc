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

#include <grpc/support/log.h>

#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/security/transport/lb_targets_info.h"

/* Channel arg key for the mapping of LB server addresses to their names for
 * secure naming purposes. */
#define GRPC_ARG_LB_SECURE_NAMING_MAP "grpc.lb_secure_naming_map"

static void* targets_info_copy(void* p) {
  return grpc_slice_hash_table_ref((grpc_slice_hash_table*)p);
}
static void targets_info_destroy(void* p) {
  grpc_slice_hash_table_unref((grpc_slice_hash_table*)p);
}
static int targets_info_cmp(void* a, void* b) {
  return grpc_slice_hash_table_cmp((const grpc_slice_hash_table*)a,
                                   (const grpc_slice_hash_table*)b);
}
static const grpc_arg_pointer_vtable server_to_balancer_names_vtable = {
    targets_info_copy, targets_info_destroy, targets_info_cmp};

grpc_arg grpc_lb_targets_info_create_channel_arg(
    grpc_slice_hash_table* targets_info) {
  return grpc_channel_arg_pointer_create((char*)GRPC_ARG_LB_SECURE_NAMING_MAP,
                                         targets_info,
                                         &server_to_balancer_names_vtable);
}

grpc_slice_hash_table* grpc_lb_targets_info_find_in_args(
    const grpc_channel_args* args) {
  const grpc_arg* targets_info_arg =
      grpc_channel_args_find(args, GRPC_ARG_LB_SECURE_NAMING_MAP);
  if (targets_info_arg != nullptr) {
    GPR_ASSERT(targets_info_arg->type == GRPC_ARG_POINTER);
    return (grpc_slice_hash_table*)targets_info_arg->value.pointer.p;
  }
  return nullptr;
}
