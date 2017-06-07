/*
 *
 * Copyright 2017, Google Inc.
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

#include <grpc/support/alloc.h>
#include <grpc/support/string_util.h>

#include "src/core/ext/filters/client_channel/client_channel.h"
#include "src/core/ext/filters/client_channel/lb_policy/grpclb/grpclb_channel.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/iomgr/sockaddr_utils.h"
#include "src/core/lib/security/credentials/credentials.h"
#include "src/core/lib/security/transport/lb_targets_info.h"
#include "src/core/lib/slice/slice_internal.h"
#include "src/core/lib/support/string.h"

grpc_channel *grpc_lb_policy_grpclb_create_lb_channel(
    grpc_exec_ctx *exec_ctx, const char *lb_service_target_addresses,
    grpc_client_channel_factory *client_channel_factory,
    grpc_channel_args *args) {
  grpc_channel_args *new_args = args;
  grpc_channel_credentials *channel_credentials =
      grpc_channel_credentials_find_in_args(args);
  if (channel_credentials != NULL) {
    /* Substitute the channel credentials with a version without call
     * credentials: the load balancer is not necessarily trusted to handle
     * bearer token credentials */
    static const char *keys_to_remove[] = {GRPC_ARG_CHANNEL_CREDENTIALS};
    grpc_channel_credentials *creds_sans_call_creds =
        grpc_channel_credentials_duplicate_without_call_credentials(
            channel_credentials);
    GPR_ASSERT(creds_sans_call_creds != NULL);
    grpc_arg args_to_add[] = {
        grpc_channel_credentials_to_arg(creds_sans_call_creds)};
    /* Create the new set of channel args */
    new_args = grpc_channel_args_copy_and_add_and_remove(
        args, keys_to_remove, GPR_ARRAY_SIZE(keys_to_remove), args_to_add,
        GPR_ARRAY_SIZE(args_to_add));
    grpc_channel_credentials_unref(exec_ctx, creds_sans_call_creds);
  }
  grpc_channel *lb_channel = grpc_client_channel_factory_create_channel(
      exec_ctx, client_channel_factory, lb_service_target_addresses,
      GRPC_CLIENT_CHANNEL_TYPE_LOAD_BALANCING, new_args);
  if (channel_credentials != NULL) {
    grpc_channel_args_destroy(exec_ctx, new_args);
  }
  return lb_channel;
}

grpc_channel_args *grpc_lb_policy_grpclb_build_lb_channel_args(
    grpc_exec_ctx *exec_ctx, grpc_slice_hash_table *targets_info,
    grpc_fake_resolver_response_generator *response_generator,
    const grpc_channel_args *args) {
  const grpc_arg to_add[] = {
      grpc_lb_targets_info_create_channel_arg(targets_info),
      grpc_fake_resolver_response_generator_arg(response_generator)};
  /* We remove:
   *
   * - The channel arg for the LB policy name, since we want to use the default
   *   (pick_first) in this case.
   *
   * - The channel arg for the resolved addresses, since that will be generated
   *   by the name resolver used in the LB channel.  Note that the LB channel
   *   will use the fake resolver, so this won't actually generate a query
   *   to DNS (or some other name service).  However, the addresses returned by
   *   the fake resolver will have is_balancer=false, whereas our own
   *   addresses have is_balancer=true.  We need the LB channel to return
   *   addresses with is_balancer=false so that it does not wind up recursively
   *   using the grpclb LB policy, as per the special case logic in
   *   client_channel.c.
   *
   * - The channel arg for the server URI, since that will be different for the
   *   LB channel than for the parent channel (the client channel factory will
   *   re-add this arg with the right value).
   *
   * - The fake resolver generator, because we are replacing it with the one
   *   from the grpclb policy, used to propagate updates to the LB channel. */
  static const char *keys_to_remove[] = {
      GRPC_ARG_LB_POLICY_NAME, GRPC_ARG_LB_ADDRESSES, GRPC_ARG_SERVER_URI,
      GRPC_ARG_FAKE_RESOLVER_RESPONSE_GENERATOR};
  /* Add the targets info table to be used for secure naming */
  return grpc_channel_args_copy_and_add_and_remove(
      args, keys_to_remove, GPR_ARRAY_SIZE(keys_to_remove), to_add,
      GPR_ARRAY_SIZE(to_add));
}
