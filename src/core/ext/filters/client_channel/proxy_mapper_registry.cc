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

#include <grpc/support/port_platform.h>

#include "src/core/ext/filters/client_channel/proxy_mapper_registry.h"

#include <algorithm>
#include <memory>
#include <utility>
#include <vector>

#include "absl/types/optional.h"

namespace grpc_core {

namespace {

using ProxyMapperList = std::vector<std::unique_ptr<ProxyMapperInterface>>;
ProxyMapperList* g_proxy_mapper_list;

}  // namespace

void ProxyMapperRegistry::Init() {
  if (g_proxy_mapper_list == nullptr) {
    g_proxy_mapper_list = new ProxyMapperList();
  }
}

void ProxyMapperRegistry::Shutdown() {
  delete g_proxy_mapper_list;
  // Clean up in case we re-initialze later.
  // TODO(roth): This should ideally live in Init().  However, if we did this
  // there, then we would do it AFTER we start registering proxy mappers from
  // third-party plugins, so they'd never show up (and would leak memory).
  // We probably need some sort of dependency system for plugins to fix
  // this.
  g_proxy_mapper_list = nullptr;
}

void ProxyMapperRegistry::Register(
    bool at_start, std::unique_ptr<ProxyMapperInterface> mapper) {
  Init();
  if (at_start) {
    g_proxy_mapper_list->insert(g_proxy_mapper_list->begin(),
                                std::move(mapper));
  } else {
    g_proxy_mapper_list->emplace_back(std::move(mapper));
  }
}

absl::optional<std::string> ProxyMapperRegistry::MapName(
    absl::string_view server_uri, ChannelArgs* args) {
  Init();
  ChannelArgs args_backup = *args;
  for (const auto& mapper : *g_proxy_mapper_list) {
    *args = args_backup;
    auto r = mapper->MapName(server_uri, args);
    if (r.has_value()) return r;
  }
  *args = args_backup;
  return absl::nullopt;
}

absl::optional<grpc_resolved_address> ProxyMapperRegistry::MapAddress(
    const grpc_resolved_address& address, ChannelArgs* args) {
  Init();
  ChannelArgs args_backup = *args;
  for (const auto& mapper : *g_proxy_mapper_list) {
    *args = args_backup;
    auto r = mapper->MapAddress(address, args);
    if (r.has_value()) return r;
  }
  *args = args_backup;
  return absl::nullopt;
}

}  // namespace grpc_core
