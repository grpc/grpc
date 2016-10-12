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
#include <string.h>

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>
#include "test/core/util/test_config.h"

static char *g_last_log_error_message = NULL;
static const char *g_file_name = "channel.c";

static int ends_with(const char *src, const char *suffix) {
  size_t src_len = strlen(src);
  size_t suffix_len = strlen(suffix);
  if (src_len < suffix_len) {
    return 0;
  }
  return strcmp(src + src_len - suffix_len, suffix) == 0;
}

static void log_error_sink(gpr_log_func_args *args) {
  if (args->severity == GPR_LOG_SEVERITY_ERROR &&
      ends_with(args->file, g_file_name)) {
    g_last_log_error_message = gpr_strdup(args->message);
  }
}

static void verify_last_error(const char *message) {
  if (message == NULL) {
    GPR_ASSERT(g_last_log_error_message == NULL);
    return;
  }
  GPR_ASSERT(strcmp(message, g_last_log_error_message) == 0);
  gpr_free(g_last_log_error_message);
  g_last_log_error_message = NULL;
}

static char *compose_error_string(const char *key, const char *message) {
  char *ret;
  gpr_asprintf(&ret, "%s%s", key, message);
  return ret;
}

static void one_test(grpc_channel_args *args, char *expected_error_message) {
  grpc_channel *chan =
      grpc_insecure_channel_create("nonexistant:54321", args, NULL);
  verify_last_error(expected_error_message);
  gpr_free(expected_error_message);
  grpc_channel_destroy(chan);
}

static void test_no_error_message(void) { one_test(NULL, NULL); }

static void test_default_authority_type(void) {
  grpc_arg client_arg;
  grpc_channel_args client_args;
  char *expected_error_message;

  client_arg.type = GRPC_ARG_INTEGER;
  client_arg.key = GRPC_ARG_DEFAULT_AUTHORITY;
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
  char *expected_error_message;

  client_arg.type = GRPC_ARG_INTEGER;
  client_arg.key = GRPC_SSL_TARGET_NAME_OVERRIDE_ARG;
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
  char *expected_error_message;

  client_arg[0].type = GRPC_ARG_STRING;
  client_arg[0].key = GRPC_ARG_DEFAULT_AUTHORITY;
  client_arg[0].value.string = "default";
  client_arg[1].type = GRPC_ARG_STRING;
  client_arg[1].key = GRPC_SSL_TARGET_NAME_OVERRIDE_ARG;
  client_arg[1].value.string = "ssl";

  client_args.num_args = 2;
  client_args.args = client_arg;
  expected_error_message =
      compose_error_string(GRPC_SSL_TARGET_NAME_OVERRIDE_ARG,
                           " ignored: default host already set some other way");
  one_test(&client_args, expected_error_message);
}

int main(int argc, char **argv) {
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
