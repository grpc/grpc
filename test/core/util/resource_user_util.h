// Copyright 2021 The gRPC Authors
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
#ifndef GRPC_TEST_CORE_UTIL_RESOURCE_USER_UTIL_H
#define GRPC_TEST_CORE_UTIL_RESOURCE_USER_UTIL_H

#include "src/core/lib/iomgr/resource_quota.h"

grpc_resource_user* grpc_resource_user_create_unlimited(
    grpc_resource_quota* resource_quota = nullptr);

grpc_slice_allocator* grpc_slice_allocator_create_unlimited();

#endif  // GRPC_TEST_CORE_UTIL_RESOURCE_USER_UTIL_H
