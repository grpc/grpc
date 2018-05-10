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

static void test_succeeds(grpc_core::ResolverFactory* factory,
                          const char* string) {
  gpr_log(GPR_DEBUG, "test: '%s' should be valid for '%s'", string,
          factory->scheme());
  grpc_core::ExecCtx exec_ctx;
  grpc_uri* uri = grpc_uri_parse(string, 0);
  GPR_ASSERT(uri);
  grpc_core::ResolverArgs args;
  args.uri = uri;
  args.combiner = g_combiner;
  grpc_core::OrphanablePtr<grpc_core::Resolver> resolver =
      factory->CreateResolver(args);
  GPR_ASSERT(resolver != nullptr);
  grpc_uri_destroy(uri);
}

static void test_fails(grpc_core::ResolverFactory* factory,
                       const char* string) {
  gpr_log(GPR_DEBUG, "test: '%s' should be invalid for '%s'", string,
          factory->scheme());
  grpc_core::ExecCtx exec_ctx;
  grpc_uri* uri = grpc_uri_parse(string, 0);
  GPR_ASSERT(uri);
  grpc_core::ResolverArgs args;
  args.uri = uri;
  args.combiner = g_combiner;
  grpc_core::OrphanablePtr<grpc_core::Resolver> resolver =
      factory->CreateResolver(args);
  GPR_ASSERT(resolver == nullptr);
  grpc_uri_destroy(uri);
}

int main(int argc, char** argv) {
  grpc_test_init(argc, argv);
  grpc_init();

  g_combiner = grpc_combiner_create();

  grpc_core::ResolverFactory* dns =
      grpc_core::ResolverRegistry::LookupResolverFactory("dns");

  test_succeeds(dns, "dns:10.2.1.1");
  test_succeeds(dns, "dns:10.2.1.1:1234");
  test_succeeds(dns, "dns:www.google.com");
  test_succeeds(dns, "dns:///www.google.com");
  if (grpc_resolve_address == grpc_resolve_address_ares) {
    test_succeeds(dns, "dns://8.8.8.8/8.8.8.8:8888");
  } else {
    test_fails(dns, "dns://8.8.8.8/8.8.8.8:8888");
  }

  {
    grpc_core::ExecCtx exec_ctx;
    GRPC_COMBINER_UNREF(g_combiner, "test");
  }
  grpc_shutdown();

  return 0;
}
