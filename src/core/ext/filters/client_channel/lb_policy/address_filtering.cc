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
    int r = a->path_[i].as_string_view().compare(b->path_[i].as_string_view());
    if (r != 0) return r;
  }
  if (b->path_.size() > a->path_.size()) return -1;
  return 0;
}

absl::StatusOr<HierarchicalAddressMap> MakeHierarchicalAddressMap(
    const absl::StatusOr<ServerAddressList>& addresses) {
  if (!addresses.ok()) return addresses.status();
  HierarchicalAddressMap result;
  RefCountedPtr<HierarchicalPathArg> remaining_path_attr;
  for (const ServerAddress& address : *addresses) {
    const auto* path_arg = address.args().GetObject<HierarchicalPathArg>();
    if (path_arg == nullptr) continue;
    const std::vector<RefCountedStringValue>& path = path_arg->path();
    auto it = path.begin();
    if (it == path.end()) continue;
    ServerAddressList& target_list = result[*it];
    ChannelArgs args = address.args();
    ++it;
    if (it != path.end()) {
      std::vector<RefCountedStringValue> remaining_path(it, path.end());
      if (remaining_path_attr == nullptr ||
          remaining_path_attr->path() != remaining_path) {
        remaining_path_attr =
            MakeRefCounted<HierarchicalPathArg>(std::move(remaining_path));
      }
      args = args.SetObject(remaining_path_attr);
    }
    target_list.emplace_back(address.address(), args);
  }
  return result;
}

}  // namespace grpc_core
