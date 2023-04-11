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

#include "src/core/ext/xds/xds_audit_logger_registry.h"

#include <string>

#include <google/protobuf/any.pb.h>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "envoy/config/rbac/v3/rbac.upb.h"
#include "gtest/gtest.h"
#include "upb/def.hpp"
#include "upb/upb.hpp"

#include <grpc/grpc.h>

#include "src/core/ext/xds/xds_bootstrap_grpc.h"
#include "src/core/lib/json/json_writer.h"
#include "src/proto/grpc/testing/xds/v3/extension.pb.h"
#include "src/proto/grpc/testing/xds/v3/rbac.pb.h"
#include "test/core/util/test_config.h"

namespace grpc_core {
namespace testing {
namespace {

using AuditLoggerConfigProto =
    ::envoy::config::rbac::v3::RBAC::AuditLoggingOptions::AuditLoggerConfig;

absl::StatusOr<std::string> ConvertAuditLoggerConfig(
    const AuditLoggerConfigProto& config) {
  std::string serialized_config = config.SerializeAsString();
  upb::Arena arena;
  upb::SymbolTable symtab;
  XdsResourceType::DecodeContext context = {nullptr,
                                            GrpcXdsBootstrap::GrpcXdsServer(),
                                            nullptr, symtab.ptr(), arena.ptr()};
  auto* upb_config =
      envoy_config_rbac_v3_RBAC_AuditLoggingOptions_AuditLoggerConfig_parse(
          serialized_config.data(), serialized_config.size(), arena.ptr());
  ValidationErrors errors;
  ValidationErrors::ScopedField field(&errors, ".logger_config");
  auto config_json = XdsAuditLoggerRegistry().ConvertXdsAuditLoggerConfig(
      context, upb_config, &errors);
  if (!errors.ok()) {
    return errors.status(absl::StatusCode::kInvalidArgument,
                         "validation errors");
  }
  return JsonDump(config_json);
}

// TODO(lwge): Add stdout logger test when the extension proto exists.

//
// XdsAuditLoggerRegistryTest
//

TEST(XdsAuditLoggerRegistryTest, EmptyAuditLoggerConfig) {
  auto result = ConvertAuditLoggerConfig(AuditLoggerConfigProto());
  EXPECT_EQ(result.status().code(), absl::StatusCode::kOk);
  EXPECT_EQ(*result, "null");
}

TEST(XdsAuditLoggerRegistryTest, NoSupportedType) {
  AuditLoggerConfigProto config;
  config.mutable_audit_logger()->mutable_typed_config()->PackFrom(
      AuditLoggerConfigProto());
  auto result = ConvertAuditLoggerConfig(config);
  EXPECT_EQ(result.status().code(), absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(result.status().message(),
            "validation errors: [field:logger_config error:unsupported audit "
            "logger type]")
      << result.status();
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
