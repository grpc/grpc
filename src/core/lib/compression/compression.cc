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

#include "src/core/lib/compression/algorithm_metadata.h"
#include "src/core/lib/compression/compression_internal.h"
#include "src/core/lib/gpr/useful.h"
#include "src/core/lib/surface/api_trace.h"
#include "src/core/lib/transport/static_metadata.h"

int grpc_compression_algorithm_is_message(
    grpc_compression_algorithm algorithm) {
  return (algorithm >= GRPC_COMPRESS_DEFLATE && algorithm <= GRPC_COMPRESS_GZIP)
             ? 1
             : 0;
}

int grpc_compression_algorithm_is_stream(grpc_compression_algorithm algorithm) {
  return (algorithm == GRPC_COMPRESS_STREAM_GZIP) ? 1 : 0;
}

int grpc_compression_algorithm_parse(grpc_slice name,
                                     grpc_compression_algorithm* algorithm) {
  if (grpc_slice_eq(name, GRPC_MDSTR_IDENTITY)) {
    *algorithm = GRPC_COMPRESS_NONE;
    return 1;
  } else if (grpc_slice_eq(name, GRPC_MDSTR_DEFLATE)) {
    *algorithm = GRPC_COMPRESS_DEFLATE;
    return 1;
  } else if (grpc_slice_eq(name, GRPC_MDSTR_GZIP)) {
    *algorithm = GRPC_COMPRESS_GZIP;
    return 1;
  } else if (grpc_slice_eq(name, GRPC_MDSTR_STREAM_SLASH_GZIP)) {
    *algorithm = GRPC_COMPRESS_STREAM_GZIP;
    return 1;
  } else {
    return 0;
  }
  return 0;
}

int grpc_compression_algorithm_name(grpc_compression_algorithm algorithm,
                                    const char** name) {
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
    case GRPC_COMPRESS_STREAM_GZIP:
      *name = "stream/gzip";
      return 1;
    case GRPC_COMPRESS_ALGORITHMS_COUNT:
      return 0;
  }
  return 0;
}

grpc_compression_algorithm grpc_compression_algorithm_for_level(
    grpc_compression_level level, uint32_t accepted_encodings) {
  grpc_compression_algorithm algo;
  if (level == GRPC_COMPRESS_LEVEL_NONE) {
    return GRPC_COMPRESS_NONE;
  } else if (level <= GRPC_COMPRESS_LEVEL_HIGH) {
    // TODO(mxyan): Design algorithm to select from all algorithms, including
    // stream compression algorithm
    if (!grpc_compression_algorithm_from_message_stream_compression_algorithm(
            &algo,
            grpc_message_compression_algorithm_for_level(
                level,
                grpc_compression_bitset_to_message_bitset(accepted_encodings)),
            static_cast<grpc_stream_compression_algorithm>(0))) {
      gpr_log(GPR_ERROR, "Parse compression level error");
      return GRPC_COMPRESS_NONE;
    }
    return algo;
  } else {
    gpr_log(GPR_ERROR, "Unknown compression level: %d", level);
    return GRPC_COMPRESS_NONE;
  }
}

void grpc_compression_options_init(grpc_compression_options* opts) {
  memset(opts, 0, sizeof(*opts));
  /* all enabled by default */
  opts->enabled_algorithms_bitset = (1u << GRPC_COMPRESS_ALGORITHMS_COUNT) - 1;
}

void grpc_compression_options_enable_algorithm(
    grpc_compression_options* opts, grpc_compression_algorithm algorithm) {
  GPR_BITSET(&opts->enabled_algorithms_bitset, algorithm);
}

void grpc_compression_options_disable_algorithm(
    grpc_compression_options* opts, grpc_compression_algorithm algorithm) {
  GPR_BITCLEAR(&opts->enabled_algorithms_bitset, algorithm);
}

int grpc_compression_options_is_algorithm_enabled(
    const grpc_compression_options* opts,
    grpc_compression_algorithm algorithm) {
  return GPR_BITGET(opts->enabled_algorithms_bitset, algorithm);
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
    case GRPC_COMPRESS_STREAM_GZIP:
      return GRPC_MDSTR_STREAM_SLASH_GZIP;
    case GRPC_COMPRESS_ALGORITHMS_COUNT:
      return grpc_empty_slice();
  }
  return grpc_empty_slice();
}

grpc_compression_algorithm grpc_compression_algorithm_from_slice(
    grpc_slice str) {
  if (grpc_slice_eq(str, GRPC_MDSTR_IDENTITY)) return GRPC_COMPRESS_NONE;
  if (grpc_slice_eq(str, GRPC_MDSTR_DEFLATE)) return GRPC_COMPRESS_DEFLATE;
  if (grpc_slice_eq(str, GRPC_MDSTR_GZIP)) return GRPC_COMPRESS_GZIP;
  if (grpc_slice_eq(str, GRPC_MDSTR_STREAM_SLASH_GZIP))
    return GRPC_COMPRESS_STREAM_GZIP;
  return GRPC_COMPRESS_ALGORITHMS_COUNT;
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
    case GRPC_COMPRESS_STREAM_GZIP:
      return GRPC_MDELEM_GRPC_ENCODING_GZIP;
    default:
      break;
  }
  return GRPC_MDNULL;
}
