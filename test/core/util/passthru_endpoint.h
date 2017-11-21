/*
 *
 * Copyright 2016 gRPC authors.
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

#ifndef MOCK_ENDPOINT_H
#define MOCK_ENDPOINT_H

#include <grpc/support/atm.h>

#include "src/core/lib/iomgr/endpoint.h"

typedef struct {
  gpr_atm num_writes;
} grpc_passthru_endpoint_stats;

void grpc_passthru_endpoint_create(grpc_endpoint** client,
                                   grpc_endpoint** server,
                                   grpc_resource_quota* resource_quota,
                                   grpc_passthru_endpoint_stats* stats);

#endif
