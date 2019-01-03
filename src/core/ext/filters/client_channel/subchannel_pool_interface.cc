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

#include "src/core/ext/filters/client_channel/subchannel_pool_interface.h"

#include "src/core/ext/filters/client_channel/subchannel_key.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/gprpp/memory.h"

namespace grpc_core {

TraceFlag grpc_subchannel_pool_trace(false, "subchannel_pool");

namespace {

static void sck_avl_destroy(void* p, void* user_data) {
  SubchannelKey* key = static_cast<SubchannelKey*>(p);
  grpc_core::Delete(key);
}

static void* sck_avl_copy(void* p, void* unused) {
  const SubchannelKey* key = static_cast<const SubchannelKey*>(p);
  auto new_key = grpc_core::New<SubchannelKey>(*key);
  return static_cast<void*>(new_key);
}

static long sck_avl_compare(void* a, void* b, void* unused) {
  const SubchannelKey* key_a = static_cast<const SubchannelKey*>(a);
  const SubchannelKey* key_b = static_cast<const SubchannelKey*>(b);
  return key_a->Cmp(*key_b);
}

static void scv_avl_destroy(void* p, void* user_data) {
  GRPC_SUBCHANNEL_WEAK_UNREF((grpc_subchannel*)p, "subchannel_index");
}

static void* scv_avl_copy(void* p, void* unused) {
  GRPC_SUBCHANNEL_WEAK_REF((grpc_subchannel*)p, "subchannel_index");
  return p;
}

}  // namespace

const grpc_avl_vtable SubchannelPoolInterface::subchannel_avl_vtable_ = {
    sck_avl_destroy,  // destroy_key
    sck_avl_copy,     // copy_key
    sck_avl_compare,  // compare_keys
    scv_avl_destroy,  // destroy_value
    scv_avl_copy      // copy_value
};

}  // namespace grpc_core
