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

#include <grpc/grpc.h>

#include <string.h>

#include <grpc/support/alloc.h>
#include <grpc/support/string_util.h>

#include "src/core/ext/filters/client_channel/client_channel.h"
#include "src/core/ext/filters/client_channel/resolver_registry.h"
#include "src/core/ext/filters/client_channel/uri_parser.h"
#include "src/core/ext/transport/chttp2/client/chttp2_connector.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/iomgr/sockaddr_utils.h"
#include "src/core/lib/security/credentials/credentials.h"
#include "src/core/lib/security/transport/lb_targets_info.h"
#include "src/core/lib/security/transport/security_connector.h"
#include "src/core/lib/slice/slice_hash_table.h"
#include "src/core/lib/slice/slice_internal.h"
#include "src/core/lib/surface/api_trace.h"
#include "src/core/lib/surface/channel.h"

static void client_channel_factory_ref(
    grpc_client_channel_factory* cc_factory) {}

static void client_channel_factory_unref(
    grpc_exec_ctx* exec_ctx, grpc_client_channel_factory* cc_factory) {}

static grpc_subchannel_args* get_secure_naming_subchannel_args(
    grpc_exec_ctx* exec_ctx, const grpc_subchannel_args* args) {
  grpc_channel_credentials* channel_credentials =
      grpc_channel_credentials_find_in_args(args->args);
  if (channel_credentials == nullptr) {
    gpr_log(GPR_ERROR,
            "Can't create subchannel: channel credentials missing for secure "
            "channel.");
    return nullptr;
  }
  // Make sure security connector does not already exist in args.
  if (grpc_security_connector_find_in_args(args->args) != nullptr) {
    gpr_log(GPR_ERROR,
            "Can't create subchannel: security connector already present in "
            "channel args.");
    return nullptr;
  }
  // To which address are we connecting? By default, use the server URI.
  const grpc_arg* server_uri_arg =
      grpc_channel_args_find(args->args, GRPC_ARG_SERVER_URI);
  GPR_ASSERT(server_uri_arg != nullptr);
  GPR_ASSERT(server_uri_arg->type == GRPC_ARG_STRING);
  const char* server_uri_str = server_uri_arg->value.string;
  GPR_ASSERT(server_uri_str != nullptr);
  grpc_uri* server_uri =
      grpc_uri_parse(exec_ctx, server_uri_str, true /* supress errors */);
  GPR_ASSERT(server_uri != nullptr);
  const char* server_uri_path;
  server_uri_path =
      server_uri->path[0] == '/' ? server_uri->path + 1 : server_uri->path;
  const grpc_slice_hash_table* targets_info =
      grpc_lb_targets_info_find_in_args(args->args);
  char* target_name_to_check = nullptr;
  if (targets_info != nullptr) {  // LB channel
    // Find the balancer name for the target.
    const char* target_uri_str =
        grpc_get_subchannel_address_uri_arg(args->args);
    grpc_uri* target_uri =
        grpc_uri_parse(exec_ctx, target_uri_str, false /* suppress errors */);
    GPR_ASSERT(target_uri != nullptr);
    if (target_uri->path[0] != '\0') {  // "path" may be empty
      const grpc_slice key = grpc_slice_from_static_string(
          target_uri->path[0] == '/' ? target_uri->path + 1 : target_uri->path);
      const char* value =
          (const char*)grpc_slice_hash_table_get(targets_info, key);
      if (value != nullptr) target_name_to_check = gpr_strdup(value);
      grpc_slice_unref_internal(exec_ctx, key);
    }
    if (target_name_to_check == nullptr) {
      // If the target name to check hasn't already been set, fall back to using
      // SERVER_URI
      target_name_to_check = gpr_strdup(server_uri_path);
    }
    grpc_uri_destroy(target_uri);
  } else {  // regular channel: the secure name is the original server URI.
    target_name_to_check = gpr_strdup(server_uri_path);
  }
  grpc_uri_destroy(server_uri);
  GPR_ASSERT(target_name_to_check != nullptr);
  grpc_channel_security_connector* subchannel_security_connector = nullptr;
  // Create the security connector using the credentials and target name.
  grpc_channel_args* new_args_from_connector = nullptr;
  const grpc_security_status security_status =
      grpc_channel_credentials_create_security_connector(
          exec_ctx, channel_credentials, target_name_to_check, args->args,
          &subchannel_security_connector, &new_args_from_connector);
  if (security_status != GRPC_SECURITY_OK) {
    gpr_log(GPR_ERROR,
            "Failed to create secure subchannel for secure name '%s'",
            target_name_to_check);
    gpr_free(target_name_to_check);
    return nullptr;
  }
  gpr_free(target_name_to_check);
  grpc_arg new_security_connector_arg =
      grpc_security_connector_to_arg(&subchannel_security_connector->base);

  grpc_channel_args* new_args = grpc_channel_args_copy_and_add(
      new_args_from_connector != nullptr ? new_args_from_connector : args->args,
      &new_security_connector_arg, 1);
  GRPC_SECURITY_CONNECTOR_UNREF(exec_ctx, &subchannel_security_connector->base,
                                "lb_channel_create");
  if (new_args_from_connector != nullptr) {
    grpc_channel_args_destroy(exec_ctx, new_args_from_connector);
  }
  grpc_subchannel_args* final_sc_args =
      (grpc_subchannel_args*)gpr_malloc(sizeof(*final_sc_args));
  memcpy(final_sc_args, args, sizeof(*args));
  final_sc_args->args = new_args;
  return final_sc_args;
}

static grpc_subchannel* client_channel_factory_create_subchannel(
    grpc_exec_ctx* exec_ctx, grpc_client_channel_factory* cc_factory,
    const grpc_subchannel_args* args) {
  grpc_subchannel_args* subchannel_args =
      get_secure_naming_subchannel_args(exec_ctx, args);
  if (subchannel_args == nullptr) {
    gpr_log(
        GPR_ERROR,
        "Failed to create subchannel arguments during subchannel creation.");
    return nullptr;
  }
  grpc_connector* connector = grpc_chttp2_connector_create();
  grpc_subchannel* s =
      grpc_subchannel_create(exec_ctx, connector, subchannel_args);
  grpc_connector_unref(exec_ctx, connector);
  grpc_channel_args_destroy(exec_ctx,
                            (grpc_channel_args*)subchannel_args->args);
  gpr_free(subchannel_args);
  return s;
}

static grpc_channel* client_channel_factory_create_channel(
    grpc_exec_ctx* exec_ctx, grpc_client_channel_factory* cc_factory,
    const char* target, grpc_client_channel_type type,
    const grpc_channel_args* args) {
  if (target == nullptr) {
    gpr_log(GPR_ERROR, "cannot create channel with NULL target name");
    return nullptr;
  }
  // Add channel arg containing the server URI.
  grpc_arg arg = grpc_channel_arg_string_create(
      (char*)GRPC_ARG_SERVER_URI,
      grpc_resolver_factory_add_default_prefix_if_needed(exec_ctx, target));
  const char* to_remove[] = {GRPC_ARG_SERVER_URI};
  grpc_channel_args* new_args =
      grpc_channel_args_copy_and_add_and_remove(args, to_remove, 1, &arg, 1);
  gpr_free(arg.value.string);
  grpc_channel* channel = grpc_channel_create(exec_ctx, target, new_args,
                                              GRPC_CLIENT_CHANNEL, nullptr);
  grpc_channel_args_destroy(exec_ctx, new_args);
  return channel;
}

static const grpc_client_channel_factory_vtable client_channel_factory_vtable =
    {client_channel_factory_ref, client_channel_factory_unref,
     client_channel_factory_create_subchannel,
     client_channel_factory_create_channel};

static grpc_client_channel_factory client_channel_factory = {
    &client_channel_factory_vtable};

// Create a secure client channel:
//   Asynchronously: - resolve target
//                   - connect to it (trying alternatives as presented)
//                   - perform handshakes
grpc_channel* grpc_secure_channel_create(grpc_channel_credentials* creds,
                                         const char* target,
                                         const grpc_channel_args* args,
                                         void* reserved) {
  grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
  GRPC_API_TRACE(
      "grpc_secure_channel_create(creds=%p, target=%s, args=%p, "
      "reserved=%p)",
      4, ((void*)creds, target, (void*)args, (void*)reserved));
  GPR_ASSERT(reserved == nullptr);
  grpc_channel* channel = nullptr;
  if (creds != nullptr) {
    // Add channel args containing the client channel factory and channel
    // credentials.
    grpc_arg args_to_add[] = {
        grpc_client_channel_factory_create_channel_arg(&client_channel_factory),
        grpc_channel_credentials_to_arg(creds)};
    grpc_channel_args* new_args = grpc_channel_args_copy_and_add(
        args, args_to_add, GPR_ARRAY_SIZE(args_to_add));
    // Create channel.
    channel = client_channel_factory_create_channel(
        &exec_ctx, &client_channel_factory, target,
        GRPC_CLIENT_CHANNEL_TYPE_REGULAR, new_args);
    // Clean up.
    grpc_channel_args_destroy(&exec_ctx, new_args);
    grpc_exec_ctx_finish(&exec_ctx);
  }
  return channel != nullptr ? channel
                            : grpc_lame_client_channel_create(
                                  target, GRPC_STATUS_INTERNAL,
                                  "Failed to create secure client channel");
}
