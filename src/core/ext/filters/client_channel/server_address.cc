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

#include <string.h>

namespace grpc_core {

//
// ServerAddress
//

ServerAddress::ServerAddress(const grpc_resolved_address& address,
                             grpc_channel_args* args)
    : address_(address), args_(args) {}

ServerAddress::ServerAddress(const void* address, size_t address_len,
                             grpc_channel_args* args)
    : args_(args) {
  memcpy(address_.addr, address, address_len);
  address_.len = static_cast<socklen_t>(address_len);
}

bool ServerAddress::operator==(const grpc_core::ServerAddress& other) const {
  return address_.len == other.address_.len &&
         memcmp(address_.addr, other.address_.addr, address_.len) == 0 &&
         grpc_channel_args_compare(args_, other.args_) == 0;
}

bool ServerAddress::IsBalancer() const {
  return grpc_channel_arg_get_bool(
      grpc_channel_args_find(args_, GRPC_ARG_ADDRESS_IS_BALANCER), false);
}

}  // namespace grpc_core
