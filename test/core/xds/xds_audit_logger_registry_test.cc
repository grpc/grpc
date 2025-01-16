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

#include "src/core/xds/grpc/xds_audit_logger_registry.h"

#include <google/protobuf/any.pb.h>
#include <grpc/grpc.h>
#include <grpc/grpc_audit_logging.h>

#include <memory>
#include <string>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_format.h"
#include "absl/strings/string_view.h"
#include "envoy/config/rbac/v3/rbac.pb.h"
#include "envoy/extensions/rbac/audit_loggers/stream/v3/stream.pb.h"
#include "google/protobuf/struct.pb.h"
#include "gtest/gtest.h"
#include "src/core/lib/security/authorization/audit_logging.h"
#include "src/core/util/crash.h"
#include "src/core/util/json/json.h"
#include "src/core/util/json/json_writer.h"
#include "src/core/xds/grpc/xds_bootstrap_grpc.h"
#include "test/core/test_util/test_config.h"
#include "upb/mem/arena.hpp"
#include "upb/reflection/def.hpp"
#include "xds/type/v3/typed_struct.pb.h"

namespace grpc_core {
namespace testing {
namespace {

using experimental::AuditLogger;
using experimental::AuditLoggerFactory;
using experimental::AuditLoggerRegistry;
using AuditLoggerConfigProto =
    ::envoy::config::rbac::v3::RBAC::AuditLoggingOptions::AuditLoggerConfig;
using ::envoy::extensions::rbac::audit_loggers::stream::v3::StdoutAuditLog;
using ::xds::type::v3::TypedStruct;

constexpr absl::string_view kName = "test_logger";

absl::StatusOr<std::string> ConvertAuditLoggerConfig(
    const AuditLoggerConfigProto& config) {
  std::string serialized_config = config.SerializeAsString();
  upb::Arena arena;
  upb::DefPool def_pool;
  XdsResourceType::DecodeContext context = {nullptr, GrpcXdsServer(),
                                            def_pool.ptr(), arena.ptr()};
  auto* upb_config =
      envoy_config_rbac_v3_RBAC_AuditLoggingOptions_AuditLoggerConfig_parse(
          serialized_config.data(), serialized_config.size(), arena.ptr());
  ValidationErrors errors;
  auto config_json = XdsAuditLoggerRegistry().ConvertXdsAuditLoggerConfig(
      context, upb_config, &errors);
  if (!errors.ok()) {
    return errors.status(absl::StatusCode::kInvalidArgument,
                         "validation errors");
  }
  return JsonDump(config_json);
}

class TestAuditLoggerFactory : public AuditLoggerFactory {
 public:
  absl::string_view name() const override { return kName; }
  absl::StatusOr<std::unique_ptr<AuditLoggerFactory::Config>>
  ParseAuditLoggerConfig(const Json& json) override {
    if (json.object().find("bad") != json.object().end()) {
      return absl::InvalidArgumentError("invalid test_logger config");
    }
    return nullptr;
  }
  std::unique_ptr<AuditLogger> CreateAuditLogger(
      std::unique_ptr<AuditLoggerFactory::Config>) override {
    Crash("unreachable");
    return nullptr;
  }
};

class XdsAuditLoggerRegistryTest : public ::testing::Test {
 protected:
  void SetUp() override {
    AuditLoggerRegistry::RegisterFactory(
        std::make_unique<TestAuditLoggerFactory>());
  }

  void TearDown() override { AuditLoggerRegistry::TestOnlyResetRegistry(); }
};

//
// StdoutLoggerTest
//

TEST(StdoutLoggerTest, BasicStdoutLogger) {
  AuditLoggerConfigProto config;
  config.mutable_audit_logger()->mutable_typed_config()->PackFrom(
      StdoutAuditLog());
  auto result = ConvertAuditLoggerConfig(config);
  ASSERT_TRUE(result.ok()) << result.status();
  EXPECT_EQ(*result, "{\"stdout_logger\":{}}");
}

//
// ThirdPartyLoggerTest
//

TEST_F(XdsAuditLoggerRegistryTest, ValidThirdPartyLogger) {
  AuditLoggerConfigProto config;
  TypedStruct logger;
  logger.set_type_url(absl::StrFormat("myorg/foo/bar/%s", kName));
  auto* fields = logger.mutable_value()->mutable_fields();
  (*fields)["foo"].set_string_value("bar");
  config.mutable_audit_logger()->mutable_typed_config()->PackFrom(logger);
  auto result = ConvertAuditLoggerConfig(config);
  ASSERT_TRUE(result.ok()) << result.status();
  EXPECT_EQ(*result, "{\"test_logger\":{\"foo\":\"bar\"}}");
}

TEST_F(XdsAuditLoggerRegistryTest, InvalidThirdPartyLoggerConfig) {
  AuditLoggerConfigProto config;
  TypedStruct logger;
  logger.set_type_url(absl::StrFormat("myorg/foo/bar/%s", kName));
  auto* fields = logger.mutable_value()->mutable_fields();
  (*fields)["bad"].set_string_value("true");
  config.mutable_audit_logger()->mutable_typed_config()->PackFrom(logger);
  auto result = ConvertAuditLoggerConfig(config);
  EXPECT_EQ(result.status().code(), absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(result.status().message(),
            "validation errors: "
            "[field:audit_logger.typed_config.value"
            "[xds.type.v3.TypedStruct].value[test_logger] "
            "error:invalid test_logger config]")
      << result.status();
}

//
// XdsAuditLoggerRegistryTest
//

TEST_F(XdsAuditLoggerRegistryTest, EmptyAuditLoggerConfig) {
  auto result = ConvertAuditLoggerConfig(AuditLoggerConfigProto());
  EXPECT_EQ(result.status().code(), absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(result.status().message(),
            "validation errors: [field:audit_logger error:field not present]")
      << result.status();
}

TEST_F(XdsAuditLoggerRegistryTest, MissingTypedConfig) {
  AuditLoggerConfigProto config;
  config.mutable_audit_logger();
  auto result = ConvertAuditLoggerConfig(config);
  EXPECT_EQ(result.status().code(), absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(result.status().message(),
            "validation errors: [field:audit_logger.typed_config error:field "
            "not present]")
      << result.status();
}

TEST_F(XdsAuditLoggerRegistryTest, NoSupportedType) {
  AuditLoggerConfigProto config;
  config.mutable_audit_logger()->mutable_typed_config()->PackFrom(
      AuditLoggerConfigProto());
  auto result = ConvertAuditLoggerConfig(config);
  EXPECT_EQ(result.status().code(), absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(result.status().message(),
            "validation errors: "
            "[field:audit_logger.typed_config.value[envoy.config.rbac.v3.RBAC."
            "AuditLoggingOptions.AuditLoggerConfig] error:unsupported audit "
            "logger type]")
      << result.status();
}

TEST_F(XdsAuditLoggerRegistryTest, NoSupportedTypeButIsOptional) {
  AuditLoggerConfigProto config;
  config.mutable_audit_logger()->mutable_typed_config()->PackFrom(
      AuditLoggerConfigProto());
  config.set_is_optional(true);
  auto result = ConvertAuditLoggerConfig(config);
  EXPECT_EQ(result.status().code(), absl::StatusCode::kOk);
  EXPECT_EQ(*result, "null");
}

}  // namespace
}  // namespace testing
}  // namespace grpc_core

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  grpc::testing::TestEnvironment env(&argc, argv);
  grpc_init();
  auto result = RUN_ALL_TESTS();
  grpc_shutdown();
  return result;
}
