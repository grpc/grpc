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

#include <grpc/support/port_platform.h>

#include <limits.h>
#include <string.h>

#include <grpc/compression.h>
#include <grpc/grpc.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>

#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/compression/compression_args.h"
#include "src/core/lib/gpr/string.h"
#include "src/core/lib/gpr/useful.h"

grpc_compression_algorithm
grpc_channel_args_get_channel_default_compression_algorithm(
    const grpc_channel_args* a) {
  size_t i;
  if (a == nullptr) return GRPC_COMPRESS_NONE;
  for (i = 0; i < a->num_args; ++i) {
    if (a->args[i].type == GRPC_ARG_INTEGER &&
        !strcmp(GRPC_COMPRESSION_CHANNEL_DEFAULT_ALGORITHM, a->args[i].key)) {
      grpc_compression_algorithm default_algorithm =
          static_cast<grpc_compression_algorithm>(a->args[i].value.integer);
      return default_algorithm < GRPC_COMPRESS_ALGORITHMS_COUNT
                 ? default_algorithm
                 : GRPC_COMPRESS_NONE;
    }
  }
  return GRPC_COMPRESS_NONE;
}

grpc_channel_args* grpc_channel_args_set_channel_default_compression_algorithm(
    grpc_channel_args* a, grpc_compression_algorithm algorithm) {
  GPR_ASSERT(algorithm < GRPC_COMPRESS_ALGORITHMS_COUNT);
  grpc_arg tmp;
  tmp.type = GRPC_ARG_INTEGER;
  tmp.key = (char*)GRPC_COMPRESSION_CHANNEL_DEFAULT_ALGORITHM;
  tmp.value.integer = algorithm;
  return grpc_channel_args_copy_and_add(a, &tmp, 1);
}

/** Returns 1 if the argument for compression algorithm's enabled states bitset
 * was found in \a a, returning the arg's value in \a states. Otherwise, returns
 * 0. */
static int find_compression_algorithm_states_bitset(const grpc_channel_args* a,
                                                    int** states_arg) {
  if (a != nullptr) {
    size_t i;
    for (i = 0; i < a->num_args; ++i) {
      if (a->args[i].type == GRPC_ARG_INTEGER &&
          !strcmp(GRPC_COMPRESSION_CHANNEL_ENABLED_ALGORITHMS_BITSET,
                  a->args[i].key)) {
        *states_arg = &a->args[i].value.integer;
        **states_arg =
            (**states_arg & ((1 << GRPC_COMPRESS_ALGORITHMS_COUNT) - 1)) |
            0x1; /* forcefully enable support for no compression */
        return 1;
      }
    }
  }
  return 0; /* GPR_FALSE */
}

grpc_channel_args* grpc_channel_args_compression_algorithm_set_state(
    grpc_channel_args** a, grpc_compression_algorithm algorithm, int state) {
  int* states_arg = nullptr;
  grpc_channel_args* result = *a;
  const int states_arg_found =
      find_compression_algorithm_states_bitset(*a, &states_arg);

  if (grpc_channel_args_get_channel_default_compression_algorithm(*a) ==
          algorithm &&
      state == 0) {
    const char* algo_name = nullptr;
    GPR_ASSERT(grpc_compression_algorithm_name(algorithm, &algo_name) != 0);
    gpr_log(GPR_ERROR,
            "Tried to disable default compression algorithm '%s'. The "
            "operation has been ignored.",
            algo_name);
  } else if (states_arg_found) {
    if (state != 0) {
      GPR_BITSET((unsigned*)states_arg, algorithm);
    } else if (algorithm != GRPC_COMPRESS_NONE) {
      GPR_BITCLEAR((unsigned*)states_arg, algorithm);
    }
  } else {
    /* create a new arg */
    grpc_arg tmp;
    tmp.type = GRPC_ARG_INTEGER;
    tmp.key = (char*)GRPC_COMPRESSION_CHANNEL_ENABLED_ALGORITHMS_BITSET;
    /* all enabled by default */
    tmp.value.integer = (1u << GRPC_COMPRESS_ALGORITHMS_COUNT) - 1;
    if (state != 0) {
      GPR_BITSET((unsigned*)&tmp.value.integer, algorithm);
    } else if (algorithm != GRPC_COMPRESS_NONE) {
      GPR_BITCLEAR((unsigned*)&tmp.value.integer, algorithm);
    }
    result = grpc_channel_args_copy_and_add(*a, &tmp, 1);
    grpc_channel_args_destroy(*a);
    *a = result;
  }
  return result;
}

uint32_t grpc_channel_args_compression_algorithm_get_states(
    const grpc_channel_args* a) {
  int* states_arg;
  if (find_compression_algorithm_states_bitset(a, &states_arg)) {
    return static_cast<uint32_t>(*states_arg);
  } else {
    return (1u << GRPC_COMPRESS_ALGORITHMS_COUNT) - 1; /* All algs. enabled */
  }
}
