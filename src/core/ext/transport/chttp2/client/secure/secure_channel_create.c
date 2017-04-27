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
    grpc_client_channel_factory *cc_factory) {}

static void client_channel_factory_unref(
    grpc_exec_ctx *exec_ctx, grpc_client_channel_factory *cc_factory) {}

static grpc_subchannel_args *get_secure_naming_subchannel_args(
    grpc_exec_ctx *exec_ctx, const grpc_subchannel_args *args) {
  grpc_channel_credentials *channel_credentials =
      grpc_channel_credentials_find_in_args(args->args);
  if (channel_credentials == NULL) {
    gpr_log(GPR_ERROR,
            "Can't create subchannel: channel credentials missing for secure "
            "channel.");
    return NULL;
  }
  // Make sure security connector does not already exist in args.
  if (grpc_security_connector_find_in_args(args->args) != NULL) {
    gpr_log(GPR_ERROR,
            "Can't create subchannel: security connector already present in "
            "channel args.");
    return NULL;
  }
  // To which address are we connecting? By default, use the server URI.
  const grpc_arg *server_uri_arg =
      grpc_channel_args_find(args->args, GRPC_ARG_SERVER_URI);
  GPR_ASSERT(server_uri_arg != NULL);
  GPR_ASSERT(server_uri_arg->type == GRPC_ARG_STRING);
  const char *server_uri_str = server_uri_arg->value.string;
  GPR_ASSERT(server_uri_str != NULL);
  grpc_uri *server_uri =
      grpc_uri_parse(exec_ctx, server_uri_str, true /* supress errors */);
  GPR_ASSERT(server_uri != NULL);
  const char *server_uri_path;
  server_uri_path =
      server_uri->path[0] == '/' ? server_uri->path + 1 : server_uri->path;
  const grpc_slice_hash_table *targets_info =
      grpc_lb_targets_info_find_in_args(args->args);
  char *target_name_to_check = NULL;
  if (targets_info != NULL) {  // LB channel
    // Find the balancer name for the target.
    const char *target_uri_str =
        grpc_get_subchannel_address_uri_arg(args->args);
    grpc_uri *target_uri =
        grpc_uri_parse(exec_ctx, target_uri_str, false /* suppress errors */);
    GPR_ASSERT(target_uri != NULL);
    if (target_uri->path[0] != '\0') {  // "path" may be empty
      const grpc_slice key = grpc_slice_from_static_string(
          target_uri->path[0] == '/' ? target_uri->path + 1 : target_uri->path);
      const char *value = grpc_slice_hash_table_get(targets_info, key);
      if (value != NULL) target_name_to_check = gpr_strdup(value);
      grpc_slice_unref_internal(exec_ctx, key);
    }
    if (target_name_to_check == NULL) {
      // If the target name to check hasn't already been set, fall back to using
      // SERVER_URI
      target_name_to_check = gpr_strdup(server_uri_path);
    }
    grpc_uri_destroy(target_uri);
  } else {  // regular channel: the secure name is the original server URI.
    target_name_to_check = gpr_strdup(server_uri_path);
  }
  grpc_uri_destroy(server_uri);
  GPR_ASSERT(target_name_to_check != NULL);
  grpc_channel_security_connector *subchannel_security_connector = NULL;
  // Create the security connector using the credentials and target name.
  grpc_channel_args *new_args_from_connector = NULL;
  const grpc_security_status security_status =
      grpc_channel_credentials_create_security_connector(
          exec_ctx, channel_credentials, target_name_to_check, args->args,
          &subchannel_security_connector, &new_args_from_connector);
  if (security_status != GRPC_SECURITY_OK) {
    gpr_log(GPR_ERROR,
            "Failed to create secure subchannel for secure name '%s'",
            target_name_to_check);
    gpr_free(target_name_to_check);
    return NULL;
  }
  gpr_free(target_name_to_check);
  grpc_arg new_security_connector_arg =
      grpc_security_connector_to_arg(&subchannel_security_connector->base);

  grpc_channel_args *new_args = grpc_channel_args_copy_and_add(
      new_args_from_connector != NULL ? new_args_from_connector : args->args,
      &new_security_connector_arg, 1);
  GRPC_SECURITY_CONNECTOR_UNREF(exec_ctx, &subchannel_security_connector->base,
                                "lb_channel_create");
  if (new_args_from_connector != NULL) {
    grpc_channel_args_destroy(exec_ctx, new_args_from_connector);
  }
  grpc_subchannel_args *final_sc_args = gpr_malloc(sizeof(*final_sc_args));
  memcpy(final_sc_args, args, sizeof(*args));
  final_sc_args->args = new_args;
  return final_sc_args;
}

static grpc_subchannel *client_channel_factory_create_subchannel(
    grpc_exec_ctx *exec_ctx, grpc_client_channel_factory *cc_factory,
    const grpc_subchannel_args *args) {
  grpc_subchannel_args *subchannel_args =
      get_secure_naming_subchannel_args(exec_ctx, args);
  if (subchannel_args == NULL) {
    gpr_log(
        GPR_ERROR,
        "Failed to create subchannel arguments during subchannel creation.");
    return NULL;
  }
  grpc_connector *connector = grpc_chttp2_connector_create();
  grpc_subchannel *s =
      grpc_subchannel_create(exec_ctx, connector, subchannel_args);
  grpc_connector_unref(exec_ctx, connector);
  grpc_channel_args_destroy(exec_ctx,
                            (grpc_channel_args *)subchannel_args->args);
  gpr_free(subchannel_args);
  return s;
}

static grpc_channel *client_channel_factory_create_channel(
    grpc_exec_ctx *exec_ctx, grpc_client_channel_factory *cc_factory,
    const char *target, grpc_client_channel_type type,
    const grpc_channel_args *args) {
  if (target == NULL) {
    gpr_log(GPR_ERROR, "cannot create channel with NULL target name");
    return NULL;
  }
  // Add channel arg containing the server URI.
  grpc_arg arg;
  arg.type = GRPC_ARG_STRING;
  arg.key = GRPC_ARG_SERVER_URI;
  arg.value.string =
      grpc_resolver_factory_add_default_prefix_if_needed(exec_ctx, target);
  const char *to_remove[] = {GRPC_ARG_SERVER_URI};
  grpc_channel_args *new_args =
      grpc_channel_args_copy_and_add_and_remove(args, to_remove, 1, &arg, 1);
  gpr_free(arg.value.string);
  grpc_channel *channel = grpc_channel_create(exec_ctx, target, new_args,
                                              GRPC_CLIENT_CHANNEL, NULL);
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
grpc_channel *grpc_secure_channel_create(grpc_channel_credentials *creds,
                                         const char *target,
                                         const grpc_channel_args *args,
                                         void *reserved) {
  grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
  GRPC_API_TRACE(
      "grpc_secure_channel_create(creds=%p, target=%s, args=%p, "
      "reserved=%p)",
      4, ((void *)creds, target, (void *)args, (void *)reserved));
  GPR_ASSERT(reserved == NULL);
  grpc_channel *channel = NULL;
  if (creds != NULL) {
    // Add channel args containing the client channel factory and channel
    // credentials.
    grpc_arg args_to_add[] = {
        grpc_client_channel_factory_create_channel_arg(&client_channel_factory),
        grpc_channel_credentials_to_arg(creds)};
    grpc_channel_args *new_args = grpc_channel_args_copy_and_add(
        args, args_to_add, GPR_ARRAY_SIZE(args_to_add));
    // Create channel.
    channel = client_channel_factory_create_channel(
        &exec_ctx, &client_channel_factory, target,
        GRPC_CLIENT_CHANNEL_TYPE_REGULAR, new_args);
    // Clean up.
    grpc_channel_args_destroy(&exec_ctx, new_args);
    grpc_exec_ctx_finish(&exec_ctx);
  }
  return channel != NULL ? channel
                         : grpc_lame_client_channel_create(
                               target, GRPC_STATUS_INTERNAL,
                               "Failed to create secure client channel");
}
