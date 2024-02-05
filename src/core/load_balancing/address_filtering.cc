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

#include "src/core/load_balancing/address_filtering.h"

#include <stddef.h>

#include <utility>

#include "absl/functional/function_ref.h"

#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/gprpp/ref_counted_ptr.h"
#include "src/core/lib/iomgr/resolved_address.h"

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

namespace {

class HierarchicalAddressIterator : public EndpointAddressesIterator {
 public:
  HierarchicalAddressIterator(
      std::shared_ptr<EndpointAddressesIterator> parent_it,
      RefCountedStringValue child_name)
      : parent_it_(std::move(parent_it)), child_name_(std::move(child_name)) {}

  void ForEach(absl::FunctionRef<void(const EndpointAddresses&)> callback)
      const override {
    RefCountedPtr<HierarchicalPathArg> remaining_path_attr;
    parent_it_->ForEach([&](const EndpointAddresses& endpoint) {
      const auto* path_arg = endpoint.args().GetObject<HierarchicalPathArg>();
      if (path_arg == nullptr) return;
      const std::vector<RefCountedStringValue>& path = path_arg->path();
      auto it = path.begin();
      if (it == path.end()) return;
      if (*it != child_name_) return;
      ChannelArgs args = endpoint.args();
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
      callback(EndpointAddresses(endpoint.addresses(), args));
    });
  }

 private:
  std::shared_ptr<EndpointAddressesIterator> parent_it_;
  RefCountedStringValue child_name_;
};

}  // namespace

absl::StatusOr<HierarchicalAddressMap> MakeHierarchicalAddressMap(
    absl::StatusOr<std::shared_ptr<EndpointAddressesIterator>> addresses) {
  if (!addresses.ok()) return addresses.status();
  HierarchicalAddressMap result;
  (*addresses)->ForEach([&](const EndpointAddresses& endpoint) {
    const auto* path_arg = endpoint.args().GetObject<HierarchicalPathArg>();
    if (path_arg == nullptr) return;
    const std::vector<RefCountedStringValue>& path = path_arg->path();
    auto it = path.begin();
    if (it == path.end()) return;
    auto& target_list = result[*it];
    if (target_list == nullptr) {
      target_list =
          std::make_shared<HierarchicalAddressIterator>(*addresses, *it);
    }
  });
  return result;
}

}  // namespace grpc_core
