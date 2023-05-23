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

#include <grpc/support/port_platform.h>

#include "test/core/util/audit_logging_utils.h"

#include <algorithm>
#include <memory>
#include <string>
#include <vector>

#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"

#include <grpc/grpc_audit_logging.h>
#include <grpc/support/json.h>

#include "src/core/lib/json/json_writer.h"

namespace grpc_core {
namespace testing {

namespace {

constexpr absl::string_view kLoggerName = "test_logger";

using experimental::AuditContext;
using experimental::AuditLogger;
using experimental::AuditLoggerFactory;
using experimental::Json;

}  // namespace

absl::string_view TestAuditLogger::name() const { return kLoggerName; }
void TestAuditLogger::Log(const AuditContext& context) {
  audit_logs_->push_back(JsonDump(Json::FromObject({
      {"rpc_method", Json::FromString(std::string(context.rpc_method()))},
      {"principal", Json::FromString(std::string(context.principal()))},
      {"policy_name", Json::FromString(std::string(context.policy_name()))},
      {"matched_rule", Json::FromString(std::string(context.matched_rule()))},
      {"authorized", Json::FromBool(context.authorized())},
  })));
}

absl::string_view TestAuditLoggerFactory::Config::name() const {
  return kLoggerName;
}

absl::string_view TestAuditLoggerFactory::name() const { return kLoggerName; }

absl::StatusOr<std::unique_ptr<AuditLoggerFactory::Config>>
TestAuditLoggerFactory::ParseAuditLoggerConfig(const Json&) {
  return std::make_unique<Config>();
}

std::unique_ptr<AuditLogger> TestAuditLoggerFactory::CreateAuditLogger(
    std::unique_ptr<AuditLoggerFactory::Config>) {
  return std::make_unique<TestAuditLogger>(audit_logs_);
}

}  // namespace testing
}  // namespace grpc_core
