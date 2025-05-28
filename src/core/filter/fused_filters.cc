//
//
// Copyright 2025 gRPC authors.
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
//

#include <grpc/grpc.h>
#include <grpc/support/port_platform.h>

#include "src/core/call/filter_fusion.h"
#include "src/core/config/core_configuration.h"
#include "src/core/ext/filters/http/client/http_client_filter.h"
#include "src/core/ext/filters/http/client_authority_filter.h"
#include "src/core/ext/filters/http/message_compress/compression_filter.h"
#include "src/core/ext/filters/http/server/http_server_filter.h"
#include "src/core/ext/filters/message_size/message_size_filter.h"
#include "src/core/filter/auth/auth_filters.h"
#include "src/core/lib/experiments/experiments.h"
#include "src/core/lib/security/authorization/grpc_server_authz_filter.h"
#include "src/core/load_balancing/grpclb/client_load_reporting_filter.h"
#include "src/core/server/server_call_tracer_filter.h"
#include "src/core/service_config/service_config_channel_arg_filter.h"

namespace grpc_core {

using FusedClientSubchannelFilter =
    FusedFilter<FilterEndpoint::kClient, ClientLoadReportingFilter,
                ClientMessageSizeFilter, HttpClientFilter,
                ClientCompressionFilter>;

using FusedClientSubchannelV3ClientAuthFilter =
    FusedFilter<FilterEndpoint::kClient, ClientAuthorityFilter,
                ClientAuthFilter, ClientLoadReportingFilter,
                ClientMessageSizeFilter, HttpClientFilter,
                ClientCompressionFilter>;

using FusedClientDirectChannelFilter =
    FusedFilter<FilterEndpoint::kClient, ServiceConfigChannelArgFilter,
                ClientMessageSizeFilter, HttpClientFilter,
                ClientCompressionFilter>;

using FusedClientDirectChannelV3ClientAuthFilter =
    FusedFilter<FilterEndpoint::kClient, ClientAuthorityFilter,
                ClientAuthFilter, ServiceConfigChannelArgFilter,
                ClientMessageSizeFilter, HttpClientFilter,
                ClientCompressionFilter>;

using FusedServerChannelFilter =
    FusedFilter<FilterEndpoint::kServer, HttpServerFilter,
                ServerCompressionFilter, ServerAuthFilter>;

void RegisterFusedFilters(CoreConfiguration::Builder* builder) {
  if (!IsFuseFiltersEnabled()) {
    return;
  }
  if (IsCallv3ClientAuthFilterEnabled()) {
    builder->channel_init()->RegisterFusedFilter(
        GRPC_CLIENT_SUBCHANNEL,
        &FusedClientSubchannelV3ClientAuthFilter::kFilter);
    builder->channel_init()->RegisterFusedFilter(
        GRPC_CLIENT_DIRECT_CHANNEL,
        &FusedClientDirectChannelV3ClientAuthFilter::kFilter);
  } else {
    builder->channel_init()->RegisterFusedFilter(
        GRPC_CLIENT_SUBCHANNEL, &FusedClientSubchannelFilter::kFilter);
    builder->channel_init()->RegisterFusedFilter(
        GRPC_CLIENT_DIRECT_CHANNEL, &FusedClientDirectChannelFilter::kFilter);
  }
  builder->channel_init()->RegisterFusedFilter(
      GRPC_SERVER_CHANNEL, &FusedServerChannelFilter::kFilter);
}

}  // namespace grpc_core