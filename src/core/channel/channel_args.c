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

#include <grpc/grpc.h>
#include "src/core/channel/channel_args.h"
#include "src/core/support/string.h"

#include <grpc/support/alloc.h>
#include <grpc/support/string_util.h>
#include <grpc/support/useful.h>

#include <string.h>

static grpc_arg copy_arg(const grpc_arg *src) {
  grpc_arg dst;
  dst.type = src->type;
  dst.key = gpr_strdup(src->key);
  switch (dst.type) {
    case GRPC_ARG_STRING:
      dst.value.string = gpr_strdup(src->value.string);
      break;
    case GRPC_ARG_INTEGER:
      dst.value.integer = src->value.integer;
      break;
    case GRPC_ARG_POINTER:
      dst.value.pointer = src->value.pointer;
      dst.value.pointer.p = src->value.pointer.copy
                                ? src->value.pointer.copy(src->value.pointer.p)
                                : src->value.pointer.p;
      break;
  }
  return dst;
}

grpc_channel_args *grpc_channel_args_copy_and_add(const grpc_channel_args *src,
                                                  const grpc_arg *to_add,
                                                  size_t num_to_add) {
  grpc_channel_args *dst = gpr_malloc(sizeof(grpc_channel_args));
  size_t i;
  size_t src_num_args = (src == NULL) ? 0 : src->num_args;
  if (!src && !to_add) {
    dst->num_args = 0;
    dst->args = NULL;
    return dst;
  }
  dst->num_args = src_num_args + num_to_add;
  dst->args = gpr_malloc(sizeof(grpc_arg) * dst->num_args);
  for (i = 0; i < src_num_args; i++) {
    dst->args[i] = copy_arg(&src->args[i]);
  }
  for (i = 0; i < num_to_add; i++) {
    dst->args[i + src_num_args] = copy_arg(&to_add[i]);
  }
  return dst;
}

grpc_channel_args *grpc_channel_args_copy(const grpc_channel_args *src) {
  return grpc_channel_args_copy_and_add(src, NULL, 0);
}

grpc_channel_args *grpc_channel_args_merge(const grpc_channel_args *a,
                                           const grpc_channel_args *b) {
  return grpc_channel_args_copy_and_add(a, b->args, b->num_args);
}

void grpc_channel_args_destroy(grpc_channel_args *a) {
  size_t i;
  for (i = 0; i < a->num_args; i++) {
    switch (a->args[i].type) {
      case GRPC_ARG_STRING:
        gpr_free(a->args[i].value.string);
        break;
      case GRPC_ARG_INTEGER:
        break;
      case GRPC_ARG_POINTER:
        if (a->args[i].value.pointer.destroy) {
          a->args[i].value.pointer.destroy(a->args[i].value.pointer.p);
        }
        break;
    }
    gpr_free(a->args[i].key);
  }
  gpr_free(a->args);
  gpr_free(a);
}

int grpc_channel_args_is_census_enabled(const grpc_channel_args *a) {
  size_t i;
  if (a == NULL) return 0;
  for (i = 0; i < a->num_args; i++) {
    if (0 == strcmp(a->args[i].key, GRPC_ARG_ENABLE_CENSUS)) {
      return a->args[i].value.integer != 0;
    }
  }
  return 0;
}

grpc_compression_algorithm grpc_channel_args_get_compression_algorithm(
    const grpc_channel_args *a) {
  size_t i;
  if (a == NULL) return 0;
  for (i = 0; i < a->num_args; ++i) {
    if (a->args[i].type == GRPC_ARG_INTEGER &&
        !strcmp(GRPC_COMPRESSION_ALGORITHM_ARG, a->args[i].key)) {
      return (grpc_compression_algorithm)a->args[i].value.integer;
      break;
    }
  }
  return GRPC_COMPRESS_NONE;
}

grpc_channel_args *grpc_channel_args_set_compression_algorithm(
    grpc_channel_args *a, grpc_compression_algorithm algorithm) {
  grpc_arg tmp;
  tmp.type = GRPC_ARG_INTEGER;
  tmp.key = GRPC_COMPRESSION_ALGORITHM_ARG;
  tmp.value.integer = algorithm;
  return grpc_channel_args_copy_and_add(a, &tmp, 1);
}

/** Returns 1 if the argument for compression algorithm's enabled states bitset
 * was found in \a a, returning the arg's value in \a states. Otherwise, returns
 * 0. */
static int find_compression_algorithm_states_bitset(const grpc_channel_args *a,
                                                    int **states_arg) {
  if (a != NULL) {
    size_t i;
    for (i = 0; i < a->num_args; ++i) {
      if (a->args[i].type == GRPC_ARG_INTEGER &&
          !strcmp(GRPC_COMPRESSION_ALGORITHM_STATE_ARG, a->args[i].key)) {
        *states_arg = &a->args[i].value.integer;
        return 1; /* GPR_TRUE */
      }
    }
  }
  return 0; /* GPR_FALSE */
}

grpc_channel_args *grpc_channel_args_compression_algorithm_set_state(
    grpc_channel_args **a, grpc_compression_algorithm algorithm, int state) {
  int *states_arg;
  grpc_channel_args *result = *a;
  const int states_arg_found =
      find_compression_algorithm_states_bitset(*a, &states_arg);

  if (states_arg_found) {
    if (state != 0) {
      GPR_BITSET((unsigned *)states_arg, algorithm);
    } else {
      GPR_BITCLEAR((unsigned *)states_arg, algorithm);
    }
  } else {
    /* create a new arg */
    grpc_arg tmp;
    tmp.type = GRPC_ARG_INTEGER;
    tmp.key = GRPC_COMPRESSION_ALGORITHM_STATE_ARG;
    /* all enabled by default */
    tmp.value.integer = (1u << GRPC_COMPRESS_ALGORITHMS_COUNT) - 1;
    if (state != 0) {
      GPR_BITSET((unsigned *)&tmp.value.integer, algorithm);
    } else {
      GPR_BITCLEAR((unsigned *)&tmp.value.integer, algorithm);
    }
    result = grpc_channel_args_copy_and_add(*a, &tmp, 1);
    grpc_channel_args_destroy(*a);
    *a = result;
  }
  return result;
}

int grpc_channel_args_compression_algorithm_get_states(
    const grpc_channel_args *a) {
  int *states_arg;
  if (find_compression_algorithm_states_bitset(a, &states_arg)) {
    return *states_arg;
  } else {
    return (1u << GRPC_COMPRESS_ALGORITHMS_COUNT) - 1; /* All algs. enabled */
  }
}
