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
#include <string>

#include <gtest/gtest.h>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"

#include <grpc/grpc_audit_logging.h>

#include "src/core/lib/json/json.h"
#include "src/core/lib/security/authorization/audit_logging/audit_logging.h"
#include "test/core/util/test_config.h"
#include "test/core/util/tls_utils.h"

namespace grpc_core {
namespace testing {

constexpr absl::string_view kName = "test_logger";

using experimental::AuditContext;
using experimental::AuditLogger;
using experimental::AuditLoggerFactory;
using experimental::AuditLoggerRegistry;
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
    absl::string_view name() const override { return kName; }
    std::string ToString() const override { return "test_config"; }
  };

  absl::string_view name() const override { return kName; }
  std::unique_ptr<AuditLogger> CreateAuditLogger(
      std::unique_ptr<AuditLoggerFactory::Config>) override {
    return std::make_unique<TestAuditLogger>();
  }
  absl::StatusOr<std::unique_ptr<Config>> ParseAuditLoggerConfig(
      const Json&) override {
    return std::make_unique<TestConfig>();
  }
};

class AuditLoggingTest : public ::testing::Test {
 protected:
  void SetUp() override {
    RegisterAuditLoggerFactory(std::make_unique<TestAuditLoggerFactory>());
  }
  void TearDown() override { AuditLoggerRegistry::TestOnlyResetRegistry(); }
};

}  // namespace

TEST_F(AuditLoggingTest, SuccessfulLoggerCreation) {
  auto result = AuditLoggerRegistry::ParseAuditLoggerConfig(
      Json::Object{{std::string(kName), Json::Object()}});
  ASSERT_TRUE(result.ok());
  ASSERT_NE(AuditLoggerRegistry::CreateAuditLogger(std::move(result.value())),
            nullptr);
}

TEST_F(AuditLoggingTest, UnknownLogger) {
  auto result = AuditLoggerRegistry::ParseAuditLoggerConfig(
      Json::Object{{"unknown_logger", Json::Object()}});
  EXPECT_EQ(result.status().code(), absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(result.status().message(), "unsupported audit logger type")
      << result.status();
}

TEST_F(AuditLoggingTest, ConfigIsNotJsonObject) {
  auto result =
      AuditLoggerRegistry::ParseAuditLoggerConfig(Json::Array{"foo", "bar"});
  EXPECT_EQ(result.status().code(), absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(result.status().message(), "audit logger should be of type object")
      << result.status();
}

TEST_F(AuditLoggingTest, ConfigIsEmpty) {
  auto result = AuditLoggerRegistry::ParseAuditLoggerConfig(Json::Object());
  EXPECT_EQ(result.status().code(), absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(result.status().message(), "no audit logger found in the config")
      << result.status();
}

TEST_F(AuditLoggingTest, ConfigViolatesOneOf) {
  auto result = AuditLoggerRegistry::ParseAuditLoggerConfig(
      Json::Object{{"foo", Json::Object()}, {"bar", Json::Object()}});
  EXPECT_EQ(result.status().code(), absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(result.status().message(), "oneOf violation") << result.status();
}

TEST_F(AuditLoggingTest, LoggerConfigIsNotJsonObject) {
  auto result = AuditLoggerRegistry::ParseAuditLoggerConfig(
      Json::Object{{"foo", Json::Array()}});
  EXPECT_EQ(result.status().code(), absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(result.status().message(), "logger config should be of type object")
      << result.status();
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
