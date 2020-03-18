//
// Copyright 2020 gRPC authors.
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

#ifndef GRPC_CORE_EXT_FILTERS_CLIENT_CHANNEL_LB_POLICY_ADDRESS_FILTERING_H
#define GRPC_CORE_EXT_FILTERS_CLIENT_CHANNEL_LB_POLICY_ADDRESS_FILTERING_H

#include <grpc/support/port_platform.h>

#include <map>
#include <vector>
#include <string>

#include "absl/strings/string_view.h"

#include "src/core/ext/filters/client_channel/server_address.h"

namespace grpc_core {

grpc_arg MakeHierarchicalPathArg(const std::vector<std::string>& path);

using HierarchicalAddressMap = std::map<std::string, ServerAddressList>;

HierarchicalAddressMap MakeHierarchicalAddressMap(
    const ServerAddressList& addresses);

}  // namespace grpc_core

#endif /* GRPC_CORE_EXT_FILTERS_CLIENT_CHANNEL_LB_POLICY_ADDRESS_FILTERING_H \
        */
