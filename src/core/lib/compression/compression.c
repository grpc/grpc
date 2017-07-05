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

#include <stdlib.h>
#include <string.h>

#include <grpc/compression.h>
#include <grpc/support/useful.h>

#include "src/core/lib/compression/algorithm_metadata.h"
#include "src/core/lib/surface/api_trace.h"
#include "src/core/lib/transport/static_metadata.h"

int grpc_compression_algorithm_parse(grpc_slice name,
                                     grpc_compression_algorithm *algorithm) {
  /* we use strncmp not only because it's safer (even though in this case it
   * doesn't matter, given that we are comparing against string literals, but
   * because this way we needn't have "name" nil-terminated (useful for slice
   * data, for example) */
  if (grpc_slice_eq(name, GRPC_MDSTR_IDENTITY)) {
    *algorithm = GRPC_COMPRESS_NONE;
    return 1;
  } else if (grpc_slice_eq(name, GRPC_MDSTR_GZIP)) {
    *algorithm = GRPC_COMPRESS_GZIP;
    return 1;
  } else if (grpc_slice_eq(name, GRPC_MDSTR_DEFLATE)) {
    *algorithm = GRPC_COMPRESS_DEFLATE;
    return 1;
  } else {
    return 0;
  }
}

int grpc_compression_algorithm_name(grpc_compression_algorithm algorithm,
                                    char **name) {
  GRPC_API_TRACE("grpc_compression_algorithm_parse(algorithm=%d, name=%p)", 2,
                 ((int)algorithm, name));
  switch (algorithm) {
    case GRPC_COMPRESS_NONE:
      *name = "identity";
      return 1;
    case GRPC_COMPRESS_DEFLATE:
      *name = "deflate";
      return 1;
    case GRPC_COMPRESS_GZIP:
      *name = "gzip";
      return 1;
    case GRPC_COMPRESS_ALGORITHMS_COUNT:
      return 0;
  }
  return 0;
}

grpc_compression_algorithm grpc_compression_algorithm_from_slice(
    grpc_slice str) {
  if (grpc_slice_eq(str, GRPC_MDSTR_IDENTITY)) return GRPC_COMPRESS_NONE;
  if (grpc_slice_eq(str, GRPC_MDSTR_DEFLATE)) return GRPC_COMPRESS_DEFLATE;
  if (grpc_slice_eq(str, GRPC_MDSTR_GZIP)) return GRPC_COMPRESS_GZIP;
  return GRPC_COMPRESS_ALGORITHMS_COUNT;
}

grpc_slice grpc_compression_algorithm_slice(
    grpc_compression_algorithm algorithm) {
  switch (algorithm) {
    case GRPC_COMPRESS_NONE:
      return GRPC_MDSTR_IDENTITY;
    case GRPC_COMPRESS_DEFLATE:
      return GRPC_MDSTR_DEFLATE;
    case GRPC_COMPRESS_GZIP:
      return GRPC_MDSTR_GZIP;
    case GRPC_COMPRESS_ALGORITHMS_COUNT:
      return grpc_empty_slice();
  }
  return grpc_empty_slice();
}

grpc_mdelem grpc_compression_encoding_mdelem(
    grpc_compression_algorithm algorithm) {
  switch (algorithm) {
    case GRPC_COMPRESS_NONE:
      return GRPC_MDELEM_GRPC_ENCODING_IDENTITY;
    case GRPC_COMPRESS_DEFLATE:
      return GRPC_MDELEM_GRPC_ENCODING_DEFLATE;
    case GRPC_COMPRESS_GZIP:
      return GRPC_MDELEM_GRPC_ENCODING_GZIP;
    default:
      break;
  }
  return GRPC_MDNULL;
}

void grpc_compression_options_init(grpc_compression_options *opts) {
  memset(opts, 0, sizeof(*opts));
  /* all enabled by default */
  opts->enabled_algorithms_bitset = (1u << GRPC_COMPRESS_ALGORITHMS_COUNT) - 1;
}

void grpc_compression_options_enable_algorithm(
    grpc_compression_options *opts, grpc_compression_algorithm algorithm) {
  GPR_BITSET(&opts->enabled_algorithms_bitset, algorithm);
}

void grpc_compression_options_disable_algorithm(
    grpc_compression_options *opts, grpc_compression_algorithm algorithm) {
  GPR_BITCLEAR(&opts->enabled_algorithms_bitset, algorithm);
}

int grpc_compression_options_is_algorithm_enabled(
    const grpc_compression_options *opts,
    grpc_compression_algorithm algorithm) {
  return GPR_BITGET(opts->enabled_algorithms_bitset, algorithm);
}

/* TODO(dgq): Add the ability to specify parameters to the individual
 * compression algorithms */
grpc_compression_algorithm grpc_compression_algorithm_for_level(
    grpc_compression_level level, uint32_t accepted_encodings) {
  GRPC_API_TRACE("grpc_compression_algorithm_for_level(level=%d)", 1,
                 ((int)level));
  if (level > GRPC_COMPRESS_LEVEL_HIGH) {
    gpr_log(GPR_ERROR, "Unknown compression level %d.", (int)level);
    abort();
  }

  const size_t num_supported =
      GPR_BITCOUNT(accepted_encodings) - 1; /* discard NONE */
  if (level == GRPC_COMPRESS_LEVEL_NONE || num_supported == 0) {
    return GRPC_COMPRESS_NONE;
  }

  GPR_ASSERT(level > 0);

  /* Establish a "ranking" or compression algorithms in increasing order of
   * compression.
   * This is simplistic and we will probably want to introduce other dimensions
   * in the future (cpu/memory cost, etc). */
  const grpc_compression_algorithm algos_ranking[] = {GRPC_COMPRESS_GZIP,
                                                      GRPC_COMPRESS_DEFLATE};

  /* intersect algos_ranking with the supported ones keeping the ranked order */
  grpc_compression_algorithm
      sorted_supported_algos[GRPC_COMPRESS_ALGORITHMS_COUNT];
  size_t algos_supported_idx = 0;
  for (size_t i = 0; i < GPR_ARRAY_SIZE(algos_ranking); i++) {
    const grpc_compression_algorithm alg = algos_ranking[i];
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
