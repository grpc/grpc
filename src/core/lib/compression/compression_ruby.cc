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

#include <grpc/compression_ruby.h>

#include "src/core/lib/surface/api_trace.h"
#include "src/core/lib/transport/static_metadata.h"

int grpc_compression_algorithm_parse_ruby(
    grpc_slice name, grpc_compression_algorithm* algorithm) {
  if (grpc_slice_eq(name, GRPC_MDSTR_IDENTITY)) {
    *algorithm = GRPC_COMPRESS_NONE;
    return 1;
  } else if (grpc_slice_eq(name, GRPC_MDSTR_DEFLATE)) {
    *algorithm = GRPC_COMPRESS_MESSAGE_DEFLATE;
    return 1;
  } else if (grpc_slice_eq(name, GRPC_MDSTR_GZIP)) {
    *algorithm = GRPC_COMPRESS_MESSAGE_GZIP;
    return 1;
  } else if (grpc_slice_eq(name, GRPC_MDSTR_STREAM_SLASH_GZIP)) {
    *algorithm = GRPC_COMPRESS_STREAM_GZIP;
    return 1;
  } else {
    return 0;
  }
  return 0;
}

int grpc_compression_algorithm_name_ruby(grpc_compression_algorithm algorithm,
                                         const char** name) {
  GRPC_API_TRACE("grpc_compression_algorithm_parse(algorithm=%d, name=%p)", 2,
                 ((int)algorithm, name));
  switch (algorithm) {
    case GRPC_COMPRESS_NONE:
      *name = "identity";
      return 1;
    case GRPC_COMPRESS_MESSAGE_DEFLATE:
      *name = "deflate";
      return 1;
    case GRPC_COMPRESS_MESSAGE_GZIP:
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
