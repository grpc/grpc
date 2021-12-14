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

#ifndef GRPC_CORE_EXT_FILTERS_CLIENT_CHANNEL_LB_POLICY_RING_HASH_RING_HASH_H
#define GRPC_CORE_EXT_FILTERS_CLIENT_CHANNEL_LB_POLICY_RING_HASH_RING_HASH_H

#include <grpc/support/port_platform.h>

#include <stdlib.h>

#include "src/core/lib/iomgr/error.h"
#include "src/core/lib/json/json.h"

namespace grpc_core {
extern const char* kRequestRingHashAttribute;

// Helper Parsing method to parse ring hash policy configs; for example, ring
// hash size validity.
void ParseRingHashLbConfig(const Json& json, size_t* min_ring_size,
                           size_t* max_ring_size,
                           std::vector<grpc_error_handle>* error_list);
}  // namespace grpc_core

#endif  // GRPC_CORE_EXT_FILTERS_CLIENT_CHANNEL_LB_POLICY_RING_HASH_RING_HASH_H
