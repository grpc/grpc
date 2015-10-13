/*
 *
 * Copyright 2015, Google Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *     * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 * copyright notice, this list of conditions and the following disclaimer
 * in the documentation and/or other materials provided with the
 * distribution.
 *     * Neither the name of Google Inc. nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

#include <stdlib.h>
#include <string.h>

#include <grpc/compression.h>
#include <grpc/support/useful.h>

#include "src/core/surface/api_trace.h"

int grpc_compression_algorithm_parse(const char *name, size_t name_length,
                                     grpc_compression_algorithm *algorithm) {
  /* we use strncmp not only because it's safer (even though in this case it
   * doesn't matter, given that we are comparing against string literals, but
   * because this way we needn't have "name" nil-terminated (useful for slice
   * data, for example) */
  GRPC_API_TRACE(
      "grpc_compression_algorithm_parse("
      "name=%*.*s, name_length=%lu, algorithm=%p)",
      5, ((int)name_length, (int)name_length, name, (unsigned long)name_length,
          algorithm));
  if (name_length == 0) {
    return 0;
  }
  if (strncmp(name, "identity", name_length) == 0) {
    *algorithm = GRPC_COMPRESS_NONE;
  } else if (strncmp(name, "gzip", name_length) == 0) {
    *algorithm = GRPC_COMPRESS_GZIP;
  } else if (strncmp(name, "deflate", name_length) == 0) {
    *algorithm = GRPC_COMPRESS_DEFLATE;
  } else {
    return 0;
  }
  return 1;
}

int grpc_compression_algorithm_name(grpc_compression_algorithm algorithm,
                                    char **name) {
  GRPC_API_TRACE("grpc_compression_algorithm_parse(algorithm=%d, name=%p)", 2,
                 ((int)algorithm, name));
  switch (algorithm) {
    case GRPC_COMPRESS_NONE:
      *name = "identity";
      break;
    case GRPC_COMPRESS_DEFLATE:
      *name = "deflate";
      break;
    case GRPC_COMPRESS_GZIP:
      *name = "gzip";
      break;
    default:
      return 0;
  }
  return 1;
}

/* TODO(dgq): Add the ability to specify parameters to the individual
 * compression algorithms */
grpc_compression_algorithm grpc_compression_algorithm_for_level(
    grpc_compression_level level) {
  GRPC_API_TRACE("grpc_compression_algorithm_for_level(level=%d)", 1,
                 ((int)level));
  switch (level) {
    case GRPC_COMPRESS_LEVEL_NONE:
      return GRPC_COMPRESS_NONE;
    case GRPC_COMPRESS_LEVEL_LOW:
    case GRPC_COMPRESS_LEVEL_MED:
    case GRPC_COMPRESS_LEVEL_HIGH:
      return GRPC_COMPRESS_DEFLATE;
    default:
      /* we shouldn't be making it here */
      abort();
      return GRPC_COMPRESS_NONE;
  }
}

grpc_compression_level grpc_compression_level_for_algorithm(
    grpc_compression_algorithm algorithm) {
  grpc_compression_level clevel;
  GRPC_API_TRACE("grpc_compression_level_for_algorithm(algorithm=%d)", 1,
                 ((int)algorithm));
  for (clevel = GRPC_COMPRESS_LEVEL_NONE; clevel < GRPC_COMPRESS_LEVEL_COUNT;
       ++clevel) {
    if (grpc_compression_algorithm_for_level(clevel) == algorithm) {
      return clevel;
    }
  }
  abort();
  return GRPC_COMPRESS_LEVEL_NONE;
}

void grpc_compression_options_init(grpc_compression_options *opts) {
  opts->enabled_algorithms_bitset = (1u << GRPC_COMPRESS_ALGORITHMS_COUNT) - 1;
  opts->default_compression_algorithm = GRPC_COMPRESS_NONE;
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
