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

#include <string.h>

#include "absl/container/inlined_vector.h"

#include <grpc/grpc_security.h>
#include <grpc/support/alloc.h>
#include <grpc/support/string_util.h>

#include "src/core/ext/filters/client_channel/client_channel.h"
#include "src/core/ext/filters/client_channel/lb_policy/grpclb/grpclb_balancer_addresses.h"
#include "src/core/ext/filters/client_channel/lb_policy/grpclb/grpclb_channel.h"
#include "src/core/ext/filters/client_channel/server_address.h"
#include "src/core/lib/address_utils/sockaddr_utils.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/gpr/string.h"
#include "src/core/lib/security/credentials/credentials.h"
#include "src/core/lib/slice/slice_internal.h"

namespace grpc_core {

grpc_channel_args* ModifyGrpclbBalancerChannelArgs(grpc_channel_args* args) {
  absl::InlinedVector<const char*, 1> args_to_remove;
  absl::InlinedVector<grpc_arg, 1> args_to_add;
  // Substitute the channel credentials with a version without call
  // credentials: the load balancer is not necessarily trusted to handle
  // bearer token credentials.
  grpc_channel_credentials* channel_credentials =
      grpc_channel_credentials_find_in_args(args);
  RefCountedPtr<grpc_channel_credentials> creds_sans_call_creds;
  if (channel_credentials != nullptr) {
    creds_sans_call_creds =
        channel_credentials->duplicate_without_call_credentials();
    GPR_ASSERT(creds_sans_call_creds != nullptr);
    args_to_remove.emplace_back(GRPC_ARG_CHANNEL_CREDENTIALS);
    args_to_add.emplace_back(
        grpc_channel_credentials_to_arg(creds_sans_call_creds.get()));
  }
  grpc_channel_args* result = grpc_channel_args_copy_and_add_and_remove(
      args, args_to_remove.data(), args_to_remove.size(), args_to_add.data(),
      args_to_add.size());
  // Clean up.
  grpc_channel_args_destroy(args);
  return result;
}

grpc_channel* CreateGrpclbBalancerChannel(const char* target_uri,
                                          const grpc_channel_args& args) {
  grpc_channel_credentials* creds =
      grpc_channel_credentials_find_in_args(&args);
  if (creds == nullptr) {
    // Build with security but parent channel is insecure.
    return grpc_insecure_channel_create(target_uri, &args, nullptr);
  }
  const char* arg_to_remove = GRPC_ARG_CHANNEL_CREDENTIALS;
  grpc_channel_args* new_args =
      grpc_channel_args_copy_and_remove(&args, &arg_to_remove, 1);
  grpc_channel* channel =
      grpc_secure_channel_create(creds, target_uri, new_args, nullptr);
  grpc_channel_args_destroy(new_args);
  return channel;
}

}  // namespace grpc_core
