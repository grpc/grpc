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

#include <memory>

#include <gtest/gtest.h>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"

#include <grpc/grpc_audit_logging.h>

#include "src/core/lib/json/json.h"
#include "src/core/lib/security/audit_logging/audit_logging.h"
#include "test/core/util/test_config.h"
#include "test/core/util/tls_utils.h"

namespace grpc_core {
namespace testing {

const char kName[] = "test_logger";

using experimental::AuditContext;
using experimental::AuditLogger;
using experimental::AuditLoggerFactory;
using Config = experimental::AuditLoggerFactory::Config;
using experimental::GetAuditLoggerRegistry;
using experimental::RegisterAuditLoggerFactory;

namespace {

class TestAuditLogger : public AuditLogger {
 public:
  void Log(const AuditContext&) override {}
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
      const Json&) override {
    return std::make_unique<TestConfig>();
  }
};

}  // namespace

TEST(AuditLoggingTest, FactoryRegistrationAndLoggerCreation) {
  RegisterAuditLoggerFactory(std::make_unique<TestAuditLoggerFactory>());
  auto& registry = GetAuditLoggerRegistry();
  auto result = registry.GetAuditLoggerFactory(kName);
  ASSERT_TRUE(result.ok());
  auto* factory = result.value();
  EXPECT_EQ(factory->name(), kName);
  auto parse_result = factory->ParseAuditLoggerConfig(Json());
  ASSERT_TRUE(parse_result.ok());
  std::unique_ptr<Config> config = std::move(parse_result.value());
  ASSERT_NE(factory->CreateAuditLogger(std::move(config)), nullptr);
  registry.TestOnlyUnregisterAuditLoggerFactory(kName);
}

TEST(AuditLoggingTest, FactoryNotFound) {
  auto& registry = GetAuditLoggerRegistry();
  auto result = registry.GetAuditLoggerFactory(kName);
  ASSERT_EQ(result.ok(), false);
}

}  // namespace testing
}  // namespace grpc_core

int main(int argc, char** argv) {
  grpc::testing::TestEnvironment env(&argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  grpc_init();
  int ret = RUN_ALL_TESTS();
  grpc_shutdown();
  return ret;
}
