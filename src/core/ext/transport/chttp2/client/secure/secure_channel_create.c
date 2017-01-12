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

#include "src/core/ext/client_channel/client_channel.h"
#include "src/core/ext/transport/chttp2/client/chttp2_connector.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/security/credentials/credentials.h"
#include "src/core/lib/security/transport/security_connector.h"
#include "src/core/lib/surface/api_trace.h"
#include "src/core/lib/surface/channel.h"

static void client_channel_factory_ref(
    grpc_client_channel_factory *cc_factory) {}

static void client_channel_factory_unref(
    grpc_exec_ctx *exec_ctx, grpc_client_channel_factory *cc_factory) {}

static grpc_subchannel *client_channel_factory_create_subchannel(
    grpc_exec_ctx *exec_ctx, grpc_client_channel_factory *cc_factory,
    const grpc_subchannel_args *args) {
  grpc_connector *connector = grpc_chttp2_connector_create();
  grpc_subchannel *s = grpc_subchannel_create(exec_ctx, connector, args);
  grpc_connector_unref(exec_ctx, connector);
  return s;
}

static grpc_channel *client_channel_factory_create_channel(
    grpc_exec_ctx *exec_ctx, grpc_client_channel_factory *cc_factory,
    const char *target, grpc_client_channel_type type,
    const grpc_channel_args *args) {
  // Add channel arg containing the server URI.
  grpc_arg arg;
  arg.type = GRPC_ARG_STRING;
  arg.key = GRPC_ARG_SERVER_URI;
  arg.value.string = (char *)target;
  grpc_channel_args *new_args = grpc_channel_args_copy_and_add(args, &arg, 1);
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

/* Create a secure client channel:
   Asynchronously: - resolve target
                   - connect to it (trying alternatives as presented)
                   - perform handshakes */
grpc_channel *grpc_secure_channel_create(grpc_channel_credentials *creds,
                                         const char *target,
                                         const grpc_channel_args *args,
                                         void *reserved) {
  grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
  GRPC_API_TRACE(
      "grpc_secure_channel_create(creds=%p, target=%s, args=%p, "
      "reserved=%p)",
      4, (creds, target, args, reserved));
  GPR_ASSERT(reserved == NULL);
  // Make sure security connector does not already exist in args.
  if (grpc_find_security_connector_in_args(args) != NULL) {
    gpr_log(GPR_ERROR, "Cannot set security context in channel args.");
    grpc_exec_ctx_finish(&exec_ctx);
    return grpc_lame_client_channel_create(
        target, GRPC_STATUS_INTERNAL,
        "Security connector exists in channel args.");
  }
  // Create security connector and construct new channel args.
  grpc_channel_security_connector *security_connector;
  grpc_channel_args *new_args_from_connector;
  if (grpc_channel_credentials_create_security_connector(
          &exec_ctx, creds, target, args, &security_connector,
          &new_args_from_connector) != GRPC_SECURITY_OK) {
    grpc_exec_ctx_finish(&exec_ctx);
    return grpc_lame_client_channel_create(
        target, GRPC_STATUS_INTERNAL, "Failed to create security connector.");
  }
  // Add channel args containing the client channel factory and security
  // connector.
  grpc_arg args_to_add[2];
  args_to_add[0] =
      grpc_client_channel_factory_create_channel_arg(&client_channel_factory);
  args_to_add[1] = grpc_security_connector_to_arg(&security_connector->base);
  grpc_channel_args *new_args = grpc_channel_args_copy_and_add(
      new_args_from_connector != NULL ? new_args_from_connector : args,
      args_to_add, GPR_ARRAY_SIZE(args_to_add));
  if (new_args_from_connector != NULL) {
    grpc_channel_args_destroy(&exec_ctx, new_args_from_connector);
  }
  // Create channel.
  grpc_channel *channel = client_channel_factory_create_channel(
      &exec_ctx, &client_channel_factory, target,
      GRPC_CLIENT_CHANNEL_TYPE_REGULAR, new_args);
  // Clean up.
  GRPC_SECURITY_CONNECTOR_UNREF(&exec_ctx, &security_connector->base,
                                "secure_client_channel_factory_create_channel");
  grpc_channel_args_destroy(&exec_ctx, new_args);
  grpc_exec_ctx_finish(&exec_ctx);
  return channel; /* may be NULL */
}
