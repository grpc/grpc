//
// Copyright 2024 gRPC authors.
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

#include "src/core/handshaker/http_connect/xds_http_proxy_mapper.h"

#include <memory>
#include <string>

#include "absl/log/log.h"
#include "absl/types/optional.h"
#include "src/core/handshaker/http_connect/http_connect_handshaker.h"
#include "src/core/lib/address_utils/parse_address.h"
#include "src/core/lib/address_utils/sockaddr_utils.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/xds/grpc/xds_endpoint.h"

namespace grpc_core {

absl::optional<grpc_resolved_address> XdsHttpProxyMapper::MapAddress(
    const grpc_resolved_address& endpoint_address, ChannelArgs* args) {
  auto proxy_address_str = args->GetString(GRPC_ARG_XDS_HTTP_PROXY);
  if (!proxy_address_str.has_value()) return absl::nullopt;
  auto proxy_address = StringToSockaddr(*proxy_address_str);
  if (!proxy_address.ok()) {
    LOG(ERROR) << "error parsing address \"" << *proxy_address_str
               << "\": " << proxy_address.status();
    return absl::nullopt;
  }
  auto endpoint_address_str = grpc_sockaddr_to_string(&endpoint_address, true);
  if (!endpoint_address_str.ok()) {
    LOG(ERROR) << "error converting address to string: "
               << endpoint_address_str.status();
    return absl::nullopt;
  }
  *args = args->Set(GRPC_ARG_HTTP_CONNECT_SERVER, *endpoint_address_str);
  return *proxy_address;
}

void RegisterXdsHttpProxyMapper(CoreConfiguration::Builder* builder) {
  builder->proxy_mapper_registry()->Register(
      /*at_start=*/true, std::make_unique<XdsHttpProxyMapper>());
}

}  // namespace grpc_core
