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

#include <limits.h>
#include <stdbool.h>
#include <string.h>

#include <grpc/support/alloc.h>

#include "src/core/ext/filters/client_channel/client_channel.h"
#include "src/core/ext/filters/client_channel/client_channel_channelz.h"
#include "src/core/ext/filters/client_channel/global_subchannel_pool.h"
#include "src/core/ext/filters/client_channel/http_connect_handshaker.h"
#include "src/core/ext/filters/client_channel/http_proxy.h"
#include "src/core/ext/filters/client_channel/lb_policy_registry.h"
#include "src/core/ext/filters/client_channel/proxy_mapper_registry.h"
#include "src/core/ext/filters/client_channel/resolver_registry.h"
#include "src/core/ext/filters/client_channel/resolver_result_parsing.h"
#include "src/core/ext/filters/client_channel/retry_throttle.h"
#include "src/core/lib/surface/channel_init.h"

static bool append_filter(grpc_channel_stack_builder* builder, void* arg) {
  const grpc_channel_args* args =
      grpc_channel_stack_builder_get_channel_arguments(builder);
  grpc_arg args_to_add[] = {
      grpc_core::channelz::ClientChannelNode::CreateChannelArg()};
  grpc_channel_args* new_args = grpc_channel_args_copy_and_add(
      args, args_to_add, GPR_ARRAY_SIZE(args_to_add));
  grpc_channel_stack_builder_set_channel_arguments(builder, new_args);
  grpc_channel_args_destroy(new_args);
  return grpc_channel_stack_builder_append_filter(
      builder, static_cast<const grpc_channel_filter*>(arg), nullptr, nullptr);
}

void grpc_client_channel_init(void) {
  grpc_core::ServiceConfig::Init();
  grpc_core::internal::ClientChannelServiceConfigParser::Register();
  grpc_core::LoadBalancingPolicyRegistry::Builder::InitRegistry();
  grpc_core::ResolverRegistry::Builder::InitRegistry();
  grpc_core::internal::ServerRetryThrottleMap::Init();
  grpc_proxy_mapper_registry_init();
  grpc_register_http_proxy_mapper();
  grpc_core::GlobalSubchannelPool::Init();
  grpc_channel_init_register_stage(
      GRPC_CLIENT_CHANNEL, GRPC_CHANNEL_INIT_BUILTIN_PRIORITY, append_filter,
      (void*)&grpc_client_channel_filter);
  grpc_http_connect_register_handshaker_factory();
}

void grpc_client_channel_shutdown(void) {
  grpc_core::GlobalSubchannelPool::Shutdown();
  grpc_channel_init_shutdown();
  grpc_proxy_mapper_registry_shutdown();
  grpc_core::internal::ServerRetryThrottleMap::Shutdown();
  grpc_core::ResolverRegistry::Builder::ShutdownRegistry();
  grpc_core::LoadBalancingPolicyRegistry::Builder::ShutdownRegistry();
  grpc_core::ServiceConfig::Shutdown();
}
