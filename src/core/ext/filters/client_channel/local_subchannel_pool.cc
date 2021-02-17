//
//
// Copyright 2018 gRPC authors.
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
//

#include <grpc/support/port_platform.h>

#include "src/core/ext/filters/client_channel/local_subchannel_pool.h"

#include "src/core/ext/filters/client_channel/subchannel.h"

namespace grpc_core {

std::unique_ptr<SubchannelRef> LocalSubchannelPool::RegisterSubchannel(
    const SubchannelKey &key, Subchannel* constructed) {
  // Check to see if a subchannel already exists.
  Subchannel* c;
  auto p = subchannel_map_.find(key);
  if (p != subchannel_map_.end()) {
    // The subchannel already exists. Reuse it.
    c = GRPC_SUBCHANNEL_REF(p->second, "subchannel_register+reuse");
    GRPC_SUBCHANNEL_UNREF(constructed, "subchannel_register+found_existing");
  } else {
    // There hasn't been such subchannel. Add one.
    c = constructed;
    subchannel_map_[key] = c;
  }
  return absl::make_unique<LocalSubchannelPoolSubchannelRef>(Ref(), c, key);
}

LocalSubchannelPool::LocalSubchannelPoolSubchannelRef::LocalSubchannelPoolSubchannelRef(
    RefCountedPtr<LocalSubchannelPool> parent, Subchannel* subchannel, const SubchannelKey &key)
    : parent_(std::move(parent)), subchannel_(subchannel), key_(key) {}

LocalSubchannelPool::LocalSubchannelPoolSubchannelRef::~LocalSubchannelPoolSubchannelRef() {
  GRPC_SUBCHANNEL_UNREF(subchannel_, "LocalSubchannelPoolSubchannelRef+destroyed");
  subchannel_ = GRPC_SUBCHANNEL_REF(subchannel_, "LocalSubchannelPoolSubchannelRef+check_for_remaining_refs");
  if (subchannel_ != nullptr) {
    GRPC_SUBCHANNEL_UNREF(subchannel_, "LocalSubchannelPoolSubchannelRef+check_for_remaining_refs");
    return;
  }
  GPR_ASSERT(parent_->subchannel_map_.erase(key_) == 1);
  GRPC_SUBCHANNEL_WEAK_UNREF(subchannel_, "LocalSubchannelPoolSubchannelRef+destroyed");
}

}  // namespace grpc_core
