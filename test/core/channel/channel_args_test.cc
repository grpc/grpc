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

#include <string.h>

#include <grpc/support/log.h>

#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/gpr/useful.h"
#include "src/core/lib/iomgr/exec_ctx.h"
#include "test/core/util/test_config.h"

static void test_create(void) {
  grpc_core::ExecCtx exec_ctx;

  grpc_arg arg_int;
  grpc_arg arg_string;
  grpc_arg to_add[2];
  grpc_channel_args* ch_args;

  arg_int.key = const_cast<char*>("int_arg");
  arg_int.type = GRPC_ARG_INTEGER;
  arg_int.value.integer = 123;

  arg_string.key = const_cast<char*>("str key");
  arg_string.type = GRPC_ARG_STRING;
  arg_string.value.string = const_cast<char*>("str value");

  to_add[0] = arg_int;
  to_add[1] = arg_string;
  ch_args = grpc_channel_args_copy_and_add(nullptr, to_add, 2);

  GPR_ASSERT(ch_args->num_args == 2);
  GPR_ASSERT(strcmp(ch_args->args[0].key, arg_int.key) == 0);
  GPR_ASSERT(ch_args->args[0].type == arg_int.type);
  GPR_ASSERT(ch_args->args[0].value.integer == arg_int.value.integer);

  GPR_ASSERT(strcmp(ch_args->args[1].key, arg_string.key) == 0);
  GPR_ASSERT(ch_args->args[1].type == arg_string.type);
  GPR_ASSERT(strcmp(ch_args->args[1].value.string, arg_string.value.string) ==
             0);

  grpc_channel_args_destroy(ch_args);
}

static void test_set_compression_algorithm(void) {
  grpc_core::ExecCtx exec_ctx;
  grpc_channel_args* ch_args;

  ch_args =
      grpc_channel_args_set_compression_algorithm(nullptr, GRPC_COMPRESS_GZIP);
  GPR_ASSERT(ch_args->num_args == 1);
  GPR_ASSERT(strcmp(ch_args->args[0].key,
                    GRPC_COMPRESSION_CHANNEL_DEFAULT_ALGORITHM) == 0);
  GPR_ASSERT(ch_args->args[0].type == GRPC_ARG_INTEGER);

  grpc_channel_args_destroy(ch_args);
}

static void test_compression_algorithm_states(void) {
  grpc_core::ExecCtx exec_ctx;
  grpc_channel_args *ch_args, *ch_args_wo_gzip, *ch_args_wo_gzip_deflate,
      *ch_args_wo_gzip_deflate_gzip;
  unsigned states_bitset;
  size_t i;

  ch_args = grpc_channel_args_copy_and_add(nullptr, nullptr, 0);
  /* by default, all enabled */
  states_bitset = static_cast<unsigned>(
      grpc_channel_args_compression_algorithm_get_states(ch_args));

  for (i = 0; i < GRPC_COMPRESS_ALGORITHMS_COUNT; i++) {
    GPR_ASSERT(GPR_BITGET(states_bitset, i));
  }

  /* disable gzip and deflate and stream/gzip */
  ch_args_wo_gzip = grpc_channel_args_compression_algorithm_set_state(
      &ch_args, GRPC_COMPRESS_GZIP, 0);
  GPR_ASSERT(ch_args == ch_args_wo_gzip);
  ch_args_wo_gzip_deflate = grpc_channel_args_compression_algorithm_set_state(
      &ch_args_wo_gzip, GRPC_COMPRESS_DEFLATE, 0);
  GPR_ASSERT(ch_args_wo_gzip == ch_args_wo_gzip_deflate);
  ch_args_wo_gzip_deflate_gzip =
      grpc_channel_args_compression_algorithm_set_state(
          &ch_args_wo_gzip_deflate, GRPC_COMPRESS_STREAM_GZIP, 0);
  GPR_ASSERT(ch_args_wo_gzip_deflate == ch_args_wo_gzip_deflate_gzip);

  states_bitset =
      static_cast<unsigned>(grpc_channel_args_compression_algorithm_get_states(
          ch_args_wo_gzip_deflate));
  for (i = 0; i < GRPC_COMPRESS_ALGORITHMS_COUNT; i++) {
    if (i == GRPC_COMPRESS_GZIP || i == GRPC_COMPRESS_DEFLATE ||
        i == GRPC_COMPRESS_STREAM_GZIP) {
      GPR_ASSERT(GPR_BITGET(states_bitset, i) == 0);
    } else {
      GPR_ASSERT(GPR_BITGET(states_bitset, i) != 0);
    }
  }

  /* re-enabled gzip and stream/gzip only */
  ch_args_wo_gzip = grpc_channel_args_compression_algorithm_set_state(
      &ch_args_wo_gzip_deflate_gzip, GRPC_COMPRESS_GZIP, 1);
  ch_args_wo_gzip = grpc_channel_args_compression_algorithm_set_state(
      &ch_args_wo_gzip, GRPC_COMPRESS_STREAM_GZIP, 1);
  GPR_ASSERT(ch_args_wo_gzip == ch_args_wo_gzip_deflate_gzip);

  states_bitset = static_cast<unsigned>(
      grpc_channel_args_compression_algorithm_get_states(ch_args_wo_gzip));
  for (i = 0; i < GRPC_COMPRESS_ALGORITHMS_COUNT; i++) {
    if (i == GRPC_COMPRESS_DEFLATE) {
      GPR_ASSERT(GPR_BITGET(states_bitset, i) == 0);
    } else {
      GPR_ASSERT(GPR_BITGET(states_bitset, i) != 0);
    }
  }

  grpc_channel_args_destroy(ch_args);
}

static void test_set_socket_mutator(void) {
  grpc_channel_args* ch_args;
  grpc_socket_mutator mutator;
  grpc_socket_mutator_init(&mutator, nullptr);

  ch_args = grpc_channel_args_set_socket_mutator(nullptr, &mutator);
  GPR_ASSERT(ch_args->num_args == 1);
  GPR_ASSERT(strcmp(ch_args->args[0].key, GRPC_ARG_SOCKET_MUTATOR) == 0);
  GPR_ASSERT(ch_args->args[0].type == GRPC_ARG_POINTER);

  {
    grpc_core::ExecCtx exec_ctx;
    grpc_channel_args_destroy(ch_args);
  }
}

struct fake_class {
  int foo;
};

static void* fake_pointer_arg_copy(void* arg) {
  gpr_log(GPR_DEBUG, "fake_pointer_arg_copy");
  fake_class* fc = static_cast<fake_class*>(arg);
  fake_class* new_fc = static_cast<fake_class*>(gpr_malloc(sizeof(fake_class)));
  new_fc->foo = fc->foo;
  return new_fc;
}

static void fake_pointer_arg_destroy(void* arg) {
  gpr_log(GPR_DEBUG, "fake_pointer_arg_destroy");
  fake_class* fc = static_cast<fake_class*>(arg);
  gpr_free(fc);
}

static int fake_pointer_cmp(void* a, void* b) { return GPR_ICMP(a, b); }

static const grpc_arg_pointer_vtable fake_pointer_arg_vtable = {
    fake_pointer_arg_copy, fake_pointer_arg_destroy, fake_pointer_cmp};

static void test_channel_create_with_args(void) {
  grpc_arg client_a[3];

  // adds integer arg
  client_a[0].type = GRPC_ARG_INTEGER;
  client_a[0].key = const_cast<char*>("arg_int");
  client_a[0].value.integer = 0;

  // adds const str arg
  client_a[1].type = GRPC_ARG_STRING;
  client_a[1].key = const_cast<char*>("arg_str");
  client_a[1].value.string = const_cast<char*>("arg_str_val");

  // allocated and adds custom pointer arg
  fake_class* fc = static_cast<fake_class*>(gpr_malloc(sizeof(fake_class)));
  fc->foo = 42;
  client_a[2].type = GRPC_ARG_POINTER;
  client_a[2].key = const_cast<char*>("arg_pointer");
  client_a[2].value.pointer.vtable = &fake_pointer_arg_vtable;
  client_a[2].value.pointer.p = fc;

  // creates channel
  grpc_channel_args client_args = {GPR_ARRAY_SIZE(client_a), client_a};
  grpc_channel* c =
      grpc_insecure_channel_create("fake_target", &client_args, nullptr);
  // user is can free the memory they allocated here
  gpr_free(fc);
  grpc_channel_destroy(c);
}

static void test_server_create_with_args(void) {
  grpc_arg server_a[3];

  // adds integer arg
  server_a[0].type = GRPC_ARG_INTEGER;
  server_a[0].key = const_cast<char*>("arg_int");
  server_a[0].value.integer = 0;

  // adds const str arg
  server_a[1].type = GRPC_ARG_STRING;
  server_a[1].key = const_cast<char*>("arg_str");
  server_a[1].value.string = const_cast<char*>("arg_str_val");

  // allocated and adds custom pointer arg
  fake_class* fc = static_cast<fake_class*>(gpr_malloc(sizeof(fake_class)));
  fc->foo = 42;
  server_a[2].type = GRPC_ARG_POINTER;
  server_a[2].key = const_cast<char*>("arg_pointer");
  server_a[2].value.pointer.vtable = &fake_pointer_arg_vtable;
  server_a[2].value.pointer.p = fc;

  // creates server
  grpc_channel_args server_args = {GPR_ARRAY_SIZE(server_a), server_a};
  grpc_server* s = grpc_server_create(&server_args, nullptr);
  // user is can free the memory they allocated here
  gpr_free(fc);
  grpc_server_destroy(s);
}

int main(int argc, char** argv) {
  grpc_test_init(argc, argv);
  grpc_init();
  test_create();
  test_set_compression_algorithm();
  test_compression_algorithm_states();
  test_set_socket_mutator();
  test_channel_create_with_args();
  test_server_create_with_args();
  grpc_shutdown();
  return 0;
}
