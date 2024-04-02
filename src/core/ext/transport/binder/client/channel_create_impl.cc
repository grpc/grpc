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
#include "src/core/lib/surface/channel_create.h"

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

  grpc_core::Transport* transport = grpc_create_binder_transport_client(
      std::move(endpoint_binder), security_policy);
  GPR_ASSERT(transport != nullptr);

  auto channel_args = grpc_core::CoreConfiguration::Get()
                          .channel_args_preconditioning()
                          .PreconditionChannelArgs(args)
                          .Set(GRPC_ARG_DEFAULT_AUTHORITY, "binder.authority");
  auto channel =
      grpc_core::ChannelCreate("binder_target_placeholder", channel_args,
                               GRPC_CLIENT_DIRECT_CHANNEL, transport);
  // TODO(mingcl): Handle error properly
  GPR_ASSERT(channel.ok());
  grpc_channel_args_destroy(args);
  return channel->release()->c_ptr();
}

grpc_channel* CreateClientBinderChannelImpl(std::string target,
                                            const grpc_channel_args* args) {
  grpc_core::ExecCtx exec_ctx;

  gpr_once_init(&g_factory_once, FactoryInit);

  auto channel_args = grpc_core::CoreConfiguration::Get()
                          .channel_args_preconditioning()
                          .PreconditionChannelArgs(args)
                          .SetObject(g_factory);

  auto channel = grpc_core::ChannelCreate(target, channel_args,
                                          GRPC_CLIENT_CHANNEL, nullptr);

  if (!channel.ok()) {
    return grpc_lame_client_channel_create(
        target.c_str(), static_cast<grpc_status_code>(channel.status().code()),
        "Failed to create binder channel");
  }

  return channel->release()->c_ptr();
}

}  // namespace internal
}  // namespace grpc
#endif
