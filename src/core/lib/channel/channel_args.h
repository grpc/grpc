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

#ifndef GRPC_CORE_LIB_CHANNEL_CHANNEL_ARGS_H
#define GRPC_CORE_LIB_CHANNEL_CHANNEL_ARGS_H

#include <grpc/compression.h>
#include <grpc/grpc.h>
#include "src/core/lib/iomgr/socket_mutator.h"

// Channel args are intentionally immutable, to avoid the need for locking.

/** Copy the arguments in \a src into a new instance */
grpc_channel_args *grpc_channel_args_copy(const grpc_channel_args *src);

/** Copy the arguments in \a src into a new instance, stably sorting keys */
grpc_channel_args *grpc_channel_args_normalize(const grpc_channel_args *src);

/** Copy the arguments in \a src and append \a to_add. If \a to_add is NULL, it
 * is equivalent to calling \a grpc_channel_args_copy. */
grpc_channel_args *grpc_channel_args_copy_and_add(const grpc_channel_args *src,
                                                  const grpc_arg *to_add,
                                                  size_t num_to_add);

/** Copies the arguments in \a src except for those whose keys are in
    \a to_remove. */
grpc_channel_args *grpc_channel_args_copy_and_remove(
    const grpc_channel_args *src, const char **to_remove, size_t num_to_remove);

/** Copies the arguments from \a src except for those whose keys are in
    \a to_remove and appends the arguments in \a to_add. */
grpc_channel_args *grpc_channel_args_copy_and_add_and_remove(
    const grpc_channel_args *src, const char **to_remove, size_t num_to_remove,
    const grpc_arg *to_add, size_t num_to_add);

/** Concatenate args from \a a and \a b into a new instance */
grpc_channel_args *grpc_channel_args_merge(const grpc_channel_args *a,
                                           const grpc_channel_args *b);

/** Destroy arguments created by \a grpc_channel_args_copy */
void grpc_channel_args_destroy(grpc_exec_ctx *exec_ctx, grpc_channel_args *a);

/** Returns the compression algorithm set in \a a. */
grpc_compression_algorithm grpc_channel_args_get_compression_algorithm(
    const grpc_channel_args *a);

/** Returns a channel arg instance with compression enabled. If \a a is
 * non-NULL, its args are copied. N.B. GRPC_COMPRESS_NONE disables compression
 * for the channel. */
grpc_channel_args *grpc_channel_args_set_compression_algorithm(
    grpc_channel_args *a, grpc_compression_algorithm algorithm);

/** Sets the support for the given compression algorithm. By default, all
 * compression algorithms are enabled. It's an error to disable an algorithm set
 * by grpc_channel_args_set_compression_algorithm.
 *
 * Returns an instance with the updated algorithm states. The \a a pointer is
 * modified to point to the returned instance (which may be different from the
 * input value of \a a). */
grpc_channel_args *grpc_channel_args_compression_algorithm_set_state(
    grpc_exec_ctx *exec_ctx, grpc_channel_args **a,
    grpc_compression_algorithm algorithm, int enabled);

/** Returns the bitset representing the support state (true for enabled, false
 * for disabled) for compression algorithms.
 *
 * The i-th bit of the returned bitset corresponds to the i-th entry in the
 * grpc_compression_algorithm enum. */
uint32_t grpc_channel_args_compression_algorithm_get_states(
    const grpc_channel_args *a);

int grpc_channel_args_compare(const grpc_channel_args *a,
                              const grpc_channel_args *b);

/** Returns a channel arg instance with socket mutator added. The socket mutator
 * will perform its mutate_fd method on all file descriptors used by the
 * channel.
 * If \a a is non-MULL, its args are copied. */
grpc_channel_args *grpc_channel_args_set_socket_mutator(
    grpc_channel_args *a, grpc_socket_mutator *mutator);

/** Returns the value of argument \a name from \a args, or NULL if not found. */
const grpc_arg *grpc_channel_args_find(const grpc_channel_args *args,
                                       const char *name);

bool grpc_channel_args_want_minimal_stack(const grpc_channel_args *args);

typedef struct grpc_integer_options {
  int default_value;  // Return this if value is outside of expected bounds.
  int min_value;
  int max_value;
} grpc_integer_options;

/** Returns the value of \a arg, subject to the contraints in \a options. */
int grpc_channel_arg_get_integer(const grpc_arg *arg,
                                 const grpc_integer_options options);

bool grpc_channel_arg_get_bool(const grpc_arg *arg, bool default_value);

#endif /* GRPC_CORE_LIB_CHANNEL_CHANNEL_ARGS_H */
