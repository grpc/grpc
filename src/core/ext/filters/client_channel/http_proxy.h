/*
 *
 * Copyright 2016 gRPC authors.
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

#ifndef GRPC_CORE_EXT_FILTERS_CLIENT_CHANNEL_HTTP_PROXY_H
#define GRPC_CORE_EXT_FILTERS_CLIENT_CHANNEL_HTTP_PROXY_H

#include <grpc/support/port_platform.h>

#include <grpc/impl/codegen/grpc_types.h>

#include "src/core/ext/filters/client_channel/proxy_mapper.h"
#include "src/core/lib/iomgr/resolved_address.h"

namespace grpc_core {

class HttpProxyMapper : public ProxyMapperInterface {
 public:
  bool MapName(absl::string_view server_uri, ChannelArgs* args,
               absl::optional<std::string>* name_to_resolve) override;

  bool MapAddress(const grpc_resolved_address& /*address*/,
                  ChannelArgs* /*args*/,
                  grpc_resolved_address** /*new_address*/) override {
    return false;
  }
};

void RegisterHttpProxyMapper();

}  // namespace grpc_core

#endif /* GRPC_CORE_EXT_FILTERS_CLIENT_CHANNEL_HTTP_PROXY_H */
