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

#ifndef TRICKLE_ENDPOINT_H
#define TRICKLE_ENDPOINT_H

#include "src/core/lib/iomgr/endpoint.h"

grpc_endpoint* grpc_trickle_endpoint_create(grpc_endpoint* wrap,
                                            double bytes_per_second);

/* Allow up to \a bytes through the endpoint. Returns the new backlog. */
size_t grpc_trickle_endpoint_trickle(grpc_endpoint* endpoint);

size_t grpc_trickle_get_backlog(grpc_endpoint* endpoint);

#endif
