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

namespace grpc_core {

class Chttp2InsecureClientChannelFactory : public ClientChannelFactory {
 public:
  Subchannel* CreateSubchannel(const grpc_channel_args* args) override {
    grpc_channel_args* new_args =
        grpc_default_authority_add_if_not_present(args);
    grpc_connector* connector = grpc_chttp2_connector_create();
    Subchannel* s = Subchannel::Create(connector, new_args);
    grpc_connector_unref(connector);
    grpc_channel_args_destroy(new_args);
    return s;
  }
};

namespace {

grpc_channel* CreateChannel(const char* target, const grpc_channel_args* args) {
  if (target == nullptr) {
    gpr_log(GPR_ERROR, "cannot create channel with NULL target name");
    return nullptr;
  }
  // Add channel arg containing the server URI.
  UniquePtr<char> canonical_target =
      ResolverRegistry::AddDefaultPrefixIfNeeded(target);
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

}  // namespace

}  // namespace grpc_core

namespace {

grpc_core::Chttp2InsecureClientChannelFactory* g_factory;
gpr_once g_factory_once = GPR_ONCE_INIT;

void FactoryInit() {
  g_factory = grpc_core::New<grpc_core::Chttp2InsecureClientChannelFactory>();
}

}  // namespace

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
  gpr_once_init(&g_factory_once, FactoryInit);
  grpc_arg arg = grpc_core::ClientChannelFactory::CreateChannelArg(g_factory);
  grpc_channel_args* new_args = grpc_channel_args_copy_and_add(args, &arg, 1);
  // Create channel.
  grpc_channel* channel = grpc_core::CreateChannel(target, new_args);
  // Clean up.
  grpc_channel_args_destroy(new_args);
  return channel != nullptr ? channel
                            : grpc_lame_client_channel_create(
                                  target, GRPC_STATUS_INTERNAL,
                                  "Failed to create client channel");
}
