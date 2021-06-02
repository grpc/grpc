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
#include "src/core/ext/filters/client_channel/resolver/dns/dns_resolver_selection.h"
#include "src/core/ext/filters/client_channel/resolver_registry.h"
#include "src/core/lib/gpr/string.h"
#include "src/core/lib/gprpp/memory.h"
#include "src/core/lib/iomgr/work_serializer.h"
#include "test/core/util/test_config.h"

static std::shared_ptr<grpc_core::WorkSerializer>* g_work_serializer;

class TestResultHandler : public grpc_core::Resolver::ResultHandler {
  void ReturnResult(grpc_core::Resolver::Result /*result*/) override {}
  void ReturnError(grpc_error_handle /*error*/) override {}
};

static void test_succeeds(grpc_core::ResolverFactory* factory,
                          const char* string) {
  gpr_log(GPR_DEBUG, "test: '%s' should be valid for '%s'", string,
          factory->scheme());
  grpc_core::ExecCtx exec_ctx;
  absl::StatusOr<grpc_core::URI> uri = grpc_core::URI::Parse(string);
  if (!uri.ok()) {
    gpr_log(GPR_ERROR, "%s", uri.status().ToString().c_str());
    GPR_ASSERT(uri.ok());
  }
  grpc_core::ResolverArgs args;
  args.uri = std::move(*uri);
  args.work_serializer = *g_work_serializer;
  args.result_handler = absl::make_unique<TestResultHandler>();
  grpc_core::OrphanablePtr<grpc_core::Resolver> resolver =
      factory->CreateResolver(std::move(args));
  GPR_ASSERT(resolver != nullptr);
}

static void test_fails(grpc_core::ResolverFactory* factory,
                       const char* string) {
  gpr_log(GPR_DEBUG, "test: '%s' should be invalid for '%s'", string,
          factory->scheme());
  grpc_core::ExecCtx exec_ctx;
  absl::StatusOr<grpc_core::URI> uri = grpc_core::URI::Parse(string);
  if (!uri.ok()) {
    gpr_log(GPR_ERROR, "%s", uri.status().ToString().c_str());
    GPR_ASSERT(uri.ok());
  }
  grpc_core::ResolverArgs args;
  args.uri = std::move(*uri);
  args.work_serializer = *g_work_serializer;
  args.result_handler = absl::make_unique<TestResultHandler>();
  grpc_core::OrphanablePtr<grpc_core::Resolver> resolver =
      factory->CreateResolver(std::move(args));
  GPR_ASSERT(resolver == nullptr);
}

int main(int argc, char** argv) {
  grpc::testing::TestEnvironment env(argc, argv);
  grpc_init();

  auto work_serializer = std::make_shared<grpc_core::WorkSerializer>();
  g_work_serializer = &work_serializer;

  grpc_core::ResolverFactory* dns =
      grpc_core::ResolverRegistry::LookupResolverFactory("dns");

  test_succeeds(dns, "dns:10.2.1.1");
  test_succeeds(dns, "dns:10.2.1.1:1234");
  test_succeeds(dns, "dns:www.google.com");
  test_succeeds(dns, "dns:///www.google.com");
  grpc_core::UniquePtr<char> resolver =
      GPR_GLOBAL_CONFIG_GET(grpc_dns_resolver);
  if (gpr_stricmp(resolver.get(), "native") == 0) {
    test_fails(dns, "dns://8.8.8.8/8.8.8.8:8888");
  } else {
    test_succeeds(dns, "dns://8.8.8.8/8.8.8.8:8888");
  }
  grpc_shutdown();

  return 0;
}
