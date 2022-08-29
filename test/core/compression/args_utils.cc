// Copyright 2021 gRPC authors.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "test/core/compression/args_utils.h"

#include <string.h>

#include <grpc/compression.h>
#include <grpc/support/log.h>

#include "src/core/lib/compression/compression_internal.h"
#include "src/core/lib/gpr/useful.h"

const grpc_channel_args*
grpc_channel_args_set_channel_default_compression_algorithm(
    const grpc_channel_args* a, grpc_compression_algorithm algorithm) {
  GPR_ASSERT(algorithm < GRPC_COMPRESS_ALGORITHMS_COUNT);
  grpc_arg tmp;
  tmp.type = GRPC_ARG_INTEGER;
  tmp.key = const_cast<char*>(GRPC_COMPRESSION_CHANNEL_DEFAULT_ALGORITHM);
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

const grpc_channel_args* grpc_channel_args_compression_algorithm_set_state(
    const grpc_channel_args** a, grpc_compression_algorithm algorithm,
    int state) {
  int* states_arg = nullptr;
  const grpc_channel_args* result = *a;
  const int states_arg_found =
      find_compression_algorithm_states_bitset(*a, &states_arg);

  if (grpc_core::DefaultCompressionAlgorithmFromChannelArgs(*a) == algorithm &&
      state == 0) {
    const char* algo_name = nullptr;
    GPR_ASSERT(grpc_compression_algorithm_name(algorithm, &algo_name) != 0);
    gpr_log(GPR_ERROR,
            "Tried to disable default compression algorithm '%s'. The "
            "operation has been ignored.",
            algo_name);
  } else if (states_arg_found) {
    if (state != 0) {
      grpc_core::SetBit(reinterpret_cast<unsigned*>(states_arg), algorithm);
    } else if (algorithm != GRPC_COMPRESS_NONE) {
      grpc_core::ClearBit(reinterpret_cast<unsigned*>(states_arg), algorithm);
    }
  } else {
    /* create a new arg */
    grpc_arg tmp;
    tmp.type = GRPC_ARG_INTEGER;
    tmp.key =
        const_cast<char*>(GRPC_COMPRESSION_CHANNEL_ENABLED_ALGORITHMS_BITSET);
    /* all enabled by default */
    tmp.value.integer = (1u << GRPC_COMPRESS_ALGORITHMS_COUNT) - 1;
    if (state != 0) {
      grpc_core::SetBit(reinterpret_cast<unsigned*>(&tmp.value.integer),
                        algorithm);
    } else if (algorithm != GRPC_COMPRESS_NONE) {
      grpc_core::ClearBit(reinterpret_cast<unsigned*>(&tmp.value.integer),
                          algorithm);
    }
    result = grpc_channel_args_copy_and_add(*a, &tmp, 1);
    grpc_channel_args_destroy(*a);
    *a = result;
  }
  return result;
}
