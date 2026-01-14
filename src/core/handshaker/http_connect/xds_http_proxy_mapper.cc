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
#include <optional>
#include <string>

#include "src/core/handshaker/http_connect/http_connect_client_handshaker.h"
#include "src/core/lib/address_utils/parse_address.h"
#include "src/core/lib/address_utils/sockaddr_utils.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/util/uri.h"
#include "src/core/xds/grpc/xds_endpoint.h"
#include "absl/log/log.h"
#include "absl/strings/strip.h"

namespace grpc_core {

std::optional<std::string> XdsHttpProxyMapper::MapAddress(
    const std::string& endpoint_address, ChannelArgs* args) {
  auto proxy_address_str = args->GetString(GRPC_ARG_XDS_HTTP_PROXY);
  if (!proxy_address_str.has_value()) return std::nullopt;
  auto uri = URI::Parse(endpoint_address);
  if (!uri.ok()) {
    LOG(ERROR) << "error parsing address " << endpoint_address << ": "
               << uri.status();
    return std::nullopt;
  }
  *args = args->Set(GRPC_ARG_HTTP_CONNECT_SERVER,
                    absl::StripPrefix(uri->path(), "/"));
  auto proxy_address = StringToSockaddr(*proxy_address_str);
  if (!proxy_address.ok()) {
    LOG(ERROR) << "error parsing address \"" << *proxy_address_str
               << "\": " << proxy_address.status();
    return std::nullopt;
  }
  auto proxy_uri = grpc_sockaddr_to_uri(&*proxy_address);
  if (!proxy_uri.ok()) {
    LOG(ERROR) << "error converting address to uri: " << proxy_uri.status();
    return std::nullopt;
  }
  return *proxy_uri;
}

void RegisterXdsHttpProxyMapper(CoreConfiguration::Builder* builder) {
  builder->proxy_mapper_registry()->Register(
      /*at_start=*/true, std::make_unique<XdsHttpProxyMapper>());
}

}  // namespace grpc_core
