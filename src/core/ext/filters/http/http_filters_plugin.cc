//
//
// Copyright 2017 gRPC authors.
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

#include <grpc/support/port_platform.h>

#include <string.h>

#include "src/core/ext/filters/http/client/http_client_filter.h"
#include "src/core/ext/filters/http/message_compress/compression_filter.h"
#include "src/core/ext/filters/http/server/http_server_filter.h"
#include "src/core/lib/channel/channel_fwd.h"
#include "src/core/lib/channel/channel_stack_builder.h"
#include "src/core/lib/config/core_configuration.h"
#include "src/core/lib/surface/channel_init.h"
#include "src/core/lib/surface/channel_stack_type.h"
#include "src/core/lib/transport/transport_fwd.h"
#include "src/core/lib/transport/transport_impl.h"

static bool is_building_http_like_transport(
    grpc_core::ChannelStackBuilder* builder) {
  grpc_transport* t = builder->transport();
  return t != nullptr && strstr(t->vtable->name, "http");
}

namespace grpc_core {
void RegisterHttpFilters(CoreConfiguration::Builder* builder) {
  auto compression = [builder](grpc_channel_stack_type channel_type,
                               const grpc_channel_filter* filter) {
    builder->channel_init()->RegisterStage(
        channel_type, GRPC_CHANNEL_INIT_BUILTIN_PRIORITY,
        [filter](ChannelStackBuilder* builder) {
          if (!is_building_http_like_transport(builder)) return true;
          builder->PrependFilter(filter);
          return true;
        });
  };
  auto http = [builder](grpc_channel_stack_type channel_type,
                        const grpc_channel_filter* filter) {
    builder->channel_init()->RegisterStage(
        channel_type, GRPC_CHANNEL_INIT_BUILTIN_PRIORITY,
        [filter](ChannelStackBuilder* builder) {
          if (is_building_http_like_transport(builder)) {
            builder->PrependFilter(filter);
          }
          return true;
        });
  };
  compression(GRPC_CLIENT_SUBCHANNEL, &ClientCompressionFilter::kFilter);
  compression(GRPC_CLIENT_DIRECT_CHANNEL, &ClientCompressionFilter::kFilter);
  compression(GRPC_SERVER_CHANNEL, &ServerCompressionFilter::kFilter);
  http(GRPC_CLIENT_SUBCHANNEL, &HttpClientFilter::kFilter);
  http(GRPC_CLIENT_DIRECT_CHANNEL, &HttpClientFilter::kFilter);
  http(GRPC_SERVER_CHANNEL, &HttpServerFilter::kFilter);
}
}  // namespace grpc_core
