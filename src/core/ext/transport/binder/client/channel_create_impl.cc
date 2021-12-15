// Copyright 2021 gRPC authors.
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

#include <grpc/support/port_platform.h>

#include "src/core/ext/transport/binder/client/channel_create_impl.h"

#ifndef GRPC_NO_BINDER

#include <memory>
#include <utility>

#include "src/core/ext/transport/binder/client/binder_connector.h"
#include "src/core/ext/transport/binder/transport/binder_transport.h"
#include "src/core/ext/transport/binder/wire_format/binder.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/config/core_configuration.h"
#include "src/core/lib/surface/api_trace.h"
#include "src/core/lib/surface/channel.h"

namespace {

grpc_core::BinderClientChannelFactory* g_factory;
gpr_once g_factory_once = GPR_ONCE_INIT;

void FactoryInit() { g_factory = new grpc_core::BinderClientChannelFactory(); }
}  // namespace

namespace grpc {
namespace internal {

grpc_channel* CreateDirectBinderChannelImplForTesting(
    std::unique_ptr<grpc_binder::Binder> endpoint_binder,
    const grpc_channel_args* args,
    std::shared_ptr<grpc::experimental::binder::SecurityPolicy>
        security_policy) {
  grpc_core::ExecCtx exec_ctx;

  grpc_transport* transport = grpc_create_binder_transport_client(
      std::move(endpoint_binder), security_policy);
  GPR_ASSERT(transport != nullptr);

  grpc_arg default_authority_arg = grpc_channel_arg_string_create(
      const_cast<char*>(GRPC_ARG_DEFAULT_AUTHORITY),
      const_cast<char*>("binder.authority"));
  args = grpc_core::CoreConfiguration::Get()
             .channel_args_preconditioning()
             .PreconditionChannelArgs(args);
  grpc_channel_args* final_args =
      grpc_channel_args_copy_and_add(args, &default_authority_arg, 1);
  grpc_error_handle error = GRPC_ERROR_NONE;
  grpc_channel* channel =
      grpc_channel_create("binder_target_placeholder", final_args,
                          GRPC_CLIENT_DIRECT_CHANNEL, transport, &error);
  // TODO(mingcl): Handle error properly
  GPR_ASSERT(error == GRPC_ERROR_NONE);
  grpc_channel_args_destroy(args);
  grpc_channel_args_destroy(final_args);
  return channel;
}

grpc_channel* CreateClientBinderChannelImpl(const grpc_channel_args* args) {
  grpc_core::ExecCtx exec_ctx;

  gpr_once_init(&g_factory_once, FactoryInit);

  args = grpc_core::CoreConfiguration::Get()
             .channel_args_preconditioning()
             .PreconditionChannelArgs(args);

  // Set channel factory argument
  grpc_arg channel_factory_arg =
      grpc_core::ClientChannelFactory::CreateChannelArg(g_factory);
  const char* arg_to_remove = channel_factory_arg.key;
  grpc_channel_args* new_args = grpc_channel_args_copy_and_add_and_remove(
      args, &arg_to_remove, 1, &channel_factory_arg, 1);

  grpc_error_handle error = GRPC_ERROR_NONE;

  grpc_channel* channel =
      grpc_channel_create("binder_channel_target_placeholder", new_args,
                          GRPC_CLIENT_CHANNEL, nullptr, &error);

  // Clean up.
  grpc_channel_args_destroy(new_args);
  grpc_channel_args_destroy(args);

  if (channel == nullptr) {
    intptr_t integer;
    grpc_status_code status = GRPC_STATUS_INTERNAL;
    if (grpc_error_get_int(error, GRPC_ERROR_INT_GRPC_STATUS, &integer)) {
      status = static_cast<grpc_status_code>(integer);
    }
    GRPC_ERROR_UNREF(error);
    channel = grpc_lame_client_channel_create(
        "binder_channel_target_placeholder", status,
        "Failed to create binder channel");
  }

  return channel;
}

}  // namespace internal
}  // namespace grpc
#endif
