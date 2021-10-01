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

#include <string.h>

#include <gtest/gtest.h>

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>

#include "src/core/ext/filters/client_channel/resolver_registry.h"
#include "src/core/lib/channel/channel_args.h"
#include "test/core/util/test_config.h"

// Registers the factory with `grpc_core::ResolverRegistry`. Defined in
// binder_resolver.cc
void grpc_resolver_binder_init(void);

namespace {

class BinderResolverTest : public ::testing::Test {
 public:
  BinderResolverTest() {
    factory_ = grpc_core::ResolverRegistry::LookupResolverFactory("binder");
  }
  ~BinderResolverTest() override {}
  static void SetUpTestSuite() {
    grpc_init();
    if (grpc_core::ResolverRegistry::LookupResolverFactory("binder") ==
        nullptr) {
      // Binder resolver will only be registered on platforms that support
      // binder transport. If it is not registered on current platform, we
      // manually register it here for testing purpose.
      grpc_resolver_binder_init();
      ASSERT_TRUE(grpc_core::ResolverRegistry::LookupResolverFactory("binder"));
    }
  }
  static void TearDownTestSuite() { grpc_shutdown(); }

  void SetUp() override { ASSERT_TRUE(factory_); }

  class ResultHandler : public grpc_core::Resolver::ResultHandler {
   public:
    void ReturnResult(grpc_core::Resolver::Result /*result*/) override {}

    void ReturnError(grpc_error_handle error) override {
      GRPC_ERROR_UNREF(error);
    }
  };

  void TestSucceeds(const char* string) {
    gpr_log(GPR_DEBUG, "test: '%s' should be valid for '%s'", string,
            factory_->scheme());
    grpc_core::ExecCtx exec_ctx;
    absl::StatusOr<grpc_core::URI> uri = grpc_core::URI::Parse(string);
    if (!uri.ok()) {
      gpr_log(GPR_ERROR, "%s", uri.status().ToString().c_str());
      ASSERT_TRUE(false);
    }
    grpc_core::ResolverArgs args;
    args.uri = std::move(*uri);
    args.result_handler =
        absl::make_unique<BinderResolverTest::ResultHandler>();
    grpc_core::OrphanablePtr<grpc_core::Resolver> resolver =
        factory_->CreateResolver(std::move(args));
    ASSERT_TRUE(resolver != nullptr);
    resolver->StartLocked();
  }

  void TestFails(const char* string) {
    gpr_log(GPR_DEBUG, "test: '%s' should be invalid for '%s'", string,
            factory_->scheme());
    grpc_core::ExecCtx exec_ctx;
    absl::StatusOr<grpc_core::URI> uri = grpc_core::URI::Parse(string);
    if (!uri.ok()) {
      gpr_log(GPR_ERROR, "%s", uri.status().ToString().c_str());
      ASSERT_TRUE(uri.ok());
    }
    grpc_core::ResolverArgs args;
    args.uri = std::move(*uri);
    args.result_handler =
        absl::make_unique<BinderResolverTest::ResultHandler>();
    grpc_core::OrphanablePtr<grpc_core::Resolver> resolver =
        factory_->CreateResolver(std::move(args));
    EXPECT_TRUE(resolver == nullptr);
  }

 private:
  grpc_core::ResolverFactory* factory_;
};

}  // namespace

TEST_F(BinderResolverTest, WrongScheme) {
  TestFails("bonder:10.2.1.1");
  TestFails("http:google.com");
}

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

// This test is hard coded and assumed that available space is 128 bytes
TEST_F(BinderResolverTest, PathLength) {
  // Length of 102 bytes should be fine
  TestSucceeds(("binder:l" + std::string(100, 'o') + "g").c_str());

  // Length of 128 bytes (including null terminator) should be fine
  TestSucceeds(("binder:l" + std::string(125, 'o') + "g").c_str());

  // Length of 129 bytes (including null terminator) should fail
  TestFails(("binder:l" + std::string(126, 'o') + "g").c_str());

  TestFails(("binder:l" + std::string(200, 'o') + "g").c_str());
}

// Only alphabets and numbers are allowed
TEST_F(BinderResolverTest, InvalidCharacter) {
  TestFails("binder:%");
  TestFails("binder:[[");
  TestFails("binder:google.com");
  TestFails("binder:aaaa,bbbb");
  TestFails("binder:test/");
  TestFails("binder:test:");
}

TEST_F(BinderResolverTest, ValidCases) {
  TestSucceeds("binder:e");
  TestSucceeds("binder:example");
  TestSucceeds("binder:example123");
  TestSucceeds("binder:ExaMpLe123");
  TestSucceeds("binder:12345");
  TestSucceeds("binder:12345Valid");
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  grpc::testing::TestEnvironment env(argc, argv);
  return RUN_ALL_TESTS();
}
