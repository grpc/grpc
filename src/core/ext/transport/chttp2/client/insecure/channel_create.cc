//
// Copyright 2015 gRPC authors.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//

#include <grpc/support/port_platform.h>

#include <string.h>

#include <grpc/grpc.h>
#include <grpc/support/alloc.h>
#include <grpc/support/string_util.h>

#include "src/core/ext/filters/client_channel/client_channel.h"
#include "src/core/ext/transport/chttp2/client/chttp2_connector.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/resolver/resolver_registry.h"
#include "src/core/lib/resource_quota/api.h"
#include "src/core/lib/surface/api_trace.h"
#include "src/core/lib/surface/channel.h"

namespace grpc_core {

class Chttp2InsecureClientChannelFactory : public ClientChannelFactory {
 public:
  RefCountedPtr<Subchannel> CreateSubchannel(
      const grpc_resolved_address& address,
      const grpc_channel_args* args) override {
    return Subchannel::Create(MakeOrphanable<Chttp2Connector>(), address, args);
  }
};

namespace {

grpc_channel* CreateChannel(const char* target, const grpc_channel_args* args,
                            grpc_error_handle* error) {
  if (target == nullptr) {
    gpr_log(GPR_ERROR, "cannot create channel with NULL target name");
    if (error != nullptr) {
      *error = GRPC_ERROR_CREATE_FROM_STATIC_STRING("channel target is NULL");
    }
    return nullptr;
  }
  // Add channel arg containing the server URI.
  UniquePtr<char> canonical_target =
      ResolverRegistry::AddDefaultPrefixIfNeeded(target);
  grpc_arg arg = grpc_channel_arg_string_create(
      const_cast<char*>(GRPC_ARG_SERVER_URI), canonical_target.get());
  const char* to_remove[] = {GRPC_ARG_SERVER_URI};
  grpc_channel_args* new_args0 =
      grpc_channel_args_copy_and_add_and_remove(args, to_remove, 1, &arg, 1);
  const grpc_channel_args* new_args = CoreConfiguration::Get()
                                          .channel_args_preconditioning()
                                          .PreconditionChannelArgs(new_args0);
  grpc_channel_args_destroy(new_args0);
  grpc_channel* channel = grpc_channel_create(
      target, new_args, GRPC_CLIENT_CHANNEL, nullptr, error);
  grpc_channel_args_destroy(new_args);
  return channel;
}

}  // namespace

}  // namespace grpc_core

namespace {

grpc_core::Chttp2InsecureClientChannelFactory* g_factory;
gpr_once g_factory_once = GPR_ONCE_INIT;

void FactoryInit() {
  g_factory = new grpc_core::Chttp2InsecureClientChannelFactory();
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
  const char* arg_to_remove = arg.key;
  grpc_channel_args* new_args = grpc_channel_args_copy_and_add_and_remove(
      args, &arg_to_remove, 1, &arg, 1);
  grpc_error_handle error = GRPC_ERROR_NONE;
  // Create channel.
  grpc_channel* channel = grpc_core::CreateChannel(target, new_args, &error);
  // Clean up.
  grpc_channel_args_destroy(new_args);
  if (channel == nullptr) {
    intptr_t integer;
    grpc_status_code status = GRPC_STATUS_INTERNAL;
    if (grpc_error_get_int(error, GRPC_ERROR_INT_GRPC_STATUS, &integer)) {
      status = static_cast<grpc_status_code>(integer);
    }
    GRPC_ERROR_UNREF(error);
    channel = grpc_lame_client_channel_create(
        target, status, "Failed to create client channel");
  }
  return channel;
}
