/*
 *
 * Copyright 2018 gRPC authors.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

#ifndef GRPC_TEST_CORE_UTIL_TEST_LB_POLICIES_H
#define GRPC_TEST_CORE_UTIL_TEST_LB_POLICIES_H

#include "src/core/ext/filters/client_channel/lb_policy.h"

namespace grpc_core {

typedef void (*InterceptRecvTrailingMetadataCallback)(
    void*, const LoadBalancingPolicy::BackendMetricData*);

// Registers an LB policy called "intercept_trailing_metadata_lb" that
// invokes cb with argument user_data when trailing metadata is received
// for each call.
void RegisterInterceptRecvTrailingMetadataLoadBalancingPolicy(
    InterceptRecvTrailingMetadataCallback cb, void* user_data);

}  // namespace grpc_core

#endif  // GRPC_TEST_CORE_UTIL_TEST_LB_POLICIES_H
