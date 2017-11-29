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

#include <grpc/grpc.h>
#include <string.h>

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>
#include "test/core/util/test_config.h"

static char* g_last_log_error_message = nullptr;
static const char* g_file_name = "channel.cc";

static int ends_with(const char* src, const char* suffix) {
  size_t src_len = strlen(src);
  size_t suffix_len = strlen(suffix);
  if (src_len < suffix_len) {
    return 0;
  }
  return strcmp(src + src_len - suffix_len, suffix) == 0;
}

static void log_error_sink(gpr_log_func_args* args) {
  if (args->severity == GPR_LOG_SEVERITY_ERROR &&
      ends_with(args->file, g_file_name)) {
    g_last_log_error_message = gpr_strdup(args->message);
  }
}

static void verify_last_error(const char* message) {
  if (message == nullptr) {
    GPR_ASSERT(g_last_log_error_message == nullptr);
    return;
  }
  GPR_ASSERT(strcmp(message, g_last_log_error_message) == 0);
  gpr_free(g_last_log_error_message);
  g_last_log_error_message = nullptr;
}

static char* compose_error_string(const char* key, const char* message) {
  char* ret;
  gpr_asprintf(&ret, "%s%s", key, message);
  return ret;
}

static void one_test(grpc_channel_args* args, char* expected_error_message) {
  grpc_channel* chan =
      grpc_insecure_channel_create("nonexistant:54321", args, nullptr);
  verify_last_error(expected_error_message);
  gpr_free(expected_error_message);
  grpc_channel_destroy(chan);
}

static void test_no_error_message(void) { one_test(nullptr, nullptr); }

static void test_default_authority_type(void) {
  grpc_arg client_arg;
  grpc_channel_args client_args;
  char* expected_error_message;

  client_arg.type = GRPC_ARG_INTEGER;
  client_arg.key = const_cast<char*>(GRPC_ARG_DEFAULT_AUTHORITY);
  client_arg.value.integer = 0;

  client_args.num_args = 1;
  client_args.args = &client_arg;
  expected_error_message = compose_error_string(
      GRPC_ARG_DEFAULT_AUTHORITY, " ignored: it must be a string");
  one_test(&client_args, expected_error_message);
}

static void test_ssl_name_override_type(void) {
  grpc_arg client_arg;
  grpc_channel_args client_args;
  char* expected_error_message;

  client_arg.type = GRPC_ARG_INTEGER;
  client_arg.key = const_cast<char*>(GRPC_SSL_TARGET_NAME_OVERRIDE_ARG);
  client_arg.value.integer = 0;

  client_args.num_args = 1;
  client_args.args = &client_arg;
  expected_error_message = compose_error_string(
      GRPC_SSL_TARGET_NAME_OVERRIDE_ARG, " ignored: it must be a string");
  one_test(&client_args, expected_error_message);
}

static void test_ssl_name_override_failed(void) {
  grpc_arg client_arg[2];
  grpc_channel_args client_args;
  char* expected_error_message;

  client_arg[0].type = GRPC_ARG_STRING;
  client_arg[0].key = const_cast<char*>(GRPC_ARG_DEFAULT_AUTHORITY);
  client_arg[0].value.string = const_cast<char*>("default");
  client_arg[1].type = GRPC_ARG_STRING;
  client_arg[1].key = const_cast<char*>(GRPC_SSL_TARGET_NAME_OVERRIDE_ARG);
  client_arg[1].value.string = const_cast<char*>("ssl");

  client_args.num_args = 2;
  client_args.args = client_arg;
  expected_error_message =
      compose_error_string(GRPC_SSL_TARGET_NAME_OVERRIDE_ARG,
                           " ignored: default host already set some other way");
  one_test(&client_args, expected_error_message);
}

int main(int argc, char** argv) {
  grpc_test_init(argc, argv);
  grpc_init();
  gpr_set_log_function(log_error_sink);

  test_no_error_message();
  test_default_authority_type();
  test_ssl_name_override_type();
  test_ssl_name_override_failed();

  grpc_shutdown();

  return 0;
}
