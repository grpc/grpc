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

#include "src/core/ext/filters/client_channel/lb_policy/grpclb/grpclb_channel.h"

#include <string.h>

#include <grpc/support/alloc.h>
#include <grpc/support/string_util.h>

#include "src/core/ext/filters/client_channel/client_channel.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/gpr/string.h"
#include "src/core/lib/iomgr/sockaddr_utils.h"
#include "src/core/lib/security/credentials/credentials.h"
#include "src/core/lib/security/transport/target_authority_table.h"
#include "src/core/lib/slice/slice_internal.h"

namespace grpc_core {
namespace {

int BalancerNameCmp(const grpc_core::UniquePtr<char>& a,
                    const grpc_core::UniquePtr<char>& b) {
  return strcmp(a.get(), b.get());
}

RefCountedPtr<TargetAuthorityTable> CreateTargetAuthorityTable(
    grpc_lb_addresses* addresses) {
  TargetAuthorityTable::Entry* target_authority_entries =
      static_cast<TargetAuthorityTable::Entry*>(gpr_zalloc(
          sizeof(*target_authority_entries) * addresses->num_addresses));
  for (size_t i = 0; i < addresses->num_addresses; ++i) {
    char* addr_str;
    GPR_ASSERT(grpc_sockaddr_to_string(
                   &addr_str, &addresses->addresses[i].address, true) > 0);
    target_authority_entries[i].key = grpc_slice_from_copied_string(addr_str);
    target_authority_entries[i].value.reset(
        gpr_strdup(addresses->addresses[i].balancer_name));
    gpr_free(addr_str);
  }
  RefCountedPtr<TargetAuthorityTable> target_authority_table =
      TargetAuthorityTable::Create(addresses->num_addresses,
                                   target_authority_entries, BalancerNameCmp);
  gpr_free(target_authority_entries);
  return target_authority_table;
}

}  // namespace
}  // namespace grpc_core

grpc_channel_args* grpc_lb_policy_grpclb_modify_lb_channel_args(
    grpc_channel_args* args) {
  const char* args_to_remove[1];
  size_t num_args_to_remove = 0;
  grpc_arg args_to_add[2];
  size_t num_args_to_add = 0;
  // Add arg for targets info table.
  const grpc_arg* arg = grpc_channel_args_find(args, GRPC_ARG_LB_ADDRESSES);
  GPR_ASSERT(arg != nullptr);
  GPR_ASSERT(arg->type == GRPC_ARG_POINTER);
  grpc_lb_addresses* addresses =
      static_cast<grpc_lb_addresses*>(arg->value.pointer.p);
  grpc_core::RefCountedPtr<grpc_core::TargetAuthorityTable>
      target_authority_table = grpc_core::CreateTargetAuthorityTable(addresses);
  args_to_add[num_args_to_add++] =
      grpc_core::CreateTargetAuthorityTableChannelArg(
          target_authority_table.get());
  // Substitute the channel credentials with a version without call
  // credentials: the load balancer is not necessarily trusted to handle
  // bearer token credentials.
  grpc_channel_credentials* channel_credentials =
      grpc_channel_credentials_find_in_args(args);
  grpc_channel_credentials* creds_sans_call_creds = nullptr;
  if (channel_credentials != nullptr) {
    creds_sans_call_creds =
        grpc_channel_credentials_duplicate_without_call_credentials(
            channel_credentials);
    GPR_ASSERT(creds_sans_call_creds != nullptr);
    args_to_remove[num_args_to_remove++] = GRPC_ARG_CHANNEL_CREDENTIALS;
    args_to_add[num_args_to_add++] =
        grpc_channel_credentials_to_arg(creds_sans_call_creds);
  }
  grpc_channel_args* result = grpc_channel_args_copy_and_add_and_remove(
      args, args_to_remove, num_args_to_remove, args_to_add, num_args_to_add);
  // Clean up.
  grpc_channel_args_destroy(args);
  if (creds_sans_call_creds != nullptr) {
    grpc_channel_credentials_unref(creds_sans_call_creds);
  }
  return result;
}
