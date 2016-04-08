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

#include <grpc/support/log.h>

#include "src/core/ext/client_config/resolver_registry.h"
#include "test/core/util/test_config.h"

static void client_channel_factory_ref(grpc_client_channel_factory *scv) {}
static void client_channel_factory_unref(grpc_exec_ctx *exec_ctx,
                                         grpc_client_channel_factory *scv) {}
static grpc_subchannel *client_channel_factory_create_subchannel(
    grpc_exec_ctx *exec_ctx, grpc_client_channel_factory *factory,
    grpc_subchannel_args *args) {
  GPR_UNREACHABLE_CODE(return NULL);
}

static grpc_channel *client_channel_factory_create_channel(
    grpc_exec_ctx *exec_ctx, grpc_client_channel_factory *cc_factory,
    const char *target, grpc_client_channel_type type,
    grpc_channel_args *args) {
  GPR_UNREACHABLE_CODE(return NULL);
}

static const grpc_client_channel_factory_vtable sc_vtable = {
    client_channel_factory_ref, client_channel_factory_unref,
    client_channel_factory_create_subchannel,
    client_channel_factory_create_channel};

static grpc_client_channel_factory cc_factory = {&sc_vtable};

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
  args.client_channel_factory = &cc_factory;
  resolver = grpc_resolver_factory_create_resolver(factory, &args);
  GPR_ASSERT(resolver != NULL);
  GRPC_RESOLVER_UNREF(&exec_ctx, resolver, "test_succeeds");
  grpc_uri_destroy(uri);
  grpc_exec_ctx_finish(&exec_ctx);
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

  grpc_resolver_factory_unref(ipv4);
  grpc_resolver_factory_unref(ipv6);
  grpc_shutdown();

  return 0;
}
