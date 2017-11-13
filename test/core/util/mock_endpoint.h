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

#include "src/core/lib/iomgr/endpoint.h"

grpc_endpoint* grpc_mock_endpoint_create(void (*on_write)(grpc_slice slice),
                                         grpc_resource_quota* resource_quota);
void grpc_mock_endpoint_put_read(grpc_exec_ctx* exec_ctx,
                                 grpc_endpoint* mock_endpoint,
                                 grpc_slice slice);

#endif
