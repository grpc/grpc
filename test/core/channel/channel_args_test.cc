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

#include <grpc/impl/codegen/grpc_types.h>
#include <grpc/impl/codegen/log.h>
#include <string.h>

#include <grpc/support/log.h>

#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/channel/channel_stack.h"
#include "src/core/lib/gpr/useful.h"
#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/lib/surface/channel.h"
#include "test/core/util/test_config.h"

static void test_create(void) {
  grpc_core::ExecCtx exec_ctx;
  grpc_arg to_add[2];
  grpc_channel_args* ch_args;

  to_add[0] =
      grpc_channel_arg_integer_create(const_cast<char*>("int_arg"), 123);
  to_add[1] = grpc_channel_arg_string_create(const_cast<char*>("str key"),
                                             const_cast<char*>("str value"));
  ch_args = grpc_channel_args_copy_and_add(nullptr, to_add, 2);

  GPR_ASSERT(ch_args->num_args == 2);
  GPR_ASSERT(strcmp(ch_args->args[0].key, to_add[0].key) == 0);
  GPR_ASSERT(ch_args->args[0].type == to_add[0].type);
  GPR_ASSERT(ch_args->args[0].value.integer == to_add[0].value.integer);

  GPR_ASSERT(strcmp(ch_args->args[1].key, to_add[1].key) == 0);
  GPR_ASSERT(ch_args->args[1].type == to_add[1].type);
  GPR_ASSERT(strcmp(ch_args->args[1].value.string, to_add[1].value.string) ==
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

  client_a[0] =
      grpc_channel_arg_integer_create(const_cast<char*>("arg_int"), 0);
  client_a[1] = grpc_channel_arg_string_create(
      const_cast<char*>("arg_str"), const_cast<char*>("arg_str_val"));
  // allocated and adds custom pointer arg
  fake_class* fc = static_cast<fake_class*>(gpr_malloc(sizeof(fake_class)));
  fc->foo = 42;
  client_a[2] = grpc_channel_arg_pointer_create(
      const_cast<char*>("arg_pointer"), fc, &fake_pointer_arg_vtable);

  // creates channel
  grpc_channel_args client_args = {GPR_ARRAY_SIZE(client_a), client_a};
  grpc_channel* c =
      grpc_insecure_channel_create("fake_target", &client_args, nullptr);
  // user is can free the memory they allocated here
  gpr_free(fc);
  grpc_channel_destroy(c);
}

grpc_channel_args* mutate_channel_args(const char* target,
                                       grpc_channel_args* old_args,
                                       grpc_channel_stack_type /*type*/) {
  GPR_ASSERT(old_args != nullptr);
  GPR_ASSERT(grpc_channel_args_find(old_args, "arg_int")->value.integer == 0);
  GPR_ASSERT(strcmp(grpc_channel_args_find(old_args, "arg_str")->value.string,
                    "arg_str_val") == 0);
  GPR_ASSERT(
      grpc_channel_args_find(old_args, "arg_pointer")->value.pointer.vtable ==
      &fake_pointer_arg_vtable);

  if (strcmp(target, "no_op_mutator") == 0) {
    return old_args;
  }

  GPR_ASSERT(strcmp(target, "minimal_stack_mutator") == 0);
  const char* args_to_remove[] = {"arg_int", "arg_str", "arg_pointer"};

  grpc_arg no_deadline_filter_arg = grpc_channel_arg_integer_create(
      const_cast<char*>(GRPC_ARG_MINIMAL_STACK), 1);
  grpc_channel_args* new_args = nullptr;
  new_args = grpc_channel_args_copy_and_add_and_remove(
      old_args, args_to_remove, GPR_ARRAY_SIZE(args_to_remove),
      &no_deadline_filter_arg, 1);
  grpc_channel_args_destroy(old_args);
  return new_args;
}

// Minimal stack should not have client_idle filter
static bool channel_has_client_idle_filter(grpc_channel* c) {
  grpc_channel_stack* stack = grpc_channel_get_channel_stack(c);
  for (size_t i = 0; i < stack->count; i++) {
    if (strcmp(grpc_channel_stack_element(stack, i)->filter->name,
               "client_idle") == 0) {
      return true;
    }
  }
  return false;
}

static void test_channel_create_with_global_mutator(void) {
  grpc_channel_args_set_client_channel_creation_mutator(mutate_channel_args);
  // We also add some custom args to make sure the ownership is correct.
  grpc_arg client_a[3];

  client_a[0] =
      grpc_channel_arg_integer_create(const_cast<char*>("arg_int"), 0);
  client_a[1] = grpc_channel_arg_string_create(
      const_cast<char*>("arg_str"), const_cast<char*>("arg_str_val"));
  // allocated and adds custom pointer arg
  fake_class* fc = static_cast<fake_class*>(gpr_malloc(sizeof(fake_class)));
  fc->foo = 42;
  client_a[2] = grpc_channel_arg_pointer_create(
      const_cast<char*>("arg_pointer"), fc, &fake_pointer_arg_vtable);

  // creates channels
  grpc_channel_args client_args = {GPR_ARRAY_SIZE(client_a), client_a};
  grpc_channel* c =
      grpc_insecure_channel_create("no_op_mutator", &client_args, nullptr);
  GPR_ASSERT(channel_has_client_idle_filter(c));
  grpc_channel_destroy(c);

  c = grpc_insecure_channel_create("minimal_stack_mutator", &client_args,
                                   nullptr);
  GPR_ASSERT(channel_has_client_idle_filter(c) == false);
  grpc_channel_destroy(c);

  gpr_free(fc);
  auto mutator = grpc_channel_args_get_client_channel_creation_mutator();
  GPR_ASSERT(mutator == &mutate_channel_args);
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
  // This has to be the last test.
  // TODO(markdroth): re-enable this test once client_idle is re-enabled
  // test_channel_create_with_global_mutator();
  grpc_shutdown();
  return 0;
}
