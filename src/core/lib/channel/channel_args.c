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

#include "src/core/lib/channel/channel_args.h"
#include <grpc/grpc.h>
#include "src/core/lib/support/string.h"

#include <grpc/compression.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
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
      dst.value.pointer.p =
          src->value.pointer.vtable->copy(src->value.pointer.p);
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

static int cmp_arg(const grpc_arg *a, const grpc_arg *b) {
  int c = GPR_ICMP(a->type, b->type);
  if (c != 0) return c;
  c = strcmp(a->key, b->key);
  if (c != 0) return c;
  switch (a->type) {
    case GRPC_ARG_STRING:
      return strcmp(a->value.string, b->value.string);
    case GRPC_ARG_INTEGER:
      return GPR_ICMP(a->value.integer, b->value.integer);
    case GRPC_ARG_POINTER:
      c = GPR_ICMP(a->value.pointer.p, b->value.pointer.p);
      if (c != 0) {
        c = GPR_ICMP(a->value.pointer.vtable, b->value.pointer.vtable);
        if (c == 0) {
          c = a->value.pointer.vtable->cmp(a->value.pointer.p,
                                           b->value.pointer.p);
        }
      }
      return c;
  }
  GPR_UNREACHABLE_CODE(return 0);
}

/* stabilizing comparison function: since channel_args ordering matters for
 * keys with the same name, we need to preserve that ordering */
static int cmp_key_stable(const void *ap, const void *bp) {
  const grpc_arg *const *a = ap;
  const grpc_arg *const *b = bp;
  int c = strcmp((*a)->key, (*b)->key);
  if (c == 0) c = GPR_ICMP(*a, *b);
  return c;
}

grpc_channel_args *grpc_channel_args_normalize(const grpc_channel_args *a) {
  grpc_arg **args = gpr_malloc(sizeof(grpc_arg *) * a->num_args);
  for (size_t i = 0; i < a->num_args; i++) {
    args[i] = &a->args[i];
  }
  if (a->num_args > 1)
    qsort(args, a->num_args, sizeof(grpc_arg *), cmp_key_stable);

  grpc_channel_args *b = gpr_malloc(sizeof(grpc_channel_args));
  b->num_args = a->num_args;
  b->args = gpr_malloc(sizeof(grpc_arg) * b->num_args);
  for (size_t i = 0; i < a->num_args; i++) {
    b->args[i] = copy_arg(args[i]);
  }

  gpr_free(args);
  return b;
}

void grpc_channel_args_destroy(grpc_channel_args *a) {
  size_t i;
  if (!a) return;
  for (i = 0; i < a->num_args; i++) {
    switch (a->args[i].type) {
      case GRPC_ARG_STRING:
        gpr_free(a->args[i].value.string);
        break;
      case GRPC_ARG_INTEGER:
        break;
      case GRPC_ARG_POINTER:
        a->args[i].value.pointer.vtable->destroy(a->args[i].value.pointer.p);
        break;
    }
    gpr_free(a->args[i].key);
  }
  gpr_free(a->args);
  gpr_free(a);
}

grpc_compression_algorithm grpc_channel_args_get_compression_algorithm(
    const grpc_channel_args *a) {
  size_t i;
  if (a == NULL) return 0;
  for (i = 0; i < a->num_args; ++i) {
    if (a->args[i].type == GRPC_ARG_INTEGER &&
        !strcmp(GRPC_COMPRESSION_CHANNEL_DEFAULT_ALGORITHM, a->args[i].key)) {
      return (grpc_compression_algorithm)a->args[i].value.integer;
      break;
    }
  }
  return GRPC_COMPRESS_NONE;
}

grpc_channel_args *grpc_channel_args_set_compression_algorithm(
    grpc_channel_args *a, grpc_compression_algorithm algorithm) {
  GPR_ASSERT(algorithm < GRPC_COMPRESS_ALGORITHMS_COUNT);
  grpc_arg tmp;
  tmp.type = GRPC_ARG_INTEGER;
  tmp.key = GRPC_COMPRESSION_CHANNEL_DEFAULT_ALGORITHM;
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
          !strcmp(GRPC_COMPRESSION_CHANNEL_ENABLED_ALGORITHMS_BITSET,
                  a->args[i].key)) {
        *states_arg = &a->args[i].value.integer;
        **states_arg |= 0x1; /* forcefully enable support for no compression */
        return 1;
      }
    }
  }
  return 0; /* GPR_FALSE */
}

grpc_channel_args *grpc_channel_args_compression_algorithm_set_state(
    grpc_channel_args **a, grpc_compression_algorithm algorithm, int state) {
  int *states_arg = NULL;
  grpc_channel_args *result = *a;
  const int states_arg_found =
      find_compression_algorithm_states_bitset(*a, &states_arg);

  if (grpc_channel_args_get_compression_algorithm(*a) == algorithm &&
      state == 0) {
    char *algo_name = NULL;
    GPR_ASSERT(grpc_compression_algorithm_name(algorithm, &algo_name) != 0);
    gpr_log(GPR_ERROR,
            "Tried to disable default compression algorithm '%s'. The "
            "operation has been ignored.",
            algo_name);
  } else if (states_arg_found) {
    if (state != 0) {
      GPR_BITSET((unsigned *)states_arg, algorithm);
    } else if (algorithm != GRPC_COMPRESS_NONE) {
      GPR_BITCLEAR((unsigned *)states_arg, algorithm);
    }
  } else {
    /* create a new arg */
    grpc_arg tmp;
    tmp.type = GRPC_ARG_INTEGER;
    tmp.key = GRPC_COMPRESSION_CHANNEL_ENABLED_ALGORITHMS_BITSET;
    /* all enabled by default */
    tmp.value.integer = (1u << GRPC_COMPRESS_ALGORITHMS_COUNT) - 1;
    if (state != 0) {
      GPR_BITSET((unsigned *)&tmp.value.integer, algorithm);
    } else if (algorithm != GRPC_COMPRESS_NONE) {
      GPR_BITCLEAR((unsigned *)&tmp.value.integer, algorithm);
    }
    result = grpc_channel_args_copy_and_add(*a, &tmp, 1);
    grpc_channel_args_destroy(*a);
    *a = result;
  }
  return result;
}

uint32_t grpc_channel_args_compression_algorithm_get_states(
    const grpc_channel_args *a) {
  int *states_arg;
  if (find_compression_algorithm_states_bitset(a, &states_arg)) {
    return (uint32_t)*states_arg;
  } else {
    return (1u << GRPC_COMPRESS_ALGORITHMS_COUNT) - 1; /* All algs. enabled */
  }
}

int grpc_channel_args_compare(const grpc_channel_args *a,
                              const grpc_channel_args *b) {
  int c = GPR_ICMP(a->num_args, b->num_args);
  if (c != 0) return c;
  for (size_t i = 0; i < a->num_args; i++) {
    c = cmp_arg(&a->args[i], &b->args[i]);
    if (c != 0) return c;
  }
  return 0;
}

int grpc_channel_arg_get_integer(grpc_arg *arg, grpc_integer_options options) {
  if (arg->type != GRPC_ARG_INTEGER) {
    gpr_log(GPR_ERROR, "%s ignored: it must be an integer", arg->key);
    return options.default_value;
  }
  if (arg->value.integer < options.min_value) {
    gpr_log(GPR_ERROR, "%s ignored: it must be >= %d", arg->key,
            options.min_value);
    return options.default_value;
  }
  if (arg->value.integer > options.max_value) {
    gpr_log(GPR_ERROR, "%s ignored: it must be <= %d", arg->key,
            options.max_value);
    return options.default_value;
  }
  return arg->value.integer;
}
