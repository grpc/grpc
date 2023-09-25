//
//
// Copyright 2015 gRPC authors.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
//

#include <memory>
#include <string>
#include <utility>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "gtest/gtest.h"

#include <grpc/support/log.h>

#include "src/core/lib/config/core_configuration.h"
#include "src/core/lib/event_engine/default_event_engine.h"
#include "src/core/lib/gprpp/orphanable.h"
#include "src/core/lib/gprpp/work_serializer.h"
#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/lib/iomgr/port.h"
#include "src/core/lib/resolver/resolver.h"
#include "src/core/lib/resolver/resolver_factory.h"
#include "src/core/lib/resolver/resolver_registry.h"
#include "src/core/lib/uri/uri_parser.h"
#include "test/core/util/test_config.h"

static std::shared_ptr<grpc_core::WorkSerializer>* g_work_serializer;

class ResultHandler : public grpc_core::Resolver::ResultHandler {
 public:
  void ReportResult(grpc_core::Resolver::Result /*result*/) override {}
};

static void test_succeeds(grpc_core::ResolverFactory* factory,
                          const char* string) {
  gpr_log(GPR_DEBUG, "test: '%s' should be valid for '%s'", string,
          std::string(factory->scheme()).c_str());
  grpc_core::ExecCtx exec_ctx;
  absl::StatusOr<grpc_core::URI> uri = grpc_core::URI::Parse(string);
  if (!uri.ok()) {
    gpr_log(GPR_ERROR, "%s", uri.status().ToString().c_str());
    ASSERT_TRUE(uri.ok());
  }
  grpc_core::ResolverArgs args;
  args.uri = std::move(*uri);
  args.work_serializer = *g_work_serializer;
  args.result_handler = std::make_unique<ResultHandler>();
  grpc_core::OrphanablePtr<grpc_core::Resolver> resolver =
      factory->CreateResolver(std::move(args));
  ASSERT_NE(resolver, nullptr);
  resolver->StartLocked();
  // Flush ExecCtx to avoid stack-use-after-scope on on_res_arg which is
  // accessed in the closure on_resolution_cb
  grpc_core::ExecCtx::Get()->Flush();
}

static void test_fails(grpc_core::ResolverFactory* factory,
                       const char* string) {
  gpr_log(GPR_DEBUG, "test: '%s' should be invalid for '%s'", string,
          std::string(factory->scheme()).c_str());
  grpc_core::ExecCtx exec_ctx;
  absl::StatusOr<grpc_core::URI> uri = grpc_core::URI::Parse(string);
  if (!uri.ok()) {
    gpr_log(GPR_ERROR, "%s", uri.status().ToString().c_str());
    ASSERT_TRUE(uri.ok());
  }
  grpc_core::ResolverArgs args;
  args.uri = std::move(*uri);
  args.work_serializer = *g_work_serializer;
  args.result_handler = std::make_unique<ResultHandler>();
  grpc_core::OrphanablePtr<grpc_core::Resolver> resolver =
      factory->CreateResolver(std::move(args));
  ASSERT_EQ(resolver, nullptr);
}

TEST(SockaddrResolverTest, MainTest) {
  auto work_serializer = std::make_shared<grpc_core::WorkSerializer>(
      grpc_event_engine::experimental::GetDefaultEventEngine());
  g_work_serializer = &work_serializer;

  grpc_core::ResolverFactory* ipv4 = grpc_core::CoreConfiguration::Get()
                                         .resolver_registry()
                                         .LookupResolverFactory("ipv4");
  grpc_core::ResolverFactory* ipv6 = grpc_core::CoreConfiguration::Get()
                                         .resolver_registry()
                                         .LookupResolverFactory("ipv6");

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
  grpc_core::ResolverFactory* uds = grpc_core::CoreConfiguration::Get()
                                        .resolver_registry()
                                        .LookupResolverFactory("unix");
  grpc_core::ResolverFactory* uds_abstract =
      grpc_core::CoreConfiguration::Get()
          .resolver_registry()
          .LookupResolverFactory("unix-abstract");

  test_succeeds(uds, "unix:///tmp/sockaddr_resolver_test");
  test_succeeds(uds_abstract, "unix-abstract:sockaddr_resolver_test");
#endif  // GRPC_HAVE_UNIX_SOCKET
}

int main(int argc, char** argv) {
  grpc::testing::TestEnvironment env(&argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  grpc::testing::TestGrpcScope grpc_scope;
  return RUN_ALL_TESTS();
}
