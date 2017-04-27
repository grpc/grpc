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

static grpc_resolver *build_fake_resolver(
    grpc_exec_ctx *exec_ctx, grpc_combiner *combiner,
    grpc_fake_resolver_response_generator *response_generator) {
  grpc_resolver_factory *factory = grpc_resolver_factory_lookup("test");
  grpc_arg generator_arg =
      grpc_fake_resolver_response_generator_arg(response_generator);
  grpc_resolver_args args;
  memset(&args, 0, sizeof(args));
  grpc_channel_args channel_args = {1, &generator_arg};
  args.args = &channel_args;
  args.combiner = combiner;
  grpc_resolver *resolver =
      grpc_resolver_factory_create_resolver(exec_ctx, factory, &args);
  grpc_resolver_factory_unref(factory);
  return resolver;
}

typedef struct on_resolution_arg {
  grpc_channel_args *resolver_result;
  grpc_channel_args *expected_resolver_result;
  bool was_called;
} on_resolution_arg;

void on_resolution_cb(grpc_exec_ctx *exec_ctx, void *arg, grpc_error *error) {
  on_resolution_arg *res = arg;
  res->was_called = true;
  // We only check the addresses channel arg because that's the only one
  // explicitly set by the test via
  // grpc_fake_resolver_response_generator_set_response.
  const grpc_lb_addresses *actual_lb_addresses =
      grpc_lb_addresses_find_channel_arg(res->resolver_result);
  const grpc_lb_addresses *expected_lb_addresses =
      grpc_lb_addresses_find_channel_arg(res->expected_resolver_result);
  GPR_ASSERT(
      grpc_lb_addresses_cmp(actual_lb_addresses, expected_lb_addresses) == 0);
  grpc_channel_args_destroy(exec_ctx, res->resolver_result);
  grpc_channel_args_destroy(exec_ctx, res->expected_resolver_result);
}

static void test_fake_resolver() {
  grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
  grpc_combiner *combiner = grpc_combiner_create(NULL);
  // Create resolver.
  grpc_fake_resolver_response_generator *response_generator =
      grpc_fake_resolver_response_generator_create();
  grpc_resolver *resolver =
      build_fake_resolver(&exec_ctx, combiner, response_generator);
  GPR_ASSERT(resolver != NULL);

  // Setup expectations.
  grpc_uri *uris[] = {grpc_uri_parse(&exec_ctx, "ipv4:10.2.1.1:1234", true),
                      grpc_uri_parse(&exec_ctx, "ipv4:127.0.0.1:4321", true)};
  char *balancer_names[] = {"name1", "name2"};
  const bool is_balancer[] = {true, false};
  grpc_lb_addresses *addresses = grpc_lb_addresses_create(3, NULL);
  for (size_t i = 0; i < GPR_ARRAY_SIZE(uris); ++i) {
    grpc_lb_addresses_set_address_from_uri(
        addresses, i, uris[i], is_balancer[i], balancer_names[i], NULL);
    grpc_uri_destroy(uris[i]);
  }
  const grpc_arg addresses_arg =
      grpc_lb_addresses_create_channel_arg(addresses);
  grpc_channel_args *results =
      grpc_channel_args_copy_and_add(NULL, &addresses_arg, 1);
  grpc_lb_addresses_destroy(&exec_ctx, addresses);
  on_resolution_arg on_res_arg;
  memset(&on_res_arg, 0, sizeof(on_res_arg));
  on_res_arg.expected_resolver_result = results;
  grpc_closure *on_resolution = grpc_closure_create(
      on_resolution_cb, &on_res_arg, grpc_combiner_scheduler(combiner, false));

  // Set resolver results and trigger first resolution. on_resolution_cb
  // performs the checks.
  grpc_fake_resolver_response_generator_set_response(
      &exec_ctx, response_generator, results);
  grpc_resolver_next_locked(&exec_ctx, resolver, &on_res_arg.resolver_result,
                            on_resolution);
  grpc_exec_ctx_flush(&exec_ctx);
  GPR_ASSERT(on_res_arg.was_called);

  // Setup update.
  grpc_uri *uris_update[] = {
      grpc_uri_parse(&exec_ctx, "ipv4:192.168.1.0:31416", true)};
  char *balancer_names_update[] = {"name3"};
  const bool is_balancer_update[] = {false};
  grpc_lb_addresses *addresses_update = grpc_lb_addresses_create(1, NULL);
  for (size_t i = 0; i < GPR_ARRAY_SIZE(uris_update); ++i) {
    grpc_lb_addresses_set_address_from_uri(addresses_update, i, uris_update[i],
                                           is_balancer_update[i],
                                           balancer_names_update[i], NULL);
    grpc_uri_destroy(uris_update[i]);
  }

  grpc_arg addresses_update_arg =
      grpc_lb_addresses_create_channel_arg(addresses_update);
  grpc_channel_args *results_update =
      grpc_channel_args_copy_and_add(NULL, &addresses_update_arg, 1);
  grpc_lb_addresses_destroy(&exec_ctx, addresses_update);

  // Setup expectations for the update.
  on_resolution_arg on_res_arg_update;
  memset(&on_res_arg_update, 0, sizeof(on_res_arg_update));
  on_res_arg_update.expected_resolver_result = results_update;
  on_resolution = grpc_closure_create(on_resolution_cb, &on_res_arg_update,
                                      grpc_combiner_scheduler(combiner, false));

  // Set updated resolver results and trigger a second resolution.
  grpc_fake_resolver_response_generator_set_response(
      &exec_ctx, response_generator, results_update);
  grpc_resolver_next_locked(&exec_ctx, resolver,
                            &on_res_arg_update.resolver_result, on_resolution);
  grpc_exec_ctx_flush(&exec_ctx);
  GPR_ASSERT(on_res_arg.was_called);

  // Requesting a new resolution without re-senting the response shouldn't
  // trigger the resolution callback.
  memset(&on_res_arg, 0, sizeof(on_res_arg));
  grpc_resolver_next_locked(&exec_ctx, resolver, &on_res_arg.resolver_result,
                            on_resolution);
  grpc_exec_ctx_flush(&exec_ctx);
  GPR_ASSERT(!on_res_arg.was_called);

  GRPC_COMBINER_UNREF(&exec_ctx, combiner, "test_fake_resolver");
  GRPC_RESOLVER_UNREF(&exec_ctx, resolver, "test_fake_resolver");
  grpc_exec_ctx_finish(&exec_ctx);
  grpc_fake_resolver_response_generator_unref(response_generator);
}

int main(int argc, char **argv) {
  grpc_test_init(argc, argv);
  grpc_fake_resolver_init();  // Registers the "test" scheme.
  grpc_init();

  test_fake_resolver();

  grpc_shutdown();
  return 0;
}
