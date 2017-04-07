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

static grpc_resolver *build_fake_resolver(
    grpc_exec_ctx *exec_ctx,
    grpc_fake_resolver_response_generator *response_generator) {
  grpc_resolver_factory *factory = grpc_resolver_factory_lookup("test");
  grpc_arg generator_arg =
      grpc_fake_resolver_response_generator_arg(response_generator);
  grpc_resolver_args args;
  memset(&args, 0, sizeof(args));
  grpc_channel_args *channel_args =
      grpc_channel_args_copy_and_add(NULL, &generator_arg, 1);
  args.args = channel_args;
  args.combiner = g_combiner;
  grpc_resolver *resolver =
      grpc_resolver_factory_create_resolver(exec_ctx, factory, &args);
  grpc_resolver_factory_unref(factory);
  grpc_channel_args_destroy(exec_ctx, channel_args);
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

static void test_response_generator() {
  grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
  // Create resolver.
  grpc_fake_resolver_response_generator *response_generator =
      grpc_fake_resolver_response_generator_create();
  grpc_resolver *resolver = build_fake_resolver(&exec_ctx, response_generator);
  GPR_ASSERT(resolver != NULL);

  // Setup expectations.
  const char *uris[] = {"ipv4:10.2.1.1:1234", "ipv4:127.0.0.1:4321"};
  const char *balancer_names[] = {"name1", "name2"};
  const bool is_balancer[] = {true, false};
  grpc_channel_args *results =
      grpc_fake_resolver_response_create(uris, balancer_names, is_balancer, 2);
  on_resolution_arg on_res_arg;
  memset(&on_res_arg, 0, sizeof(on_res_arg));
  on_res_arg.expected_resolver_result = results;
  grpc_closure *on_resolution = grpc_closure_create(
      on_resolution_cb, &on_res_arg, grpc_schedule_on_exec_ctx);

  // Set resolver results and trigger first resolution. on_resolution_cb
  // performs the checks.
  grpc_fake_resolver_response_generator_set_response_locked(
      &exec_ctx, response_generator, results);
  grpc_resolver_next_locked(&exec_ctx, resolver, &on_res_arg.resolver_result,
                            on_resolution);

  // Setup update.
  const char *uris_update[] = {"ipv4:192.168.1.0:31416"};
  const char *balancer_names_update[] = {"name3"};
  const bool is_balancer_update[] = {false};
  grpc_channel_args *results_update = grpc_fake_resolver_response_create(
      uris_update, balancer_names_update, is_balancer_update, 1);

  // Setup expectations for the update.
  on_resolution_arg on_res_arg_update;
  memset(&on_res_arg_update, 0, sizeof(on_res_arg_update));
  on_res_arg_update.expected_resolver_result = results_update;
  on_resolution = grpc_closure_create(on_resolution_cb, &on_res_arg_update,
                                      grpc_schedule_on_exec_ctx);

  // Set updated resolver results and trigger a second resolution.
  grpc_fake_resolver_response_generator_set_response_locked(
      &exec_ctx, response_generator, results_update);
  grpc_resolver_next_locked(&exec_ctx, resolver,
                            &on_res_arg_update.resolver_result, on_resolution);

  grpc_fake_resolver_response_generator_unref(response_generator);
  GRPC_RESOLVER_UNREF(&exec_ctx, resolver, "test_response_generator");
  grpc_exec_ctx_finish(&exec_ctx);
}

int main(int argc, char **argv) {
  grpc_test_init(argc, argv);
  grpc_fake_resolver_init();  // Registers the "test" scheme.
  grpc_init();

  g_combiner = grpc_combiner_create(NULL);

  test_response_generator();
  {
    grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
    GRPC_COMBINER_UNREF(&exec_ctx, g_combiner, "test");
    grpc_exec_ctx_finish(&exec_ctx);
  }
  grpc_shutdown();

  return 0;
}
