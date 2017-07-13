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

#ifndef GRPC_CORE_LIB_COMPRESSION_ALGORITHM_METADATA_H
#define GRPC_CORE_LIB_COMPRESSION_ALGORITHM_METADATA_H

#include <grpc/compression.h>
#include "src/core/lib/transport/metadata.h"

/** Return compression algorithm based metadata value */
grpc_slice grpc_compression_algorithm_slice(
    grpc_compression_algorithm algorithm);

/** Return compression algorithm based metadata element (grpc-encoding: xxx) */
grpc_mdelem grpc_compression_encoding_mdelem(
    grpc_compression_algorithm algorithm);

/** Find compression algorithm based on passed in mdstr - returns
 * GRPC_COMPRESS_ALGORITHM_COUNT on failure */
grpc_compression_algorithm grpc_compression_algorithm_from_slice(
    grpc_slice str);

#endif /* GRPC_CORE_LIB_COMPRESSION_ALGORITHM_METADATA_H */
