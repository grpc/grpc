/*
 *
 * Copyright 2018 gRPC authors.
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

#include "src/core/ext/filters/client_channel/lb_policy/xds/xds_channel.h"

#include <grpc/support/alloc.h>
#include <grpc/support/string_util.h>
#include <string.h>

#include "src/core/ext/filters/client_channel/client_channel.h"
#include "src/core/ext/filters/client_channel/server_address.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/gpr/string.h"
#include "src/core/lib/iomgr/sockaddr_utils.h"
#include "src/core/lib/security/credentials/credentials.h"
#include "src/core/lib/security/transport/target_authority_table.h"
#include "src/core/lib/slice/slice_internal.h"

grpc_channel_args* grpc_lb_policy_xds_modify_lb_channel_args(
    grpc_channel_args* args) {
  const char* args_to_remove[1];
  size_t num_args_to_remove = 0;
  grpc_arg args_to_add[2];
  size_t num_args_to_add = 0;
  // Substitute the channel credentials with a version without call
  // credentials: the load balancer is not necessarily trusted to handle
  // bearer token credentials.
  grpc_channel_credentials* channel_credentials =
      grpc_channel_credentials_find_in_args(args);
  grpc_core::RefCountedPtr<grpc_channel_credentials> creds_sans_call_creds;
  if (channel_credentials != nullptr) {
    creds_sans_call_creds =
        channel_credentials->duplicate_without_call_credentials();
    GPR_ASSERT(creds_sans_call_creds != nullptr);
    args_to_remove[num_args_to_remove++] = GRPC_ARG_CHANNEL_CREDENTIALS;
    args_to_add[num_args_to_add++] =
        grpc_channel_credentials_to_arg(creds_sans_call_creds.get());
  }
  grpc_channel_args* result = grpc_channel_args_copy_and_add_and_remove(
      args, args_to_remove, num_args_to_remove, args_to_add, num_args_to_add);
  // Clean up.
  grpc_channel_args_destroy(args);
  return result;
}
