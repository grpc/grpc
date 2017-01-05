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

#include <string.h>

#include <grpc/support/log.h>
#include <grpc/support/useful.h>

#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/iomgr/exec_ctx.h"
#include "test/core/util/test_config.h"

static void test_create(void) {
  grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;

  grpc_arg arg_int;
  grpc_arg arg_string;
  grpc_arg to_add[2];
  grpc_channel_args *ch_args;

  arg_int.key = "int_arg";
  arg_int.type = GRPC_ARG_INTEGER;
  arg_int.value.integer = 123;

  arg_string.key = "str key";
  arg_string.type = GRPC_ARG_STRING;
  arg_string.value.string = "str value";

  to_add[0] = arg_int;
  to_add[1] = arg_string;
  ch_args = grpc_channel_args_copy_and_add(NULL, to_add, 2);

  GPR_ASSERT(ch_args->num_args == 2);
  GPR_ASSERT(strcmp(ch_args->args[0].key, arg_int.key) == 0);
  GPR_ASSERT(ch_args->args[0].type == arg_int.type);
  GPR_ASSERT(ch_args->args[0].value.integer == arg_int.value.integer);

  GPR_ASSERT(strcmp(ch_args->args[1].key, arg_string.key) == 0);
  GPR_ASSERT(ch_args->args[1].type == arg_string.type);
  GPR_ASSERT(strcmp(ch_args->args[1].value.string, arg_string.value.string) ==
             0);

  grpc_channel_args_destroy(&exec_ctx, ch_args);
  grpc_exec_ctx_finish(&exec_ctx);
}

static void test_set_compression_algorithm(void) {
  grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
  grpc_channel_args *ch_args;

  ch_args =
      grpc_channel_args_set_compression_algorithm(NULL, GRPC_COMPRESS_GZIP);
  GPR_ASSERT(ch_args->num_args == 1);
  GPR_ASSERT(strcmp(ch_args->args[0].key,
                    GRPC_COMPRESSION_CHANNEL_DEFAULT_ALGORITHM) == 0);
  GPR_ASSERT(ch_args->args[0].type == GRPC_ARG_INTEGER);

  grpc_channel_args_destroy(&exec_ctx, ch_args);
  grpc_exec_ctx_finish(&exec_ctx);
}

static void test_compression_algorithm_states(void) {
  grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
  grpc_channel_args *ch_args, *ch_args_wo_gzip, *ch_args_wo_gzip_deflate;
  unsigned states_bitset;
  size_t i;

  ch_args = grpc_channel_args_copy_and_add(NULL, NULL, 0);
  /* by default, all enabled */
  states_bitset =
      (unsigned)grpc_channel_args_compression_algorithm_get_states(ch_args);

  for (i = 0; i < GRPC_COMPRESS_ALGORITHMS_COUNT; i++) {
    GPR_ASSERT(GPR_BITGET(states_bitset, i));
  }

  /* disable gzip and deflate */
  ch_args_wo_gzip = grpc_channel_args_compression_algorithm_set_state(
      &exec_ctx, &ch_args, GRPC_COMPRESS_GZIP, 0);
  GPR_ASSERT(ch_args == ch_args_wo_gzip);
  ch_args_wo_gzip_deflate = grpc_channel_args_compression_algorithm_set_state(
      &exec_ctx, &ch_args_wo_gzip, GRPC_COMPRESS_DEFLATE, 0);
  GPR_ASSERT(ch_args_wo_gzip == ch_args_wo_gzip_deflate);

  states_bitset = (unsigned)grpc_channel_args_compression_algorithm_get_states(
      ch_args_wo_gzip_deflate);
  for (i = 0; i < GRPC_COMPRESS_ALGORITHMS_COUNT; i++) {
    if (i == GRPC_COMPRESS_GZIP || i == GRPC_COMPRESS_DEFLATE) {
      GPR_ASSERT(GPR_BITGET(states_bitset, i) == 0);
    } else {
      GPR_ASSERT(GPR_BITGET(states_bitset, i) != 0);
    }
  }

  /* re-enabled gzip only */
  ch_args_wo_gzip = grpc_channel_args_compression_algorithm_set_state(
      &exec_ctx, &ch_args_wo_gzip_deflate, GRPC_COMPRESS_GZIP, 1);
  GPR_ASSERT(ch_args_wo_gzip == ch_args_wo_gzip_deflate);

  states_bitset = (unsigned)grpc_channel_args_compression_algorithm_get_states(
      ch_args_wo_gzip);
  for (i = 0; i < GRPC_COMPRESS_ALGORITHMS_COUNT; i++) {
    if (i == GRPC_COMPRESS_DEFLATE) {
      GPR_ASSERT(GPR_BITGET(states_bitset, i) == 0);
    } else {
      GPR_ASSERT(GPR_BITGET(states_bitset, i) != 0);
    }
  }

  grpc_channel_args_destroy(&exec_ctx, ch_args);
  grpc_exec_ctx_finish(&exec_ctx);
}

static void test_set_socket_mutator(void) {
  grpc_channel_args *ch_args;
  grpc_socket_mutator mutator;
  grpc_socket_mutator_init(&mutator, NULL);

  ch_args = grpc_channel_args_set_socket_mutator(NULL, &mutator);
  GPR_ASSERT(ch_args->num_args == 1);
  GPR_ASSERT(strcmp(ch_args->args[0].key, GRPC_ARG_SOCKET_MUTATOR) == 0);
  GPR_ASSERT(ch_args->args[0].type == GRPC_ARG_POINTER);

  {
    grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
    grpc_channel_args_destroy(&exec_ctx, ch_args);
    grpc_exec_ctx_finish(&exec_ctx);
  }
}

int main(int argc, char **argv) {
  grpc_test_init(argc, argv);
  grpc_init();
  test_create();
  test_set_compression_algorithm();
  test_compression_algorithm_states();
  test_set_socket_mutator();
  grpc_shutdown();
  return 0;
}
