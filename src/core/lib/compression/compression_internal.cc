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

#include <stdlib.h>
#include <string.h>

#include <grpc/compression.h>

#include "src/core/lib/compression/algorithm_metadata.h"
#include "src/core/lib/compression/compression_internal.h"
#include "src/core/lib/gpr/useful.h"
#include "src/core/lib/slice/slice_utils.h"
#include "src/core/lib/surface/api_trace.h"
#include "src/core/lib/transport/static_metadata.h"

/* Interfaces related to MD */

grpc_message_compression_algorithm
grpc_message_compression_algorithm_from_slice(const grpc_slice& str) {
  if (grpc_slice_eq_static_interned(str, GRPC_MDSTR_IDENTITY))
    return GRPC_MESSAGE_COMPRESS_NONE;
  if (grpc_slice_eq_static_interned(str, GRPC_MDSTR_DEFLATE))
    return GRPC_MESSAGE_COMPRESS_DEFLATE;
  if (grpc_slice_eq_static_interned(str, GRPC_MDSTR_GZIP))
    return GRPC_MESSAGE_COMPRESS_GZIP;
  return GRPC_MESSAGE_COMPRESS_ALGORITHMS_COUNT;
}

grpc_stream_compression_algorithm grpc_stream_compression_algorithm_from_slice(
    const grpc_slice& str) {
  if (grpc_slice_eq_static_interned(str, GRPC_MDSTR_IDENTITY))
    return GRPC_STREAM_COMPRESS_NONE;
  if (grpc_slice_eq_static_interned(str, GRPC_MDSTR_GZIP))
    return GRPC_STREAM_COMPRESS_GZIP;
  return GRPC_STREAM_COMPRESS_ALGORITHMS_COUNT;
}

grpc_mdelem grpc_message_compression_encoding_mdelem(
    grpc_message_compression_algorithm algorithm) {
  switch (algorithm) {
    case GRPC_MESSAGE_COMPRESS_NONE:
      return GRPC_MDELEM_GRPC_ENCODING_IDENTITY;
    case GRPC_MESSAGE_COMPRESS_DEFLATE:
      return GRPC_MDELEM_GRPC_ENCODING_DEFLATE;
    case GRPC_MESSAGE_COMPRESS_GZIP:
      return GRPC_MDELEM_GRPC_ENCODING_GZIP;
    default:
      break;
  }
  return GRPC_MDNULL;
}

grpc_mdelem grpc_stream_compression_encoding_mdelem(
    grpc_stream_compression_algorithm algorithm) {
  switch (algorithm) {
    case GRPC_STREAM_COMPRESS_NONE:
      return GRPC_MDELEM_CONTENT_ENCODING_IDENTITY;
    case GRPC_STREAM_COMPRESS_GZIP:
      return GRPC_MDELEM_CONTENT_ENCODING_GZIP;
    default:
      break;
  }
  return GRPC_MDNULL;
}

/* Interfaces performing transformation between compression algorithms and
 * levels. */
grpc_message_compression_algorithm
grpc_compression_algorithm_to_message_compression_algorithm(
    grpc_compression_algorithm algo) {
  switch (algo) {
    case GRPC_COMPRESS_DEFLATE:
      return GRPC_MESSAGE_COMPRESS_DEFLATE;
    case GRPC_COMPRESS_GZIP:
      return GRPC_MESSAGE_COMPRESS_GZIP;
    default:
      return GRPC_MESSAGE_COMPRESS_NONE;
  }
}

grpc_stream_compression_algorithm
grpc_compression_algorithm_to_stream_compression_algorithm(
    grpc_compression_algorithm algo) {
  switch (algo) {
    case GRPC_COMPRESS_STREAM_GZIP:
      return GRPC_STREAM_COMPRESS_GZIP;
    default:
      return GRPC_STREAM_COMPRESS_NONE;
  }
}

uint32_t grpc_compression_bitset_to_message_bitset(uint32_t bitset) {
  return bitset & ((1u << GRPC_MESSAGE_COMPRESS_ALGORITHMS_COUNT) - 1);
}

uint32_t grpc_compression_bitset_to_stream_bitset(uint32_t bitset) {
  uint32_t identity = (bitset & 1u);
  uint32_t other_bits =
      (bitset >> (GRPC_MESSAGE_COMPRESS_ALGORITHMS_COUNT - 1)) &
      ((1u << GRPC_STREAM_COMPRESS_ALGORITHMS_COUNT) - 2);
  return identity | other_bits;
}

uint32_t grpc_compression_bitset_from_message_stream_compression_bitset(
    uint32_t message_bitset, uint32_t stream_bitset) {
  uint32_t offset_stream_bitset =
      (stream_bitset & 1u) |
      ((stream_bitset & (~1u)) << (GRPC_MESSAGE_COMPRESS_ALGORITHMS_COUNT - 1));
  return message_bitset | offset_stream_bitset;
}

int grpc_compression_algorithm_from_message_stream_compression_algorithm(
    grpc_compression_algorithm* algorithm,
    grpc_message_compression_algorithm message_algorithm,
    grpc_stream_compression_algorithm stream_algorithm) {
  if (message_algorithm != GRPC_MESSAGE_COMPRESS_NONE &&
      stream_algorithm != GRPC_STREAM_COMPRESS_NONE) {
    *algorithm = GRPC_COMPRESS_NONE;
    return 0;
  }
  if (message_algorithm == GRPC_MESSAGE_COMPRESS_NONE) {
    switch (stream_algorithm) {
      case GRPC_STREAM_COMPRESS_NONE:
        *algorithm = GRPC_COMPRESS_NONE;
        return 1;
      case GRPC_STREAM_COMPRESS_GZIP:
        *algorithm = GRPC_COMPRESS_STREAM_GZIP;
        return 1;
      default:
        *algorithm = GRPC_COMPRESS_NONE;
        return 0;
    }
  } else {
    switch (message_algorithm) {
      case GRPC_MESSAGE_COMPRESS_NONE:
        *algorithm = GRPC_COMPRESS_NONE;
        return 1;
      case GRPC_MESSAGE_COMPRESS_DEFLATE:
        *algorithm = GRPC_COMPRESS_DEFLATE;
        return 1;
      case GRPC_MESSAGE_COMPRESS_GZIP:
        *algorithm = GRPC_COMPRESS_GZIP;
        return 1;
      default:
        *algorithm = GRPC_COMPRESS_NONE;
        return 0;
    }
  }
  return 0;
}

/* Interfaces for message compression. */

int grpc_message_compression_algorithm_name(
    grpc_message_compression_algorithm algorithm, const char** name) {
  GRPC_API_TRACE(
      "grpc_message_compression_algorithm_name(algorithm=%d, name=%p)", 2,
      ((int)algorithm, name));
  switch (algorithm) {
    case GRPC_MESSAGE_COMPRESS_NONE:
      *name = "identity";
      return 1;
    case GRPC_MESSAGE_COMPRESS_DEFLATE:
      *name = "deflate";
      return 1;
    case GRPC_MESSAGE_COMPRESS_GZIP:
      *name = "gzip";
      return 1;
    case GRPC_MESSAGE_COMPRESS_ALGORITHMS_COUNT:
      return 0;
  }
  return 0;
}

/* TODO(dgq): Add the ability to specify parameters to the individual
 * compression algorithms */
grpc_message_compression_algorithm grpc_message_compression_algorithm_for_level(
    grpc_compression_level level, uint32_t accepted_encodings) {
  GRPC_API_TRACE("grpc_message_compression_algorithm_for_level(level=%d)", 1,
                 ((int)level));
  if (level > GRPC_COMPRESS_LEVEL_HIGH) {
    gpr_log(GPR_ERROR, "Unknown message compression level %d.",
            static_cast<int>(level));
    abort();
  }

  const size_t num_supported =
      GPR_BITCOUNT(accepted_encodings) - 1; /* discard NONE */
  if (level == GRPC_COMPRESS_LEVEL_NONE || num_supported == 0) {
    return GRPC_MESSAGE_COMPRESS_NONE;
  }

  GPR_ASSERT(level > 0);

  /* Establish a "ranking" or compression algorithms in increasing order of
   * compression.
   * This is simplistic and we will probably want to introduce other dimensions
   * in the future (cpu/memory cost, etc). */
  const grpc_message_compression_algorithm algos_ranking[] = {
      GRPC_MESSAGE_COMPRESS_GZIP, GRPC_MESSAGE_COMPRESS_DEFLATE};

  /* intersect algos_ranking with the supported ones keeping the ranked order */
  grpc_message_compression_algorithm
      sorted_supported_algos[GRPC_MESSAGE_COMPRESS_ALGORITHMS_COUNT];
  size_t algos_supported_idx = 0;
  for (size_t i = 0; i < GPR_ARRAY_SIZE(algos_ranking); i++) {
    const grpc_message_compression_algorithm alg = algos_ranking[i];
    for (size_t j = 0; j < num_supported; j++) {
      if (GPR_BITGET(accepted_encodings, alg) == 1) {
        /* if \a alg in supported */
        sorted_supported_algos[algos_supported_idx++] = alg;
        break;
      }
    }
    if (algos_supported_idx == num_supported) break;
  }

  switch (level) {
    case GRPC_COMPRESS_LEVEL_NONE:
      abort(); /* should have been handled already */
    case GRPC_COMPRESS_LEVEL_LOW:
      return sorted_supported_algos[0];
    case GRPC_COMPRESS_LEVEL_MED:
      return sorted_supported_algos[num_supported / 2];
    case GRPC_COMPRESS_LEVEL_HIGH:
      return sorted_supported_algos[num_supported - 1];
    default:
      abort();
  };
}

int grpc_message_compression_algorithm_parse(
    grpc_slice value, grpc_message_compression_algorithm* algorithm) {
  if (grpc_slice_eq_static_interned(value, GRPC_MDSTR_IDENTITY)) {
    *algorithm = GRPC_MESSAGE_COMPRESS_NONE;
    return 1;
  } else if (grpc_slice_eq_static_interned(value, GRPC_MDSTR_DEFLATE)) {
    *algorithm = GRPC_MESSAGE_COMPRESS_DEFLATE;
    return 1;
  } else if (grpc_slice_eq_static_interned(value, GRPC_MDSTR_GZIP)) {
    *algorithm = GRPC_MESSAGE_COMPRESS_GZIP;
    return 1;
  } else {
    return 0;
  }
  return 0;
}

/* Interfaces for stream compression. */

int grpc_stream_compression_algorithm_parse(
    grpc_slice value, grpc_stream_compression_algorithm* algorithm) {
  if (grpc_slice_eq_static_interned(value, GRPC_MDSTR_IDENTITY)) {
    *algorithm = GRPC_STREAM_COMPRESS_NONE;
    return 1;
  } else if (grpc_slice_eq_static_interned(value, GRPC_MDSTR_GZIP)) {
    *algorithm = GRPC_STREAM_COMPRESS_GZIP;
    return 1;
  } else {
    return 0;
  }
  return 0;
}
