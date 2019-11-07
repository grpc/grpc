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

Subchannel* LocalSubchannelPool::RegisterSubchannel(SubchannelKey* key,
                                                    Subchannel* constructed) {
  // Check to see if a subchannel already exists.
  Subchannel* c =
      static_cast<Subchannel*>(grpc_avl_get(subchannel_map_, key, nullptr));
  if (c != nullptr) {
    // The subchannel already exists. Reuse it.
    c = GRPC_SUBCHANNEL_REF(c, "subchannel_register+reuse");
    GRPC_SUBCHANNEL_UNREF(constructed, "subchannel_register+found_existing");
  } else {
    // There hasn't been such subchannel. Add one.
    subchannel_map_ = grpc_avl_add(subchannel_map_, new SubchannelKey(*key),
                                   constructed, nullptr);
    c = constructed;
  }
  return c;
}

void LocalSubchannelPool::UnregisterSubchannel(SubchannelKey* key) {
  subchannel_map_ = grpc_avl_remove(subchannel_map_, key, nullptr);
}

Subchannel* LocalSubchannelPool::FindSubchannel(SubchannelKey* key) {
  Subchannel* c =
      static_cast<Subchannel*>(grpc_avl_get(subchannel_map_, key, nullptr));
  return c == nullptr ? c : GRPC_SUBCHANNEL_REF(c, "found_from_pool");
}

namespace {

void sck_avl_destroy(void* p, void* /*user_data*/) {
  SubchannelKey* key = static_cast<SubchannelKey*>(p);
  delete key;
}

void* sck_avl_copy(void* p, void* /*unused*/) {
  const SubchannelKey* key = static_cast<const SubchannelKey*>(p);
  auto new_key = new SubchannelKey(*key);
  return static_cast<void*>(new_key);
}

long sck_avl_compare(void* a, void* b, void* /*unused*/) {
  const SubchannelKey* key_a = static_cast<const SubchannelKey*>(a);
  const SubchannelKey* key_b = static_cast<const SubchannelKey*>(b);
  return key_a->Cmp(*key_b);
}

void scv_avl_destroy(void* /*p*/, void* /*user_data*/) {}

void* scv_avl_copy(void* p, void* /*unused*/) { return p; }

}  // namespace

const grpc_avl_vtable LocalSubchannelPool::subchannel_avl_vtable_ = {
    sck_avl_destroy,  // destroy_key
    sck_avl_copy,     // copy_key
    sck_avl_compare,  // compare_keys
    scv_avl_destroy,  // destroy_value
    scv_avl_copy      // copy_value
};

}  // namespace grpc_core
