/*
 *
 * Copyright 2018 gRPC authors.
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

#include "src/core/ext/filters/client_channel/server_address.h"

namespace grpc_core {

//
// ServerAddress
//

ServerAddress::ServerAddress(
    const grpc_resolved_address& address, grpc_channel_args* args,
    std::map<const char*, std::unique_ptr<AttributeInterface>> attributes)
    : address_(address), args_(args), attributes_(std::move(attributes)) {}

ServerAddress::ServerAddress(
    const void* address, size_t address_len, grpc_channel_args* args,
    std::map<const char*, std::unique_ptr<AttributeInterface>> attributes)
    : args_(args), attributes_(std::move(attributes)) {
  memcpy(address_.addr, address, address_len);
  address_.len = static_cast<socklen_t>(address_len);
}

namespace {

int CompareAttributes(
    const std::map<const char*,
                   std::unique_ptr<ServerAddress::AttributeInterface>>&
        attributes1,
    const std::map<const char*,
                   std::unique_ptr<ServerAddress::AttributeInterface>>&
        attributes2) {
  auto it2 = attributes2.begin();
  for (auto it1 = attributes1.begin(); it1 != attributes1.end(); ++it1) {
    // attributes2 has fewer elements than attributes1
    if (it2 == attributes2.end()) return -1;
    // compare keys
    int retval = strcmp(it1->first, it2->first);
    if (retval != 0) return retval;
    // compare values
    retval = it1->second->Cmp(it2->second.get());
    if (retval != 0) return retval;
    ++it2;
  }
  // attributes1 has fewer elements than attributes2
  if (it2 != attributes2.end()) return 1;
  // equal
  return 0;
}

}  // namespace

int ServerAddress::Cmp(const ServerAddress& other) const {
  if (address_.len > other.address_.len) return 1;
  if (address_.len < other.address_.len) return -1;
  int retval = memcmp(address_.addr, other.address_.addr, address_.len);
  if (retval != 0) return retval;
  retval = grpc_channel_args_compare(args_, other.args_);
  if (retval != 0) return retval;
  return CompareAttributes(attributes_, other.attributes_);
}

}  // namespace grpc_core
