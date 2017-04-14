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

#include "src/core/ext/filters/client_channel/resolver_registry.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/iomgr/combiner.h"

#include "test/core/util/test_config.h"

static grpc_combiner *g_combiner;

typedef struct on_resolution_arg {
  char *expected_server_name;
  grpc_channel_args *resolver_result;
} on_resolution_arg;

void on_resolution_cb(grpc_exec_ctx *exec_ctx, void *arg, grpc_error *error) {
  on_resolution_arg *res = arg;
  grpc_channel_args_destroy(exec_ctx, res->resolver_result);
}

static void test_succeeds(grpc_resolver_factory *factory, const char *string) {
  grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
  grpc_uri *uri = grpc_uri_parse(&exec_ctx, string, 0);
  grpc_resolver_args args;
  grpc_resolver *resolver;
  gpr_log(GPR_DEBUG, "test: '%s' should be valid for '%s'", string,
          factory->vtable->scheme);
  GPR_ASSERT(uri);
  memset(&args, 0, sizeof(args));
  args.uri = uri;
  args.combiner = g_combiner;
  resolver = grpc_resolver_factory_create_resolver(&exec_ctx, factory, &args);
  GPR_ASSERT(resolver != NULL);

  on_resolution_arg on_res_arg;
  memset(&on_res_arg, 0, sizeof(on_res_arg));
  on_res_arg.expected_server_name = uri->path;
  grpc_closure *on_resolution = grpc_closure_create(
      on_resolution_cb, &on_res_arg, grpc_schedule_on_exec_ctx);

  grpc_resolver_next_locked(&exec_ctx, resolver, &on_res_arg.resolver_result,
                            on_resolution);
  GRPC_RESOLVER_UNREF(&exec_ctx, resolver, "test_succeeds");
  grpc_exec_ctx_finish(&exec_ctx);
  grpc_uri_destroy(uri);
}

static void test_fails(grpc_resolver_factory *factory, const char *string) {
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
  grpc_exec_ctx_finish(&exec_ctx);
}

int main(int argc, char **argv) {
  grpc_resolver_factory *ipv4, *ipv6;
  grpc_test_init(argc, argv);
  grpc_init();

  g_combiner = grpc_combiner_create(NULL);

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

  grpc_resolver_factory_unref(ipv4);
  grpc_resolver_factory_unref(ipv6);

  {
    grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
    GRPC_COMBINER_UNREF(&exec_ctx, g_combiner, "test");
    grpc_exec_ctx_finish(&exec_ctx);
  }
  grpc_shutdown();

  return 0;
}
