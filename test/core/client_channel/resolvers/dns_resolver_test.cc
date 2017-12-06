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

#include "src/core/ext/filters/client_channel/resolver/dns/c_ares/grpc_ares_wrapper.h"
#include "src/core/ext/filters/client_channel/resolver_registry.h"
#include "src/core/lib/iomgr/combiner.h"
#include "test/core/util/test_config.h"

static grpc_combiner* g_combiner;

static void test_succeeds(grpc_resolver_factory* factory, const char* string) {
  grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
  grpc_uri* uri = grpc_uri_parse(&exec_ctx, string, 0);
  grpc_resolver_args args;
  grpc_resolver* resolver;
  gpr_log(GPR_DEBUG, "test: '%s' should be valid for '%s'", string,
          factory->vtable->scheme);
  GPR_ASSERT(uri);
  memset(&args, 0, sizeof(args));
  args.uri = uri;
  args.combiner = g_combiner;
  resolver = grpc_resolver_factory_create_resolver(&exec_ctx, factory, &args);
  GPR_ASSERT(resolver != nullptr);
  GRPC_RESOLVER_UNREF(&exec_ctx, resolver, "test_succeeds");
  grpc_uri_destroy(uri);
  grpc_exec_ctx_finish(&exec_ctx);
}

static void test_fails(grpc_resolver_factory* factory, const char* string) {
  grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
  grpc_uri* uri = grpc_uri_parse(&exec_ctx, string, 0);
  grpc_resolver_args args;
  grpc_resolver* resolver;
  gpr_log(GPR_DEBUG, "test: '%s' should be invalid for '%s'", string,
          factory->vtable->scheme);
  GPR_ASSERT(uri);
  memset(&args, 0, sizeof(args));
  args.uri = uri;
  args.combiner = g_combiner;
  resolver = grpc_resolver_factory_create_resolver(&exec_ctx, factory, &args);
  GPR_ASSERT(resolver == nullptr);
  grpc_uri_destroy(uri);
  grpc_exec_ctx_finish(&exec_ctx);
}

int main(int argc, char** argv) {
  grpc_resolver_factory* dns;
  grpc_test_init(argc, argv);
  grpc_init();

  g_combiner = grpc_combiner_create();

  dns = grpc_resolver_factory_lookup("dns");

  test_succeeds(dns, "dns:10.2.1.1");
  test_succeeds(dns, "dns:10.2.1.1:1234");
  test_succeeds(dns, "ipv4:www.google.com");
  if (grpc_resolve_address == grpc_resolve_address_ares) {
    test_succeeds(dns, "ipv4://8.8.8.8/8.8.8.8:8888");
  } else {
    test_fails(dns, "ipv4://8.8.8.8/8.8.8.8:8888");
  }

  grpc_resolver_factory_unref(dns);
  {
    grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
    GRPC_COMBINER_UNREF(&exec_ctx, g_combiner, "test");
    grpc_exec_ctx_finish(&exec_ctx);
  }
  grpc_shutdown();

  return 0;
}
