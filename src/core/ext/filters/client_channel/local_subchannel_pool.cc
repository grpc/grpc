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

LocalSubchannelPool::LocalSubchannelPool() {
  subchannel_map_ = grpc_avl_create(&subchannel_avl_vtable_);
}

LocalSubchannelPool::~LocalSubchannelPool() {
  grpc_avl_unref(subchannel_map_, nullptr);
}

grpc_subchannel* LocalSubchannelPool::RegisterSubchannel(
    SubchannelKey* key, grpc_subchannel* constructed) {
  // Check to see if a subchannel already exists.
  grpc_subchannel* c = static_cast<grpc_subchannel*>(
      grpc_avl_get(subchannel_map_, key, nullptr));
  if (c != nullptr) {
    // The subchannel already exists. Reuse it.
    c = GRPC_SUBCHANNEL_REF_FROM_WEAK_REF(c, "subchannel_register+reuse");
    GRPC_SUBCHANNEL_UNREF(constructed, "subchannel_register+found_existing");
  } else {
    // There hasn't been such subchannel. Add one.
    subchannel_map_ = grpc_avl_add(
        subchannel_map_, grpc_core::New<SubchannelKey>(*key),
        GRPC_SUBCHANNEL_WEAK_REF(constructed, "subchannel_register+new"),
        nullptr);
    c = constructed;
  }
  return c;
}

void LocalSubchannelPool::UnregisterSubchannel(SubchannelKey* key,
                                               grpc_subchannel* constructed) {
  // TODO(juanlishen): Why it may have changed?
  // Check to see if this key still refers to the previously registered
  // subchannel.
  grpc_subchannel* c = static_cast<grpc_subchannel*>(
      grpc_avl_get(subchannel_map_, key, nullptr));
  if (c != constructed) return;
  subchannel_map_ = grpc_avl_remove(subchannel_map_, key, nullptr);
}

grpc_subchannel* LocalSubchannelPool::FindSubchannel(SubchannelKey* key) {
  return GRPC_SUBCHANNEL_REF_FROM_WEAK_REF(
      (grpc_subchannel*)grpc_avl_get(subchannel_map_, key, nullptr),
      "found_from_pool");
}

}  // namespace grpc_core
