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
#include "src/core/lib/security/transport/lb_targets_info.h"
#include "src/core/lib/slice/slice_internal.h"

static void destroy_balancer_name(void* balancer_name) {
  gpr_free(balancer_name);
}

static grpc_slice_hash_table_entry targets_info_entry_create(
    const char* address, const char* balancer_name) {
  grpc_slice_hash_table_entry entry;
  entry.key = grpc_slice_from_copied_string(address);
  entry.value = gpr_strdup(balancer_name);
  return entry;
}

static int balancer_name_cmp_fn(void* a, void* b) {
  const char* a_str = static_cast<const char*>(a);
  const char* b_str = static_cast<const char*>(b);
  return strcmp(a_str, b_str);
}

static grpc_slice_hash_table* build_targets_info_table(
    grpc_lb_addresses* addresses) {
  grpc_slice_hash_table_entry* targets_info_entries =
      static_cast<grpc_slice_hash_table_entry*>(
          gpr_zalloc(sizeof(*targets_info_entries) * addresses->num_addresses));
  for (size_t i = 0; i < addresses->num_addresses; ++i) {
    char* addr_str;
    GPR_ASSERT(grpc_sockaddr_to_string(
                   &addr_str, &addresses->addresses[i].address, true) > 0);
    targets_info_entries[i] = targets_info_entry_create(
        addr_str, addresses->addresses[i].balancer_name);
    gpr_free(addr_str);
  }
  grpc_slice_hash_table* targets_info = grpc_slice_hash_table_create(
      addresses->num_addresses, targets_info_entries, destroy_balancer_name,
      balancer_name_cmp_fn);
  gpr_free(targets_info_entries);
  return targets_info;
}

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
  grpc_slice_hash_table* targets_info = build_targets_info_table(addresses);
  args_to_add[num_args_to_add++] =
      grpc_lb_targets_info_create_channel_arg(targets_info);
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
  grpc_slice_hash_table_unref(targets_info);
  if (creds_sans_call_creds != nullptr) {
    grpc_channel_credentials_unref(creds_sans_call_creds);
  }
  return result;
}
