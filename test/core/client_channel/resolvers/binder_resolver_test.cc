// Copyright 2021 gRPC authors.
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

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "gtest/gtest.h"

#include "src/core/lib/config/core_configuration.h"
#include "src/core/lib/gprpp/orphanable.h"
#include "src/core/lib/iomgr/port.h"
#include "src/core/lib/iomgr/resolved_address.h"
#include "src/core/lib/uri/uri_parser.h"
#include "src/core/resolver/endpoint_addresses.h"
#include "src/core/resolver/resolver.h"
#include "src/core/resolver/resolver_factory.h"
#include "test/core/util/test_config.h"

#ifdef GRPC_HAVE_UNIX_SOCKET

#include <sys/socket.h>
#include <sys/un.h>

#include <grpc/grpc.h>
#include <grpc/support/log.h>

#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/resolver/resolver_registry.h"

// Registers the factory with `grpc_core::ResolverRegistry`. Defined in
// binder_resolver.cc
namespace grpc_core {
void RegisterBinderResolver(CoreConfiguration::Builder* builder);
}

namespace {

class BinderResolverTest : public ::testing::Test {
 public:
  BinderResolverTest() {
    factory_ = grpc_core::CoreConfiguration::Get()
                   .resolver_registry()
                   .LookupResolverFactory("binder");
  }
  ~BinderResolverTest() override {}
  static void SetUpTestSuite() {
    builder_ =
        std::make_unique<grpc_core::CoreConfiguration::WithSubstituteBuilder>(
            [](grpc_core::CoreConfiguration::Builder* builder) {
              BuildCoreConfiguration(builder);
              if (!builder->resolver_registry()->HasResolverFactory("binder")) {
                // Binder resolver will only be registered on platforms that
                // support binder transport. If it is not registered on current
                // platform, we manually register it here for testing purpose.
                RegisterBinderResolver(builder);
                ASSERT_TRUE(
                    builder->resolver_registry()->HasResolverFactory("binder"));
              }
            });
    grpc_init();
    if (grpc_core::CoreConfiguration::Get()
            .resolver_registry()
            .LookupResolverFactory("binder") == nullptr) {
    }
  }
  static void TearDownTestSuite() {
    grpc_shutdown();
    builder_.reset();
  }

  void SetUp() override { ASSERT_TRUE(factory_); }

  class ResultHandler : public grpc_core::Resolver::ResultHandler {
   public:
    ResultHandler() = default;

    explicit ResultHandler(const std::string& expected_binder_id)
        : expect_result_(true), expected_binder_id_(expected_binder_id) {}

    void ReportResult(grpc_core::Resolver::Result result) override {
      EXPECT_TRUE(expect_result_);
      ASSERT_TRUE(result.addresses.ok());
      ASSERT_EQ(result.addresses->size(), 1);
      grpc_core::EndpointAddresses addr = (*result.addresses)[0];
      const struct sockaddr_un* un =
          reinterpret_cast<const struct sockaddr_un*>(addr.address().addr);
      EXPECT_EQ(addr.address().len,
                sizeof(un->sun_family) + expected_binder_id_.length() + 1);
      EXPECT_EQ(un->sun_family, AF_MAX);
      EXPECT_EQ(un->sun_path, expected_binder_id_);
    }

   private:
    // Whether we expect ReportResult function to be invoked
    bool expect_result_ = false;

    std::string expected_binder_id_;
  };

  void TestSucceeds(const char* string, const std::string& expected_path) {
    gpr_log(GPR_DEBUG, "test: '%s' should be valid for '%s'", string,
            std::string(factory_->scheme()).c_str());
    grpc_core::ExecCtx exec_ctx;
    absl::StatusOr<grpc_core::URI> uri = grpc_core::URI::Parse(string);
    ASSERT_TRUE(uri.ok()) << uri.status().ToString();
    grpc_core::ResolverArgs args;
    args.uri = std::move(*uri);
    args.result_handler =
        std::make_unique<BinderResolverTest::ResultHandler>(expected_path);
    grpc_core::OrphanablePtr<grpc_core::Resolver> resolver =
        factory_->CreateResolver(std::move(args));
    ASSERT_TRUE(resolver != nullptr);
    resolver->StartLocked();
  }

  void TestFails(const char* string) {
    gpr_log(GPR_DEBUG, "test: '%s' should be invalid for '%s'", string,
            std::string(factory_->scheme()).c_str());
    grpc_core::ExecCtx exec_ctx;
    absl::StatusOr<grpc_core::URI> uri = grpc_core::URI::Parse(string);
    ASSERT_TRUE(uri.ok()) << uri.status().ToString();
    grpc_core::ResolverArgs args;
    args.uri = std::move(*uri);
    args.result_handler = std::make_unique<BinderResolverTest::ResultHandler>();
    grpc_core::OrphanablePtr<grpc_core::Resolver> resolver =
        factory_->CreateResolver(std::move(args));
    EXPECT_TRUE(resolver == nullptr);
  }

 private:
  grpc_core::ResolverFactory* factory_;
  static std::unique_ptr<grpc_core::CoreConfiguration::WithSubstituteBuilder>
      builder_;
};

std::unique_ptr<grpc_core::CoreConfiguration::WithSubstituteBuilder>
    BinderResolverTest::builder_;

}  // namespace

// Authority is not allowed
TEST_F(BinderResolverTest, AuthorityPresents) {
  TestFails("binder://example");
  TestFails("binder://google.com");
  TestFails("binder://google.com/test");
}

// Path cannot be empty
TEST_F(BinderResolverTest, EmptyPath) {
  TestFails("binder:");
  TestFails("binder:/");
  TestFails("binder://");
}

TEST_F(BinderResolverTest, PathLength) {
  // Note that we have a static assert in binder_resolver.cc that checks
  // sizeof(sockaddr_un::sun_path) is greater than 100

  // 100 character path should be fine
  TestSucceeds(("binder:l" + std::string(98, 'o') + "g").c_str(),
               "l" + std::string(98, 'o') + "g");

  // 200 character path most likely will fail
  TestFails(("binder:l" + std::string(198, 'o') + "g").c_str());
}

TEST_F(BinderResolverTest, SlashPrefixes) {
  TestSucceeds("binder:///test", "test");
  TestSucceeds("binder:////test", "/test");
}

TEST_F(BinderResolverTest, ValidCases) {
  TestSucceeds("binder:[[", "[[");
  TestSucceeds("binder:google!com", "google!com");
  TestSucceeds("binder:test/", "test/");
  TestSucceeds("binder:test:", "test:");

  TestSucceeds("binder:e", "e");
  TestSucceeds("binder:example", "example");
  TestSucceeds("binder:google.com", "google.com");
  TestSucceeds("binder:~", "~");
  TestSucceeds("binder:12345", "12345");
  TestSucceeds(
      "binder:abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-._"
      "~",
      "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-._~");
}

#endif

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  grpc::testing::TestEnvironment env(&argc, argv);
  return RUN_ALL_TESTS();
}
