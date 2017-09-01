/*
 *
 * Copyright 2015 gRPC authors.
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

#ifndef GRPC_CORE_LIB_SECURITY_TRANSPORT_SECURE_ENDPOINT_H
#define GRPC_CORE_LIB_SECURITY_TRANSPORT_SECURE_ENDPOINT_H

#include <grpc/slice.h>
#include "src/core/lib/iomgr/endpoint.h"

struct tsi_frame_protector;

extern grpc_tracer_flag grpc_trace_secure_endpoint;

/* Takes ownership of protector and to_wrap, and refs leftover_slices. */
grpc_endpoint *grpc_secure_endpoint_create(
    struct tsi_frame_protector *protector, grpc_endpoint *to_wrap,
    grpc_slice *leftover_slices, size_t leftover_nslices);

#endif /* GRPC_CORE_LIB_SECURITY_TRANSPORT_SECURE_ENDPOINT_H */
