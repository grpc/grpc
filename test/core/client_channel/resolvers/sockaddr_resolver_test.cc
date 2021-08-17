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

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>

#include "src/core/ext/filters/client_channel/resolver_registry.h"
#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/iomgr/work_serializer.h"

#include "test/core/util/test_config.h"

static std::shared_ptr<grpc_core::WorkSerializer>* g_work_serializer;

class ResultHandler : public grpc_core::Resolver::ResultHandler {
 public:
  void ReturnResult(grpc_core::Resolver::Result /*result*/) override {}

  void ReturnError(grpc_error_handle error) override {
    GRPC_ERROR_UNREF(error);
  }
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
  args.result_handler = absl::make_unique<ResultHandler>();
  grpc_core::OrphanablePtr<grpc_core::Resolver> resolver =
      factory->CreateResolver(std::move(args));
  GPR_ASSERT(resolver != nullptr);
  resolver->StartLocked();
  /* Flush ExecCtx to avoid stack-use-after-scope on on_res_arg which is
   * accessed in the closure on_resolution_cb */
  grpc_core::ExecCtx::Get()->Flush();
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
  args.result_handler = absl::make_unique<ResultHandler>();
  grpc_core::OrphanablePtr<grpc_core::Resolver> resolver =
      factory->CreateResolver(std::move(args));
  GPR_ASSERT(resolver == nullptr);
}

int main(int argc, char** argv) {
  grpc::testing::TestEnvironment env(argc, argv);
  grpc_init();

  auto work_serializer = std::make_shared<grpc_core::WorkSerializer>();
  g_work_serializer = &work_serializer;

  grpc_core::ResolverFactory* ipv4 =
      grpc_core::ResolverRegistry::LookupResolverFactory("ipv4");
  grpc_core::ResolverFactory* ipv6 =
      grpc_core::ResolverRegistry::LookupResolverFactory("ipv6");

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

#ifdef GRPC_HAVE_UNIX_SOCKET
  grpc_core::ResolverFactory* uds =
      grpc_core::ResolverRegistry::LookupResolverFactory("unix");
  grpc_core::ResolverFactory* uds_abstract =
      grpc_core::ResolverRegistry::LookupResolverFactory("unix-abstract");

  test_succeeds(uds, "unix:///tmp/sockaddr_resolver_test");
  test_succeeds(uds_abstract, "unix-abstract:sockaddr_resolver_test");
#endif  // GRPC_HAVE_UNIX_SOCKET

  grpc_shutdown();

  return 0;
}
