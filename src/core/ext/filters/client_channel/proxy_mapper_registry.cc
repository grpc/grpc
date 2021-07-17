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

#include <memory>
#include <vector>

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

bool ProxyMapperRegistry::MapName(const char* server_uri,
                                  const grpc_channel_args* args,
                                  char** name_to_resolve,
                                  grpc_channel_args** new_args) {
  Init();
  for (const auto& mapper : *g_proxy_mapper_list) {
    if (mapper->MapName(server_uri, args, name_to_resolve, new_args)) {
      return true;
    }
  }
  return false;
}

bool ProxyMapperRegistry::MapAddress(const grpc_resolved_address& address,
                                     const grpc_channel_args* args,
                                     grpc_resolved_address** new_address,
                                     grpc_channel_args** new_args) {
  Init();
  for (const auto& mapper : *g_proxy_mapper_list) {
    if (mapper->MapAddress(address, args, new_address, new_args)) {
      return true;
    }
  }
  return false;
}

}  // namespace grpc_core
