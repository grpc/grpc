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
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/** To be used as initial metadata key for the request of a concrete compression
 * algorithm */
#define GRPC_COMPRESSION_REQUEST_ALGORITHM_MD_KEY \
  "grpc-internal-encoding-request"

/** To be used in channel arguments */
#define GRPC_COMPRESSION_CHANNEL_DEFAULT_ALGORITHM \
  "grpc.default_compression_algorithm"
#define GRPC_COMPRESSION_CHANNEL_DEFAULT_LEVEL "grpc.default_compression_level"
#define GRPC_COMPRESSION_CHANNEL_ENABLED_ALGORITHMS_BITSET \
  "grpc.compression_enabled_algorithms_bitset"

/* The various compression algorithms supported by gRPC */
typedef enum {
  GRPC_COMPRESS_NONE = 0,
  GRPC_COMPRESS_DEFLATE,
  GRPC_COMPRESS_GZIP,
  /* TODO(ctiller): snappy */
  GRPC_COMPRESS_ALGORITHMS_COUNT
} grpc_compression_algorithm;

/** Compression levels allow a party with knowledge of its peer's accepted
 * encodings to request compression in an abstract way. The level-algorithm
 * mapping is performed internally and depends on the peer's supported
 * compression algorithms. */
typedef enum {
  GRPC_COMPRESS_LEVEL_NONE = 0,
  GRPC_COMPRESS_LEVEL_LOW,
  GRPC_COMPRESS_LEVEL_MED,
  GRPC_COMPRESS_LEVEL_HIGH,
  GRPC_COMPRESS_LEVEL_COUNT
} grpc_compression_level;

typedef struct grpc_compression_options {
  /** All algs are enabled by default. This option corresponds to the channel
   * argument key behind \a GRPC_COMPRESSION_CHANNEL_ENABLED_ALGORITHMS_BITSET
   */
  uint32_t enabled_algorithms_bitset;

  /** The default channel compression level. It'll be used in the absence of
   * call specific settings. This option corresponds to the channel argument key
   * behind \a GRPC_COMPRESSION_CHANNEL_DEFAULT_LEVEL. If present, takes
   * precedence over \a default_algorithm.
   * TODO(dgq): currently only available for server channels. */
  struct {
    bool is_set;
    grpc_compression_level level;
  } default_level;

  /** The default channel compression algorithm. It'll be used in the absence of
   * call specific settings. This option corresponds to the channel argument key
   * behind \a GRPC_COMPRESSION_CHANNEL_DEFAULT_ALGORITHM. */
  struct {
    bool is_set;
    grpc_compression_algorithm algorithm;
  } default_algorithm;

} grpc_compression_options;

#ifdef __cplusplus
}
#endif

#endif /* GRPC_IMPL_CODEGEN_COMPRESSION_TYPES_H */
