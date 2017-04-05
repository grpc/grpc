/*
 *
 * Copyright 2017, Google Inc.
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

#include "src/core/ext/filters/client_channel/lb_policy_factory.h"
#include "src/core/ext/filters/client_channel/parse_address.h"
#include "src/core/ext/filters/client_channel/resolver_registry.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/iomgr/combiner.h"
#include "src/core/lib/security/credentials/fake/fake_credentials.h"

#include "test/core/end2end/fake_resolver.h"
#include "test/core/util/test_config.h"

static grpc_combiner *g_combiner;

typedef struct on_resolution_arg {
  grpc_channel_args *resolver_result;
  grpc_channel_args *expected_resolver_result;
} on_resolution_arg;

static grpc_resolver *build_resolver(grpc_exec_ctx *exec_ctx, grpc_uri *uri,
                                     const grpc_channel_args *channel_args) {
  grpc_resolver_factory *factory = grpc_resolver_factory_lookup("test");
  grpc_resolver_args args;
  memset(&args, 0, sizeof(args));
  args.args = channel_args;
  args.uri = uri;
  args.combiner = g_combiner;
  grpc_resolver *resolver =
      grpc_resolver_factory_create_resolver(exec_ctx, factory, &args);
  grpc_resolver_factory_unref(factory);
  return resolver;
}

void on_resolution_cb(grpc_exec_ctx *exec_ctx, void *arg, grpc_error *error) {
  on_resolution_arg *res = arg;

  const grpc_arg *lb_addresses_arg =
      grpc_channel_args_find(res->resolver_result, GRPC_ARG_LB_ADDRESSES);
  GPR_ASSERT(lb_addresses_arg != NULL &&
             lb_addresses_arg->type == GRPC_ARG_POINTER);
  const grpc_lb_addresses *addresses = lb_addresses_arg->value.pointer.p;

  const grpc_arg *expected_lb_addresses_arg = grpc_channel_args_find(
      res->expected_resolver_result, GRPC_ARG_LB_ADDRESSES);
  GPR_ASSERT(expected_lb_addresses_arg != NULL &&
             expected_lb_addresses_arg->type == GRPC_ARG_POINTER);
  const grpc_lb_addresses *expected_addresses =
      expected_lb_addresses_arg->value.pointer.p;
  GPR_ASSERT(grpc_lb_addresses_cmp(addresses, expected_addresses) == 0);
  grpc_channel_args_destroy(exec_ctx, res->resolver_result);
  grpc_channel_args_destroy(exec_ctx, res->expected_resolver_result);
}

static void test_succeeds() {
  grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
  grpc_uri *uris[] = {grpc_uri_parse(&exec_ctx, "ipv4:10.2.1.1:1234", true),
                      grpc_uri_parse(&exec_ctx, "ipv4:127.0.0.1:4321", true)};
  char *uri_str = NULL;
  gpr_asprintf(&uri_str, "test:%s,%s", uris[0]->path, uris[1]->path);
  grpc_uri *uri = grpc_uri_parse(&exec_ctx, uri_str, 0);
  gpr_free(uri_str);
  GPR_ASSERT(uri);

  grpc_lb_addresses *lb_addrs = grpc_lb_addresses_create(2, NULL);
  for (size_t i = 0; i < GPR_ARRAY_SIZE(uris); ++i) {
    grpc_resolved_address addr;
    GPR_ASSERT(grpc_parse_ipv4(uris[i], &addr));
    char *balancer_name;
    gpr_asprintf(&balancer_name, "name%lu", i + 1);
    grpc_lb_addresses_set_address(lb_addrs, i, addr.addr, addr.len, true,
                                  balancer_name, NULL);
    gpr_free(balancer_name);
    grpc_uri_destroy(uris[i]);
  }

  const grpc_arg expected_options[] = {
      grpc_lb_addresses_create_channel_arg(lb_addrs),
      grpc_fake_resolver_balancer_names_arg("name1,name2"),
      grpc_fake_resolver_lb_enabled_arg()};
  grpc_channel_args *expected_resolver_result = grpc_channel_args_copy_and_add(
      NULL, expected_options, GPR_ARRAY_SIZE(expected_options));
  grpc_lb_addresses_destroy(&exec_ctx, lb_addrs);

  on_resolution_arg on_res_arg;
  memset(&on_res_arg, 0, sizeof(on_res_arg));
  on_res_arg.expected_resolver_result = expected_resolver_result;

  grpc_closure *on_resolution = grpc_closure_create(
      on_resolution_cb, &on_res_arg, grpc_schedule_on_exec_ctx);

  const grpc_arg options[] = {
      grpc_fake_resolver_balancer_names_arg("name1,name2"),
      grpc_fake_resolver_lb_enabled_arg()};
  grpc_channel_args *channel_args =
      grpc_channel_args_copy_and_add(NULL, options, GPR_ARRAY_SIZE(options));
  grpc_resolver *resolver = build_resolver(&exec_ctx, uri, channel_args);
  grpc_channel_args_destroy(&exec_ctx, channel_args);
  grpc_uri_destroy(uri);
  grpc_resolver_next_locked(&exec_ctx, resolver, &on_res_arg.resolver_result,
                            on_resolution);
  GRPC_RESOLVER_UNREF(&exec_ctx, resolver, "test_succeeds");
  grpc_exec_ctx_finish(&exec_ctx);
}

static void test_fails(const char *string) {
  grpc_resolver_factory *factory = grpc_resolver_factory_lookup("test");
  grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
  grpc_uri *uri = grpc_uri_parse(&exec_ctx, string, 0);
  grpc_resolver_args args;
  grpc_resolver *resolver;
  gpr_log(GPR_DEBUG, "test: '%s' should be invalid for '%s'", string,
          factory->vtable->scheme);
  GPR_ASSERT(uri);
  memset(&args, 0, sizeof(args));
  args.uri = uri;
  args.combiner = g_combiner;
  resolver = grpc_resolver_factory_create_resolver(&exec_ctx, factory, &args);
  GPR_ASSERT(resolver == NULL);
  grpc_uri_destroy(uri);
  grpc_resolver_factory_unref(factory);
  grpc_exec_ctx_finish(&exec_ctx);
}

static void test_response_generator() {
  grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
  grpc_uri *uris[] = {grpc_uri_parse(&exec_ctx, "ipv4:10.2.1.1:1234", true),
                      grpc_uri_parse(&exec_ctx, "ipv4:127.0.0.1:4321", true)};
  char *uri_str = NULL;
  gpr_asprintf(&uri_str, "test:%s", uris[0]->path);
  grpc_uri *uri = grpc_uri_parse(&exec_ctx, uri_str, 0);
  gpr_free(uri_str);
  GPR_ASSERT(uri);

  // Create resolver.
  grpc_fake_resolver_response_generator *response_generator =
      grpc_fake_resolver_response_generator_create();
  const grpc_arg options[] = {
      grpc_fake_resolver_balancer_names_arg("name1"),
      grpc_fake_resolver_response_generator_arg(response_generator)};
  grpc_channel_args *channel_args =
      grpc_channel_args_copy_and_add(NULL, options, GPR_ARRAY_SIZE(options));
  grpc_resolver *resolver = build_resolver(&exec_ctx, uri, channel_args);
  grpc_uri_destroy(uri);
  grpc_channel_args_destroy(&exec_ctx, channel_args);
  grpc_fake_resolver_response_generator_unref(response_generator);
  GPR_ASSERT(resolver != NULL);

  // Setup expectations.
  on_resolution_arg on_res_arg;
  memset(&on_res_arg, 0, sizeof(on_res_arg));
  grpc_lb_addresses *lb_addrs = grpc_lb_addresses_create(1, NULL);
  grpc_resolved_address addr;
  GPR_ASSERT(grpc_parse_ipv4(uris[0], &addr));
  grpc_lb_addresses_set_address(lb_addrs, 0, addr.addr, addr.len, false,
                                "name1", NULL);
  grpc_uri_destroy(uris[0]);
  const grpc_arg expected_options[] = {
      grpc_lb_addresses_create_channel_arg(lb_addrs),
      grpc_fake_resolver_balancer_names_arg("name1")};
  grpc_channel_args *expected_resolver_result = grpc_channel_args_copy_and_add(
      NULL, expected_options, GPR_ARRAY_SIZE(expected_options));
  grpc_lb_addresses_destroy(&exec_ctx, lb_addrs);
  on_res_arg.expected_resolver_result = expected_resolver_result;
  grpc_closure *on_resolution = grpc_closure_create(
      on_resolution_cb, &on_res_arg, grpc_schedule_on_exec_ctx);

  // Trigger first resolution. on_resolution_cb performs the checks.
  grpc_resolver_next_locked(&exec_ctx, resolver, &on_res_arg.resolver_result,
                            on_resolution);

  // Setup update.
  gpr_asprintf(&uri_str, "test:%s", uris[1]->path);
  uri = grpc_uri_parse(&exec_ctx, uri_str, 0);
  gpr_free(uri_str);
  GPR_ASSERT(uri);
  const grpc_arg options_update[] = {
      grpc_fake_resolver_balancer_names_arg("name2"),
      grpc_fake_resolver_lb_enabled_arg()};
  grpc_channel_args *channel_args_update = grpc_channel_args_copy_and_add(
      NULL, options_update, GPR_ARRAY_SIZE(options_update));
  grpc_fake_resolver_response_generator_set_response(
      &exec_ctx, response_generator, uri, channel_args_update);
  grpc_channel_args_destroy(&exec_ctx, channel_args_update);
  grpc_uri_destroy(uri);
  grpc_lb_addresses *lb_addrs_update = grpc_lb_addresses_create(1, NULL);
  GPR_ASSERT(grpc_parse_ipv4(uris[1], &addr));
  grpc_lb_addresses_set_address(lb_addrs_update, 0, addr.addr, addr.len, true,
                                "name2", NULL);
  grpc_uri_destroy(uris[1]);

  // Setup expectations for the update.
  const grpc_arg expected_options_update[] = {
      grpc_lb_addresses_create_channel_arg(lb_addrs_update),
      grpc_fake_resolver_balancer_names_arg("name1")};
  grpc_channel_args *expected_resolver_result_update =
      grpc_channel_args_copy_and_add(NULL, expected_options_update,
                                     GPR_ARRAY_SIZE(expected_options_update));
  grpc_lb_addresses_destroy(&exec_ctx, lb_addrs_update);

  on_resolution_arg on_res_arg_update;
  memset(&on_res_arg_update, 0, sizeof(on_res_arg_update));
  on_res_arg_update.expected_resolver_result = expected_resolver_result_update;
  on_resolution = grpc_closure_create(on_resolution_cb, &on_res_arg_update,
                                      grpc_schedule_on_exec_ctx);

  // Trigger a second resolution, which shold return the new results, uris[1]
  // and options_update.
  grpc_resolver_next_locked(&exec_ctx, resolver,
                            &on_res_arg_update.resolver_result, on_resolution);

  GRPC_RESOLVER_UNREF(&exec_ctx, resolver, "test_response_generator");
  grpc_exec_ctx_finish(&exec_ctx);
}

int main(int argc, char **argv) {
  grpc_test_init(argc, argv);
  grpc_fake_resolver_init();  // Registers the "test" scheme.
  grpc_init();

  g_combiner = grpc_combiner_create(NULL);

  test_fails("test:10.2.1.1");
  test_fails("test:10.2.1.1:123456");
  test_fails("test:www.google.com");
  test_fails("test:[");
  test_fails("test://8.8.8.8/8.8.8.8:8888");
  test_succeeds();
  test_response_generator();

  {
    grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
    GRPC_COMBINER_UNREF(&exec_ctx, g_combiner, "test");
    grpc_exec_ctx_finish(&exec_ctx);
  }
  grpc_shutdown();

  return 0;
}
