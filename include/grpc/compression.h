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

#ifndef GRPC_COMPRESSION_H
#define GRPC_COMPRESSION_H

#include <stdlib.h>

#include <grpc/support/port_platform.h>

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
  gpr_uint32 enabled_algorithms_bitset; /**< All algs are enabled by default */
  grpc_compression_algorithm default_compression_algorithm; /**< for channel */
} grpc_compression_options;

/** Parses the first \a name_length bytes of \a name as a
 * grpc_compression_algorithm instance, updating \a algorithm. Returns 1 upon
 * success, 0 otherwise. */
int grpc_compression_algorithm_parse(const char *name, size_t name_length,
                                     grpc_compression_algorithm *algorithm);

/** Updates \a name with the encoding name corresponding to a valid \a
 * algorithm.  Returns 1 upon success, 0 otherwise. */
int grpc_compression_algorithm_name(grpc_compression_algorithm algorithm,
                                    char **name);

/** Returns the compression level corresponding to \a algorithm.
 *
 * It abort()s for unknown algorithms. */
grpc_compression_level grpc_compression_level_for_algorithm(
    grpc_compression_algorithm algorithm);

/** Returns the compression algorithm corresponding to \a level.
 *
 * It abort()s for unknown levels . */
grpc_compression_algorithm grpc_compression_algorithm_for_level(
    grpc_compression_level level);

void grpc_compression_options_init(grpc_compression_options *opts);

/** Mark \a algorithm as enabled in \a opts. */
void grpc_compression_options_enable_algorithm(
    grpc_compression_options *opts, grpc_compression_algorithm algorithm);

/** Mark \a algorithm as disabled in \a opts. */
void grpc_compression_options_disable_algorithm(
    grpc_compression_options *opts, grpc_compression_algorithm algorithm);

/** Returns true if \a algorithm is marked as enabled in \a opts. */
int grpc_compression_options_is_algorithm_enabled(
    const grpc_compression_options *opts, grpc_compression_algorithm algorithm);

#ifdef __cplusplus
}
#endif

#endif /* GRPC_COMPRESSION_H */
