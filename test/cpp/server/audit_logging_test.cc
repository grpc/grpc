//
//
// Copyright 2023 gRPC authors.
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

#include <grpc/support/port_platform.h>

#include "src/core/lib/security/audit_logging/audit_logging.h"

#include <gtest/gtest.h>

#include <grpc/grpc_audit_logging.h>
#include <grpcpp/security/audit_logging.h>

#include "src/core/lib/json/json.h"
#include "src/cpp/server/audit_logging.h"
#include "test/core/util/test_config.h"
#include "test/core/util/tls_utils.h"

namespace grpc {
namespace testing {

const char kName[] = "test_logger";

using experimental::AuditContext;
using experimental::AuditLogger;
using experimental::AuditLoggerFactory;
using experimental::RegisterAuditLoggerFactory;
using experimental::UnregisterAuditLoggerFactory;

namespace {

class TestAuditLogger : public AuditLogger {
 public:
  void Log(const AuditContext& audit_context) override {}
};

class TestAuditLoggerFactory : public AuditLoggerFactory {
 public:
  class TestConfig : public Config {
   public:
    const char* name() const override { return kName; }
    std::string ToString() override { return "test_config"; }
  };

  const char* name() const override { return kName; }
  std::unique_ptr<AuditLogger> CreateAuditLogger(
      std::unique_ptr<AuditLoggerFactory::Config>) override {
    return std::make_unique<TestAuditLogger>();
  }
  absl::StatusOr<std::unique_ptr<Config>> ParseAuditLoggerConfig(
      const grpc_core::Json& json) override {
    return std::make_unique<TestConfig>();
  }
};

}  // namespace

TEST(AuditLoggingTest, FactoryRegistrationAndLoggerCreation) {
  RegisterAuditLoggerFactory(std::make_unique<TestAuditLoggerFactory>());
  auto& registry = grpc_core::experimental::GetAuditLoggerRegistry();
  auto result = registry.GetAuditLoggerFactory(kName);
  ASSERT_TRUE(result.ok());
  auto* factory = result.value();
  EXPECT_EQ(factory->name(), kName);
  auto result2 = factory->ParseAuditLoggerConfig(grpc_core::Json());
  ASSERT_TRUE(result2.ok());
  std::unique_ptr<grpc_core::experimental::AuditLoggerFactory::Config> config =
      std::move(result2.value());
  ASSERT_NE(factory->CreateAuditLogger(std::move(config)), nullptr);
  UnregisterAuditLoggerFactory(kName);
}

TEST(AuditLoggingTest, FactoryNotFound) {
  auto& registry = grpc_core::experimental::GetAuditLoggerRegistry();
  auto result = registry.GetAuditLoggerFactory(kName);
  ASSERT_EQ(result.ok(), false);
}

}  // namespace testing
}  // namespace grpc

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  grpc::testing::TestEnvironment env(&argc, argv);
  return RUN_ALL_TESTS();
}
