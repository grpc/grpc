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

#include <grpc/support/port_platform.h>

#include "src/core/lib/compression/compression_internal.h"

#include <stdlib.h>
#include <string.h>

#include "absl/container/inlined_vector.h"

#include <grpc/compression.h>

#include "src/core/lib/gpr/useful.h"
#include "src/core/lib/slice/slice_utils.h"
#include "src/core/lib/surface/api_trace.h"
#include "src/core/lib/transport/static_metadata.h"

namespace grpc_core {

const char* CompressionAlgorithmToString(grpc_compression_algorithm algorithm) {
  switch (algorithm) {
    case GRPC_COMPRESS_NONE:
      return "identity";
    case GRPC_COMPRESS_DEFLATE:
      return "deflate";
    case GRPC_COMPRESS_GZIP:
      return "gzip";
    case GRPC_COMPRESS_ALGORITHMS_COUNT:
      return nullptr;
  }
}

grpc_compression_algorithm
CompressionAlgorithmSet::CompressionAlgorithmForLevel(
    grpc_compression_level level) const {
  GRPC_API_TRACE("grpc_message_compression_algorithm_for_level(level=%d)", 1,
                 ((int)level));
  if (level > GRPC_COMPRESS_LEVEL_HIGH) {
    gpr_log(GPR_ERROR, "Unknown message compression level %d.",
            static_cast<int>(level));
    abort();
  }

  if (level == GRPC_COMPRESS_LEVEL_NONE || set_.count() == 0) {
    return GRPC_COMPRESS_NONE;
  }

  GPR_ASSERT(level > 0);

  /* Establish a "ranking" or compression algorithms in increasing order of
   * compression.
   * This is simplistic and we will probably want to introduce other dimensions
   * in the future (cpu/memory cost, etc). */
  absl::InlinedVector<grpc_compression_algorithm,
                      GRPC_COMPRESS_ALGORITHMS_COUNT>
      algos;
  for (size_t i = 0; i < GRPC_COMPRESS_ALGORITHMS_COUNT; i++) {
    if (set_.is_set(i)) {
      algos.push_back(static_cast<grpc_compression_algorithm>(i));
    }
  }

  switch (level) {
    case GRPC_COMPRESS_LEVEL_NONE:
      abort(); /* should have been handled already */
    case GRPC_COMPRESS_LEVEL_LOW:
      return algos[0];
    case GRPC_COMPRESS_LEVEL_MED:
      return algos[algos.size() / 2];
    case GRPC_COMPRESS_LEVEL_HIGH:
      return algos.back();
    default:
      abort();
  };
}

}  // namespace grpc_core
