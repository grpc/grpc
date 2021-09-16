// Copyright 2021 gRPC authors.
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

#ifndef GRPC_CORE_LIB_RESOURCE_QUOTA_MAKE_SLICE_H_
#define GRPC_CORE_LIB_RESOURCE_QUOTA_MAKE_SLICE_H_

#include <grpc/support/port_platform.h>

#include <grpc/slice.h>

#include "src/core/lib/resource_quota/memory_quota.h"

namespace grpc_core {

grpc_slice MakeSlice(MemoryQuota* memory_quota, MemoryRequest request);

}  // namespace grpc_core

#endif  // GRPC_CORE_LIB_RESOURCE_QUOTA_MAKE_SLICE_H_
