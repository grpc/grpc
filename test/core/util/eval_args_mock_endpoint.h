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

#ifndef GRPC_TEST_CORE_UTIL_EVAL_ARGS_MOCK_ENDPOINT_H
#define GRPC_TEST_CORE_UTIL_EVAL_ARGS_MOCK_ENDPOINT_H

#include <grpc/support/port_platform.h>

#include "src/core/lib/iomgr/endpoint.h"

grpc_endpoint* grpc_eval_args_mock_endpoint_create(const char* local_address,
                                                   const int local_port,
                                                   const char* peer_address,
                                                   const int peer_port);
void grpc_eval_args_mock_endpoint_destroy(grpc_endpoint* ep);

#endif  // GRPC_TEST_CORE_UTIL_EVAL_ARGS_MOCK_ENDPOINT_H
