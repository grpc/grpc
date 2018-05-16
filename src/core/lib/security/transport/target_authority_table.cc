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

#include <grpc/support/log.h>

#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/security/transport/target_authority_table.h"

// Channel arg key for the mapping of target addresses to their authorities.
#define GRPC_ARG_TARGET_AUTHORITY_TABLE "grpc.target_authority_table"

namespace grpc_core {
namespace {

void* target_authority_table_copy(void* p) {
  TargetAuthorityTable* table = static_cast<TargetAuthorityTable*>(p);
  // TODO(roth): When channel_args are converted to C++, pass the
  // RefCountedPtr<> directly instead of managing the ref manually.
  table->Ref().release();
  return p;
}
void target_authority_table_destroy(void* p) {
  TargetAuthorityTable* table = static_cast<TargetAuthorityTable*>(p);
  table->Unref();
}
int target_authority_table_cmp(void* a, void* b) {
  return TargetAuthorityTable::Cmp(
      *static_cast<const TargetAuthorityTable*>(a),
      *static_cast<const TargetAuthorityTable*>(b));
}
const grpc_arg_pointer_vtable target_authority_table_arg_vtable = {
    target_authority_table_copy, target_authority_table_destroy,
    target_authority_table_cmp};

}  // namespace

grpc_arg CreateTargetAuthorityTableChannelArg(TargetAuthorityTable* table) {
  return grpc_channel_arg_pointer_create((char*)GRPC_ARG_TARGET_AUTHORITY_TABLE,
                                         table,
                                         &target_authority_table_arg_vtable);
}

TargetAuthorityTable* FindTargetAuthorityTableInArgs(
    const grpc_channel_args* args) {
  const grpc_arg* arg =
      grpc_channel_args_find(args, GRPC_ARG_TARGET_AUTHORITY_TABLE);
  if (arg != nullptr) {
    if (arg->type == GRPC_ARG_POINTER) {
      return static_cast<TargetAuthorityTable*>(arg->value.pointer.p);
    } else {
      gpr_log(GPR_ERROR, "value of " GRPC_ARG_TARGET_AUTHORITY_TABLE
                         " channel arg was not pointer type; ignoring");
    }
  }
  return nullptr;
}

}  // namespace grpc_core
