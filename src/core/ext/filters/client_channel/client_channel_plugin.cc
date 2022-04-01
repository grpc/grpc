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

#include "src/core/ext/filters/client_channel/backup_poller.h"
#include "src/core/ext/filters/client_channel/client_channel.h"
#include "src/core/ext/filters/client_channel/client_channel_channelz.h"
#include "src/core/ext/filters/client_channel/global_subchannel_pool.h"
#include "src/core/ext/filters/client_channel/http_connect_handshaker.h"
#include "src/core/ext/filters/client_channel/http_proxy.h"
#include "src/core/ext/filters/client_channel/lb_policy_registry.h"
#include "src/core/ext/filters/client_channel/proxy_mapper_registry.h"
#include "src/core/ext/filters/client_channel/resolver_result_parsing.h"
#include "src/core/ext/filters/client_channel/retry_service_config.h"
#include "src/core/ext/filters/client_channel/retry_throttle.h"
#include "src/core/ext/filters/client_channel/tcp_connect_handshaker.h"
#include "src/core/lib/config/core_configuration.h"
#include "src/core/lib/resolver/resolver_registry.h"

void grpc_client_channel_init(void) {
  grpc_core::LoadBalancingPolicyRegistry::Builder::InitRegistry();
  grpc_core::ProxyMapperRegistry::Init();
  grpc_core::RegisterHttpProxyMapper();
  grpc_client_channel_global_init_backup_polling();
}

void grpc_client_channel_shutdown(void) {
  grpc_core::ProxyMapperRegistry::Shutdown();
  grpc_core::LoadBalancingPolicyRegistry::Builder::ShutdownRegistry();
}

namespace grpc_core {

void BuildClientChannelConfiguration(CoreConfiguration::Builder* builder) {
  // The order of the registration is crucial here.
  // We want TCP connect handshaker to be registered last so that it is added to
  // the start of the handshaker list.
  RegisterHttpConnectHandshaker(builder);
  RegisterTCPConnectHandshaker(builder);
  internal::ClientChannelServiceConfigParser::Register(builder);
  internal::RetryServiceConfigParser::Register(builder);
  builder->channel_init()->RegisterStage(
      GRPC_CLIENT_CHANNEL, GRPC_CHANNEL_INIT_BUILTIN_PRIORITY,
      [](ChannelStackBuilder* builder) {
        builder->AppendFilter(&ClientChannel::kFilterVtable, nullptr);
        return true;
      });
}

}  // namespace grpc_core
