//
// Copyright 2019 gRPC authors.
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

#include <grpc/support/port_platform.h>

#include "src/core/ext/filters/client_channel/lb_policy/grpclb/grpclb_balancer_addresses.h"

#include <stddef.h>

#include <memory>
#include <utility>

#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/gpr/useful.h"

// Channel arg key for the list of balancer addresses.
#define GRPC_ARG_GRPCLB_BALANCER_ADDRESSES "grpc.grpclb_balancer_addresses"

namespace grpc_core {

namespace {

void* BalancerAddressesArgCopy(void* p) {
  ServerAddressList* address_list = static_cast<ServerAddressList*>(p);
  return new ServerAddressList(*address_list);
}

void BalancerAddressesArgDestroy(void* p) {
  ServerAddressList* address_list = static_cast<ServerAddressList*>(p);
  delete address_list;
}

int BalancerAddressesArgCmp(void* p, void* q) {
  ServerAddressList* address_list1 = static_cast<ServerAddressList*>(p);
  ServerAddressList* address_list2 = static_cast<ServerAddressList*>(q);
  if (address_list1 == nullptr || address_list2 == nullptr) {
    return QsortCompare(address_list1, address_list2);
  }
  if (address_list1->size() > address_list2->size()) return 1;
  if (address_list1->size() < address_list2->size()) return -1;
  for (size_t i = 0; i < address_list1->size(); ++i) {
    int retval = (*address_list1)[i].Cmp((*address_list2)[i]);
    if (retval != 0) return retval;
  }
  return 0;
}

const grpc_arg_pointer_vtable kBalancerAddressesArgVtable = {
    BalancerAddressesArgCopy, BalancerAddressesArgDestroy,
    BalancerAddressesArgCmp};

}  // namespace

grpc_arg CreateGrpclbBalancerAddressesArg(
    const ServerAddressList* address_list) {
  return grpc_channel_arg_pointer_create(
      const_cast<char*>(GRPC_ARG_GRPCLB_BALANCER_ADDRESSES),
      const_cast<ServerAddressList*>(address_list),
      &kBalancerAddressesArgVtable);
}

const ServerAddressList* FindGrpclbBalancerAddressesInChannelArgs(
    const ChannelArgs& args) {
  return args.GetPointer<const ServerAddressList>(
      GRPC_ARG_GRPCLB_BALANCER_ADDRESSES);
}

ChannelArgs SetGrpcLbBalancerAddresses(const ChannelArgs& args,
                                       ServerAddressList address_list) {
  return args.Set(
      GRPC_ARG_GRPCLB_BALANCER_ADDRESSES,
      ChannelArgs::Pointer(new ServerAddressList(std::move(address_list)),
                           &kBalancerAddressesArgVtable));
}

}  // namespace grpc_core
