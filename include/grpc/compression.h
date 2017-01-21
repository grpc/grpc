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

#include <grpc/impl/codegen/port_platform.h>

#include <grpc/impl/codegen/compression_types.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Parses the first \a name_length bytes of \a name as a
 * grpc_compression_algorithm instance, updating \a algorithm. Returns 1 upon
 * success, 0 otherwise. */
GRPCAPI int grpc_compression_algorithm_parse(
    const char *name, size_t name_length,
    grpc_compression_algorithm *algorithm);

/** Updates \a name with the encoding name corresponding to a valid \a
 * algorithm. Note that \a name is statically allocated and must *not* be freed.
 * Returns 1 upon success, 0 otherwise. */
GRPCAPI int grpc_compression_algorithm_name(
    grpc_compression_algorithm algorithm, char **name);

/** Returns the compression algorithm corresponding to \a level for the
 * compression algorithms encoded in the \a accepted_encodings bitset.
 *
 * It abort()s for unknown levels . */
GRPCAPI grpc_compression_algorithm grpc_compression_algorithm_for_level(
    grpc_compression_level level, uint32_t accepted_encodings);

GRPCAPI void grpc_compression_options_init(grpc_compression_options *opts);

/** Mark \a algorithm as enabled in \a opts. */
GRPCAPI void grpc_compression_options_enable_algorithm(
    grpc_compression_options *opts, grpc_compression_algorithm algorithm);

/** Mark \a algorithm as disabled in \a opts. */
GRPCAPI void grpc_compression_options_disable_algorithm(
    grpc_compression_options *opts, grpc_compression_algorithm algorithm);

/** Returns true if \a algorithm is marked as enabled in \a opts. */
GRPCAPI int grpc_compression_options_is_algorithm_enabled(
    const grpc_compression_options *opts, grpc_compression_algorithm algorithm);

#ifdef __cplusplus
}
#endif

#endif /* GRPC_COMPRESSION_H */
