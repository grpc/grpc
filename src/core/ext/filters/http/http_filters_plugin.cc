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

#include "absl/strings/match.h"
#include "src/core/config/core_configuration.h"
#include "src/core/ext/filters/http/client/http_client_filter.h"
#include "src/core/ext/filters/http/message_compress/compression_filter.h"
#include "src/core/ext/filters/http/server/http_server_filter.h"
#include "src/core/ext/filters/message_size/message_size_filter.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/experiments/experiments.h"
#include "src/core/lib/surface/channel_stack_type.h"
#include "src/core/lib/transport/transport.h"

namespace grpc_core {
namespace {
bool IsBuildingHttpLikeTransport(const ChannelArgs& args) {
  auto* t = args.GetObject<Transport>();
  return t != nullptr && absl::StrContains(t->GetTransportName(), "http");
}
}  // namespace

void RegisterHttpFilters(CoreConfiguration::Builder* builder) {
  builder->channel_init()
      ->RegisterFilter<ClientCompressionFilter>(GRPC_CLIENT_SUBCHANNEL)
      .If(IsBuildingHttpLikeTransport)
      .After<HttpClientFilter>()
      .After<ClientMessageSizeFilter>();
  builder->channel_init()
      ->RegisterFilter<ClientCompressionFilter>(GRPC_CLIENT_DIRECT_CHANNEL)
      .If(IsBuildingHttpLikeTransport)
      .After<HttpClientFilter>()
      .After<ClientMessageSizeFilter>();
  builder->channel_init()
      ->RegisterFilter<ServerCompressionFilter>(GRPC_SERVER_CHANNEL)
      .If(IsBuildingHttpLikeTransport)
      .After<HttpServerFilter>()
      .After<ServerMessageSizeFilter>();
  builder->channel_init()
      ->RegisterFilter<HttpClientFilter>(GRPC_CLIENT_SUBCHANNEL)
      .If(IsBuildingHttpLikeTransport)
      .After<ClientMessageSizeFilter>();
  builder->channel_init()
      ->RegisterFilter<HttpClientFilter>(GRPC_CLIENT_DIRECT_CHANNEL)
      .If(IsBuildingHttpLikeTransport)
      .After<ClientMessageSizeFilter>();
  builder->channel_init()
      ->RegisterFilter<HttpServerFilter>(GRPC_SERVER_CHANNEL)
      .If(IsBuildingHttpLikeTransport)
      .After<ServerMessageSizeFilter>();
}
}  // namespace grpc_core
