/*
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
 */

/*
 * WARNING: Auto-generated code.
 *
 * To make changes to this file, change
 * tools/codegen/core/gen_static_metadata.py, and then re-run it.
 *
 * This file contains the mapping from the index of each metadata element in the
 * grpc static metadata table to the index of that element in the hpack static
 * metadata table. If the element is not contained in the static hpack table,
 * then the returned index is 0.
 */

#include <grpc/support/port_platform.h>

#include "src/core/ext/transport/chttp2/transport/hpack_mapping.h"

const uint8_t grpc_hpack_static_mdelem_indices[GRPC_STATIC_MDELEM_COUNT] = {
    0,  0,  0,  0,  0,  0,  0,  0,  3,  8,  13, 6,  7,  0,  1,  2,  0,  4,
    5,  9,  10, 11, 12, 14, 15, 0,  16, 17, 18, 19, 20, 21, 22, 23, 24, 25,
    0,  0,  26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40, 41,
    42, 43, 44, 0,  0,  45, 46, 47, 48, 49, 50, 51, 52, 53, 54, 55, 56, 57,
    58, 59, 60, 61, 0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
};
