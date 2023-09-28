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

#ifndef GRPC_SRC_CORE_EXT_FILTERS_CLIENT_CHANNEL_LB_POLICY_GRPCLB_GRPCLB_BALANCER_ADDRESSES_H
#define GRPC_SRC_CORE_EXT_FILTERS_CLIENT_CHANNEL_LB_POLICY_GRPCLB_GRPCLB_BALANCER_ADDRESSES_H

#include <grpc/support/port_platform.h>

#include <grpc/grpc.h>

#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/resolver/server_address.h"

namespace grpc_core {

GRPC_MUST_USE_RESULT
ChannelArgs SetGrpcLbBalancerAddresses(const ChannelArgs& args,
                                       ServerAddressList address_list);
const ServerAddressList* FindGrpclbBalancerAddressesInChannelArgs(
    const ChannelArgs& args);

}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_EXT_FILTERS_CLIENT_CHANNEL_LB_POLICY_GRPCLB_GRPCLB_BALANCER_ADDRESSES_H
