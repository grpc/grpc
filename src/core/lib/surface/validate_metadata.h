/*
 *
 * Copyright 2017 gRPC authors.
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

#ifndef GRPC_CORE_LIB_SURFACE_VALIDATE_METADATA_H
#define GRPC_CORE_LIB_SURFACE_VALIDATE_METADATA_H

#include <grpc/slice.h>
#include "src/core/lib/iomgr/error.h"

#ifdef __cplusplus
extern "C" {
#endif

grpc_error* grpc_validate_header_key_is_legal(grpc_slice slice);
grpc_error* grpc_validate_header_nonbin_value_is_legal(grpc_slice slice);

#ifdef __cplusplus
}
#endif

#endif /* GRPC_CORE_LIB_SURFACE_VALIDATE_METADATA_H */
