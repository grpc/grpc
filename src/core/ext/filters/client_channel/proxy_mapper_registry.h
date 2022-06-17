/*
 *
 * Copyright 2017 gRPC authors.
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

#ifndef GRPC_CORE_EXT_FILTERS_CLIENT_CHANNEL_PROXY_MAPPER_REGISTRY_H
#define GRPC_CORE_EXT_FILTERS_CLIENT_CHANNEL_PROXY_MAPPER_REGISTRY_H

#include <grpc/support/port_platform.h>

#include <memory>

#include <grpc/impl/codegen/grpc_types.h>

#include "src/core/ext/filters/client_channel/proxy_mapper.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/iomgr/resolved_address.h"

namespace grpc_core {

class ProxyMapperRegistry {
 public:
  static void Init();
  static void Shutdown();

  /// Registers a new proxy mapper.
  /// If \a at_start is true, the new mapper will be at the beginning of
  /// the list.  Otherwise, it will be added to the end.
  static void Register(bool at_start,
                       std::unique_ptr<ProxyMapperInterface> mapper);

  static bool MapName(absl::string_view server_uri, ChannelArgs* args,
                      absl::optional<std::string>* name_to_resolve);

  static bool MapAddress(const grpc_resolved_address& address,
                         ChannelArgs* args,
                         grpc_resolved_address** new_address);
};

}  // namespace grpc_core

#endif /* GRPC_CORE_EXT_FILTERS_CLIENT_CHANNEL_PROXY_MAPPER_REGISTRY_H */
