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

#include "src/core/handshaker/proxy_mapper_registry.h"

#include <grpc/support/port_platform.h>

#include <algorithm>
#include <memory>
#include <utility>
#include <vector>

#include "absl/types/optional.h"

namespace grpc_core {

void ProxyMapperRegistry::Builder::Register(
    bool at_start, std::unique_ptr<ProxyMapperInterface> mapper) {
  if (at_start) {
    mappers_.insert(mappers_.begin(), std::move(mapper));
  } else {
    mappers_.emplace_back(std::move(mapper));
  }
}

ProxyMapperRegistry ProxyMapperRegistry::Builder::Build() {
  ProxyMapperRegistry registry;
  registry.mappers_ = std::move(mappers_);
  return registry;
}

absl::optional<std::string> ProxyMapperRegistry::MapName(
    absl::string_view server_uri, ChannelArgs* args) const {
  ChannelArgs args_backup = *args;
  for (const auto& mapper : mappers_) {
    *args = args_backup;
    auto r = mapper->MapName(server_uri, args);
    if (r.has_value()) return r;
  }
  *args = args_backup;
  return absl::nullopt;
}

absl::optional<grpc_resolved_address> ProxyMapperRegistry::MapAddress(
    const grpc_resolved_address& address, ChannelArgs* args) const {
  ChannelArgs args_backup = *args;
  for (const auto& mapper : mappers_) {
    *args = args_backup;
    auto r = mapper->MapAddress(address, args);
    if (r.has_value()) return r;
  }
  *args = args_backup;
  return absl::nullopt;
}

}  // namespace grpc_core
