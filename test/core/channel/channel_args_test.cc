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
  grpc::testing::TestEnvironment env(argc, argv);
  grpc_init();
  test_create();
  test_channel_create_with_args();
  test_server_create_with_args();
  grpc_shutdown();
  return 0;
}
