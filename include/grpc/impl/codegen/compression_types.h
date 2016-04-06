/*
 *
 * Copyright 2016, Google Inc.
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

#ifndef GRPC_IMPL_CODEGEN_COMPRESSION_TYPES_H
#define GRPC_IMPL_CODEGEN_COMPRESSION_TYPES_H

#include <grpc/impl/codegen/port_platform.h>

#ifdef __cplusplus
extern "C" {
#endif

/** To be used in channel arguments */
#define GRPC_COMPRESSION_ALGORITHM_ARG "grpc.compression_algorithm"
#define GRPC_COMPRESSION_ALGORITHM_STATE_ARG "grpc.compression_algorithm_state"

/* The various compression algorithms supported by GRPC */
typedef enum {
  GRPC_COMPRESS_NONE = 0,
  GRPC_COMPRESS_DEFLATE,
  GRPC_COMPRESS_GZIP,
  /* TODO(ctiller): snappy */
  GRPC_COMPRESS_ALGORITHMS_COUNT
} grpc_compression_algorithm;

typedef enum {
  GRPC_COMPRESS_LEVEL_NONE = 0,
  GRPC_COMPRESS_LEVEL_LOW,
  GRPC_COMPRESS_LEVEL_MED,
  GRPC_COMPRESS_LEVEL_HIGH,
  GRPC_COMPRESS_LEVEL_COUNT
} grpc_compression_level;

typedef struct grpc_compression_options {
  uint32_t enabled_algorithms_bitset; /**< All algs are enabled by default */
  grpc_compression_algorithm default_compression_algorithm; /**< for channel */
} grpc_compression_options;

#ifdef __cplusplus
}
#endif

#endif /* GRPC_IMPL_CODEGEN_COMPRESSION_TYPES_H */
