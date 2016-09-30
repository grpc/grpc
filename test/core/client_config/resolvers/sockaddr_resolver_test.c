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

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>

#include "src/core/ext/client_config/method_config.h"
#include "src/core/ext/client_config/resolver_registry.h"
#include "src/core/ext/client_config/resolver_result.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/transport/metadata.h"

#include "test/core/util/test_config.h"

typedef struct on_resolution_arg {
  char *expected_server_name;
  const char *expected_method_name;
  bool expected_wait_for_ready;
  gpr_timespec expected_timeout;
  int32_t expected_max_request_message_bytes;
  int32_t expected_max_response_message_bytes;
  grpc_resolver_result *resolver_result;
} on_resolution_arg;

void on_resolution_cb(grpc_exec_ctx *exec_ctx, void *arg, grpc_error *error) {
  on_resolution_arg *res = arg;
  const char *server_name =
      grpc_resolver_result_get_server_name(res->resolver_result);
  GPR_ASSERT(strcmp(res->expected_server_name, server_name) == 0);
  const grpc_channel_args *lb_policy_args =
      grpc_resolver_result_get_lb_policy_args(res->resolver_result);
  if (res->expected_method_name == NULL) {
    GPR_ASSERT(lb_policy_args == NULL);
  } else {
    const grpc_arg *channel_arg =
        grpc_channel_args_find(lb_policy_args, GRPC_ARG_SERVICE_CONFIG);
    GPR_ASSERT(channel_arg != NULL);
    GPR_ASSERT(channel_arg->type == GRPC_ARG_POINTER);
    grpc_method_config_table *method_config_table =
        (grpc_method_config_table *)channel_arg->value.pointer.p;
    GPR_ASSERT(method_config_table != NULL);
    grpc_mdstr *path = grpc_mdstr_from_string(res->expected_method_name);
    grpc_method_config *method_config =
        grpc_method_config_table_get_method_config(method_config_table, path);
    GRPC_MDSTR_UNREF(path);
    GPR_ASSERT(method_config != NULL);
    bool *wait_for_ready = grpc_method_config_get_wait_for_ready(method_config);
    GPR_ASSERT(wait_for_ready != NULL);
    GPR_ASSERT(*wait_for_ready == res->expected_wait_for_ready);
    gpr_timespec *timeout = grpc_method_config_get_timeout(method_config);
    GPR_ASSERT(timeout != NULL);
    GPR_ASSERT(gpr_time_cmp(*timeout, res->expected_timeout) == 0);
    int32_t *max_request_message_bytes =
        grpc_method_config_get_max_request_message_bytes(method_config);
    GPR_ASSERT(max_request_message_bytes != NULL);
    GPR_ASSERT(*max_request_message_bytes ==
               res->expected_max_request_message_bytes);
    int32_t *max_response_message_bytes =
        grpc_method_config_get_max_response_message_bytes(method_config);
    GPR_ASSERT(max_response_message_bytes != NULL);
    GPR_ASSERT(*max_response_message_bytes ==
               res->expected_max_response_message_bytes);
  }
  grpc_resolver_result_unref(exec_ctx, res->resolver_result);
}

static void test_succeeds(grpc_resolver_factory *factory, const char *string) {
  grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
  grpc_uri *uri = grpc_uri_parse(string, 0);
  grpc_resolver_args args;
  grpc_resolver *resolver;
  gpr_log(GPR_DEBUG, "test: '%s' should be valid for '%s'", string,
          factory->vtable->scheme);
  GPR_ASSERT(uri);
  memset(&args, 0, sizeof(args));
  args.uri = uri;
  resolver = grpc_resolver_factory_create_resolver(factory, &args);
  GPR_ASSERT(resolver != NULL);
  on_resolution_arg on_res_arg;
  memset(&on_res_arg, 0, sizeof(on_res_arg));
  on_res_arg.expected_server_name = uri->path;
  grpc_closure *on_resolution =
      grpc_closure_create(on_resolution_cb, &on_res_arg);
  grpc_resolver_next(&exec_ctx, resolver, &on_res_arg.resolver_result,
                     on_resolution);
  GRPC_RESOLVER_UNREF(&exec_ctx, resolver, "test_succeeds");
  grpc_exec_ctx_finish(&exec_ctx);
  grpc_uri_destroy(uri);
}

static void test_succeeds_with_service_config(
    grpc_resolver_factory *factory, const char *string, const char *method_name,
    bool wait_for_ready, gpr_timespec timeout,
    int32_t max_request_message_bytes, int32_t max_response_message_bytes) {
  grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
  grpc_uri *uri = grpc_uri_parse(string, 0);
  grpc_resolver_args args;
  grpc_resolver *resolver;
  gpr_log(GPR_DEBUG, "test: '%s' should be valid for '%s'", string,
          factory->vtable->scheme);
  GPR_ASSERT(uri);
  memset(&args, 0, sizeof(args));
  args.uri = uri;
  resolver = grpc_resolver_factory_create_resolver(factory, &args);
  GPR_ASSERT(resolver != NULL);
  on_resolution_arg on_res_arg;
  memset(&on_res_arg, 0, sizeof(on_res_arg));
  on_res_arg.expected_server_name = uri->path;
  on_res_arg.expected_method_name = method_name;
  on_res_arg.expected_wait_for_ready = wait_for_ready;
  on_res_arg.expected_timeout = timeout;
  on_res_arg.expected_max_request_message_bytes = max_request_message_bytes;
  on_res_arg.expected_max_response_message_bytes = max_response_message_bytes;
  grpc_closure *on_resolution =
      grpc_closure_create(on_resolution_cb, &on_res_arg);
  grpc_resolver_next(&exec_ctx, resolver, &on_res_arg.resolver_result,
                     on_resolution);
  GRPC_RESOLVER_UNREF(&exec_ctx, resolver, "test_succeeds");
  grpc_exec_ctx_finish(&exec_ctx);
  grpc_uri_destroy(uri);
}

static void test_fails(grpc_resolver_factory *factory, const char *string) {
  grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
  grpc_uri *uri = grpc_uri_parse(string, 0);
  grpc_resolver_args args;
  grpc_resolver *resolver;
  gpr_log(GPR_DEBUG, "test: '%s' should be invalid for '%s'", string,
          factory->vtable->scheme);
  GPR_ASSERT(uri);
  memset(&args, 0, sizeof(args));
  args.uri = uri;
  resolver = grpc_resolver_factory_create_resolver(factory, &args);
  GPR_ASSERT(resolver == NULL);
  grpc_uri_destroy(uri);
  grpc_exec_ctx_finish(&exec_ctx);
}

int main(int argc, char **argv) {
  grpc_resolver_factory *ipv4, *ipv6;
  grpc_test_init(argc, argv);
  grpc_init();

  ipv4 = grpc_resolver_factory_lookup("ipv4");
  ipv6 = grpc_resolver_factory_lookup("ipv6");

  test_fails(ipv4, "ipv4:10.2.1.1");
  test_succeeds(ipv4, "ipv4:10.2.1.1:1234");
  test_succeeds(ipv4, "ipv4:10.2.1.1:1234,127.0.0.1:4321");
  test_fails(ipv4, "ipv4:10.2.1.1:123456");
  test_fails(ipv4, "ipv4:www.google.com");
  test_fails(ipv4, "ipv4:[");
  test_fails(ipv4, "ipv4://8.8.8.8/8.8.8.8:8888");

  test_fails(ipv6, "ipv6:[");
  test_fails(ipv6, "ipv6:[::]");
  test_succeeds(ipv6, "ipv6:[::]:1234");
  test_fails(ipv6, "ipv6:[::]:123456");
  test_fails(ipv6, "ipv6:www.google.com");

  test_succeeds_with_service_config(
      ipv4,
      "ipv4:127.0.0.1:1234?method_name=/service/method"
      "&wait_for_ready=1"
      "&timeout_seconds=7"
      "&max_request_message_bytes=456"
      "&max_response_message_bytes=789",
      "/service/method", true /* wait_for_ready */,
      (gpr_timespec){7, 0, GPR_CLOCK_MONOTONIC}, 456, 789);

  grpc_resolver_factory_unref(ipv4);
  grpc_resolver_factory_unref(ipv6);
  grpc_shutdown();

  return 0;
}
