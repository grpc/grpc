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

#include <grpc/support/port_platform.h>

#include "src/core/ext/filters/client_channel/lb_policy/address_filtering.h"

#include <stddef.h>

#include <algorithm>
#include <utility>

#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/gprpp/ref_counted_ptr.h"

namespace grpc_core {

absl::string_view HierarchicalPathArg::ChannelArgName() {
  return GRPC_ARG_NO_SUBCHANNEL_PREFIX "address.hierarchical_path";
}

int HierarchicalPathArg::ChannelArgsCompare(const HierarchicalPathArg* a,
                                            const HierarchicalPathArg* b) {
  for (size_t i = 0; i < a->path_.size(); ++i) {
    if (b->path_.size() == i) return 1;
    int r = a->path_[i].compare(b->path_[i]);
    if (r != 0) return r;
  }
  if (b->path_.size() > a->path_.size()) return -1;
  return 0;
}

absl::StatusOr<HierarchicalAddressMap> MakeHierarchicalAddressMap(
    const absl::StatusOr<EndpointAddressesList>& addresses) {
  if (!addresses.ok()) return addresses.status();
  HierarchicalAddressMap result;
  for (const EndpointAddresses& endpoint_addresses : *addresses) {
    const auto* path_arg =
        endpoint_addresses.args().GetObject<HierarchicalPathArg>();
    if (path_arg == nullptr) continue;
    const std::vector<std::string>& path = path_arg->path();
    auto it = path.begin();
    if (it == path.end()) continue;
    EndpointAddressesList& target_list = result[*it];
    ChannelArgs args = endpoint_addresses.args();
    ++it;
    if (it != path.end()) {
      std::vector<std::string> remaining_path(it, path.end());
      args = args.SetObject(
          MakeRefCounted<HierarchicalPathArg>(std::move(remaining_path)));
    }
    target_list.emplace_back(endpoint_addresses.addresses(), args);
  }
  return result;
}

}  // namespace grpc_core
