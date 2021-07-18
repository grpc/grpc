//
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
//

#include <grpc/support/port_platform.h>

#include "src/core/ext/xds/xds_max_retries_map.h"

#include "src/core/lib/gpr/useful.h"

namespace grpc_core {

namespace {

void* XdsMaxRetriesMapArgCopy(void* p) {
  XdsMaxRetriesMap* xds_max_retries_map = static_cast<XdsMaxRetriesMap*>(p);
  return xds_max_retries_map->Ref().release();
}

void XdsMaxRetriesMapArgDestroy(void* p) {
  XdsMaxRetriesMap* xds_max_retries_map = static_cast<XdsMaxRetriesMap*>(p);
  xds_max_retries_map->Unref();
}

int XdsMaxRetriesMapArgCmp(void* p, void* q) { return GPR_ICMP(p, q); }

const grpc_arg_pointer_vtable kChannelArgVtable = {XdsMaxRetriesMapArgCopy,
                                                   XdsMaxRetriesMapArgDestroy,
                                                   XdsMaxRetriesMapArgCmp};

}  // namespace

grpc_arg XdsMaxRetriesMap::MakeChannelArg() const {
  return grpc_channel_arg_pointer_create(
      const_cast<char*>(GRPC_ARG_CLUSTER_MAX_RETRIES_MAP),
      const_cast<XdsMaxRetriesMap*>(this), &kChannelArgVtable);
}

RefCountedPtr<XdsMaxRetriesMap> XdsMaxRetriesMap::GetFromChannelArgs(
    const grpc_channel_args* args) {
  XdsMaxRetriesMap* xds_max_retries_map =
      grpc_channel_args_find_pointer<XdsMaxRetriesMap>(
          args, GRPC_ARG_CLUSTER_MAX_RETRIES_MAP);
  return xds_max_retries_map != nullptr ? xds_max_retries_map->Ref() : nullptr;
}

}  // namespace grpc_core
