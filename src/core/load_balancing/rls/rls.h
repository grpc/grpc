//
// Copyright 2024 gRPC authors.
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

#ifndef GRPC_SRC_CORE_LOAD_BALANCING_RLS_RLS_H
#define GRPC_SRC_CORE_LOAD_BALANCING_RLS_RLS_H

#include <grpc/support/port_platform.h>

// A test-only channel arg to set the instance ID of the RLS LB
// policy for use in metric labels.
#define GRPC_ARG_TEST_ONLY_RLS_INSTANCE_ID "grpc.test-only.rls.instance_id"

#endif  // GRPC_SRC_CORE_LOAD_BALANCING_RLS_RLS_H
