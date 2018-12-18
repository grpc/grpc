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

int ServerAddress::Cmp(const ServerAddress& other) const {
  if (address_.len > other.address_.len) return 1;
  if (address_.len < other.address_.len) return -1;
  int retval = memcmp(address_.addr, other.address_.addr, address_.len);
  if (retval != 0) return retval;
  return grpc_channel_args_compare(args_, other.args_);
}

bool ServerAddress::IsBalancer() const {
  return grpc_channel_arg_get_bool(
      grpc_channel_args_find(args_, GRPC_ARG_ADDRESS_IS_BALANCER), false);
}

//
// ServerAddressList
//

namespace {

void* ServerAddressListCopy(void* addresses) {
  ServerAddressList* a = static_cast<ServerAddressList*>(addresses);
  return New<ServerAddressList>(*a);
}

void ServerAddressListDestroy(void* addresses) {
  ServerAddressList* a = static_cast<ServerAddressList*>(addresses);
  Delete(a);
}

int ServerAddressListCompare(void* addresses1, void* addresses2) {
  ServerAddressList* a1 = static_cast<ServerAddressList*>(addresses1);
  ServerAddressList* a2 = static_cast<ServerAddressList*>(addresses2);
  if (a1->size() > a2->size()) return 1;
  if (a1->size() < a2->size()) return -1;
  for (size_t i = 0; i < a1->size(); ++i) {
    int retval = (*a1)[i].Cmp((*a2)[i]);
    if (retval != 0) return retval;
  }
  return 0;
}

const grpc_arg_pointer_vtable server_addresses_arg_vtable = {
    ServerAddressListCopy, ServerAddressListDestroy, ServerAddressListCompare};

}  // namespace

grpc_arg CreateServerAddressListChannelArg(const ServerAddressList* addresses) {
  return grpc_channel_arg_pointer_create(
      const_cast<char*>(GRPC_ARG_SERVER_ADDRESS_LIST),
      const_cast<ServerAddressList*>(addresses), &server_addresses_arg_vtable);
}

ServerAddressList* FindServerAddressListChannelArg(
    const grpc_channel_args* channel_args) {
  const grpc_arg* lb_addresses_arg =
      grpc_channel_args_find(channel_args, GRPC_ARG_SERVER_ADDRESS_LIST);
  if (lb_addresses_arg == nullptr || lb_addresses_arg->type != GRPC_ARG_POINTER)
    return nullptr;
  return static_cast<ServerAddressList*>(lb_addresses_arg->value.pointer.p);
}

}  // namespace grpc_core
