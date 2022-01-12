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
#include "src/core/lib/address_utils/sockaddr_utils.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/gprpp/memory.h"
#include "src/core/lib/resolver/resolver_registry.h"
#include "src/core/lib/resource_quota/api.h"
#include "src/core/lib/security/credentials/credentials.h"
#include "src/core/lib/security/security_connector/security_connector.h"
#include "src/core/lib/slice/slice_internal.h"
#include "src/core/lib/surface/api_trace.h"
#include "src/core/lib/surface/channel.h"
#include "src/core/lib/uri/uri_parser.h"

namespace grpc_core {

class Chttp2SecureClientChannelFactory : public ClientChannelFactory {
 public:
  RefCountedPtr<Subchannel> CreateSubchannel(
      const grpc_resolved_address& address,
      const grpc_channel_args* args) override {
    grpc_channel_args* new_args = GetSecureNamingChannelArgs(args);
    if (new_args == nullptr) {
      gpr_log(GPR_ERROR,
              "Failed to create channel args during subchannel creation.");
      return nullptr;
    }
    RefCountedPtr<Subchannel> s = Subchannel::Create(
        MakeOrphanable<Chttp2Connector>(), address, new_args);
    grpc_channel_args_destroy(new_args);
    return s;
  }

 private:
  static grpc_channel_args* GetSecureNamingChannelArgs(
      const grpc_channel_args* args) {
    grpc_channel_credentials* channel_credentials =
        grpc_channel_credentials_find_in_args(args);
    if (channel_credentials == nullptr) {
      gpr_log(GPR_ERROR,
              "Can't create subchannel: channel credentials missing for secure "
              "channel.");
      return nullptr;
    }
    // Make sure security connector does not already exist in args.
    if (grpc_security_connector_find_in_args(args) != nullptr) {
      gpr_log(GPR_ERROR,
              "Can't create subchannel: security connector already present in "
              "channel args.");
      return nullptr;
    }
    // Find the authority to use in the security connector.
    const char* authority =
        grpc_channel_args_find_string(args, GRPC_ARG_DEFAULT_AUTHORITY);
    GPR_ASSERT(authority != nullptr);
    // Create the security connector using the credentials and target name.
    grpc_channel_args* new_args_from_connector = nullptr;
    RefCountedPtr<grpc_channel_security_connector>
        subchannel_security_connector =
            channel_credentials->create_security_connector(
                /*call_creds=*/nullptr, authority, args,
                &new_args_from_connector);
    if (subchannel_security_connector == nullptr) {
      gpr_log(GPR_ERROR,
              "Failed to create secure subchannel for secure name '%s'",
              authority);
      return nullptr;
    }
    grpc_arg new_security_connector_arg =
        grpc_security_connector_to_arg(subchannel_security_connector.get());
    grpc_channel_args* new_args = grpc_channel_args_copy_and_add(
        new_args_from_connector != nullptr ? new_args_from_connector : args,
        &new_security_connector_arg, 1);
    subchannel_security_connector.reset(DEBUG_LOCATION, "lb_channel_create");
    grpc_channel_args_destroy(new_args_from_connector);
    return new_args;
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
  grpc_channel_args* new_args =
      grpc_channel_args_copy_and_add_and_remove(args, to_remove, 1, &arg, 1);
  grpc_channel* channel = grpc_channel_create(
      target, new_args, GRPC_CLIENT_CHANNEL, nullptr, error);
  grpc_channel_args_destroy(new_args);
  return channel;
}

}  // namespace

}  // namespace grpc_core

namespace {

grpc_core::Chttp2SecureClientChannelFactory* g_factory;
gpr_once g_factory_once = GPR_ONCE_INIT;

void FactoryInit() {
  g_factory = new grpc_core::Chttp2SecureClientChannelFactory();
}

}  // namespace

// Create a secure client channel:
//   Asynchronously: - resolve target
//                   - connect to it (trying alternatives as presented)
//                   - perform handshakes
grpc_channel* grpc_secure_channel_create(grpc_channel_credentials* creds,
                                         const char* target,
                                         const grpc_channel_args* args,
                                         void* reserved) {
  grpc_core::ExecCtx exec_ctx;
  GRPC_API_TRACE(
      "grpc_secure_channel_create(creds=%p, target=%s, args=%p, "
      "reserved=%p)",
      4, ((void*)creds, target, (void*)args, (void*)reserved));
  GPR_ASSERT(reserved == nullptr);
  args = grpc_core::CoreConfiguration::Get()
             .channel_args_preconditioning()
             .PreconditionChannelArgs(args);
  grpc_channel* channel = nullptr;
  grpc_error_handle error = GRPC_ERROR_NONE;
  if (creds != nullptr) {
    // Add channel args containing the client channel factory and channel
    // credentials.
    gpr_once_init(&g_factory_once, FactoryInit);
    grpc_arg channel_factory_arg =
        grpc_core::ClientChannelFactory::CreateChannelArg(g_factory);
    grpc_arg args_to_add[] = {channel_factory_arg,
                              grpc_channel_credentials_to_arg(creds)};
    const char* arg_to_remove = channel_factory_arg.key;
    grpc_channel_args* new_args = grpc_channel_args_copy_and_add_and_remove(
        args, &arg_to_remove, 1, args_to_add, GPR_ARRAY_SIZE(args_to_add));
    new_args = creds->update_arguments(new_args);
    // Create channel.
    channel = grpc_core::CreateChannel(target, new_args, &error);
    // Clean up.
    grpc_channel_args_destroy(new_args);
  }
  grpc_channel_args_destroy(args);
  if (channel == nullptr) {
    intptr_t integer;
    grpc_status_code status = GRPC_STATUS_INTERNAL;
    if (grpc_error_get_int(error, GRPC_ERROR_INT_GRPC_STATUS, &integer)) {
      status = static_cast<grpc_status_code>(integer);
    }
    GRPC_ERROR_UNREF(error);
    channel = grpc_lame_client_channel_create(
        target, status, "Failed to create secure client channel");
  }
  return channel;
}
