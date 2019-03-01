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

#include <grpc/support/port_platform.h>

#include <grpc/grpc.h>

#include <string.h>

#include <grpc/support/alloc.h>
#include <grpc/support/string_util.h>

#include "src/core/ext/filters/client_channel/client_channel.h"
#include "src/core/ext/filters/client_channel/resolver_registry.h"
#include "src/core/ext/transport/chttp2/client/authority.h"
#include "src/core/ext/transport/chttp2/client/chttp2_connector.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/surface/api_trace.h"
#include "src/core/lib/surface/channel.h"

static void client_channel_factory_ref(
    grpc_client_channel_factory* cc_factory) {}

static void client_channel_factory_unref(
    grpc_client_channel_factory* cc_factory) {}

static grpc_core::Subchannel* client_channel_factory_create_subchannel(
    grpc_client_channel_factory* cc_factory, const grpc_channel_args* args) {
  grpc_channel_args* new_args = grpc_default_authority_add_if_not_present(args);
  grpc_connector* connector = grpc_chttp2_connector_create();
  grpc_core::Subchannel* s = grpc_core::Subchannel::Create(connector, new_args);
  grpc_connector_unref(connector);
  grpc_channel_args_destroy(new_args);
  return s;
}

static grpc_channel* client_channel_factory_create_channel(
    grpc_client_channel_factory* cc_factory, const char* target,
    grpc_client_channel_type type, const grpc_channel_args* args) {
  if (target == nullptr) {
    gpr_log(GPR_ERROR, "cannot create channel with NULL target name");
    return nullptr;
  }
  // Add channel arg containing the server URI.
  grpc_core::UniquePtr<char> canonical_target =
      grpc_core::ResolverRegistry::AddDefaultPrefixIfNeeded(target);
  grpc_arg arg = grpc_channel_arg_string_create(
      const_cast<char*>(GRPC_ARG_SERVER_URI), canonical_target.get());
  const char* to_remove[] = {GRPC_ARG_SERVER_URI};
  grpc_channel_args* new_args =
      grpc_channel_args_copy_and_add_and_remove(args, to_remove, 1, &arg, 1);
  grpc_channel* channel =
      grpc_channel_create(target, new_args, GRPC_CLIENT_CHANNEL, nullptr);
  grpc_channel_args_destroy(new_args);
  return channel;
}

static const grpc_client_channel_factory_vtable client_channel_factory_vtable =
    {client_channel_factory_ref, client_channel_factory_unref,
     client_channel_factory_create_subchannel,
     client_channel_factory_create_channel};

static grpc_client_channel_factory client_channel_factory = {
    &client_channel_factory_vtable};

/* Create a client channel:
   Asynchronously: - resolve target
                   - connect to it (trying alternatives as presented)
                   - perform handshakes */
grpc_channel* grpc_insecure_channel_create(const char* target,
                                           const grpc_channel_args* args,
                                           void* reserved) {
  grpc_core::ExecCtx exec_ctx;
  GRPC_API_TRACE(
      "grpc_insecure_channel_create(target=%s, args=%p, reserved=%p)", 3,
      (target, args, reserved));
  GPR_ASSERT(reserved == nullptr);
  // Add channel arg containing the client channel factory.
  grpc_arg arg =
      grpc_client_channel_factory_create_channel_arg(&client_channel_factory);
  grpc_channel_args* new_args = grpc_channel_args_copy_and_add(args, &arg, 1);
  // Create channel.
  grpc_channel* channel = client_channel_factory_create_channel(
      &client_channel_factory, target, GRPC_CLIENT_CHANNEL_TYPE_REGULAR,
      new_args);
  // Clean up.
  grpc_channel_args_destroy(new_args);

  return channel != nullptr ? channel
                            : grpc_lame_client_channel_create(
                                  target, GRPC_STATUS_INTERNAL,
                                  "Failed to create client channel");
}
