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

#include <grpc/support/port_platform.h>

#include "src/core/lib/security/authorization/stdout_logger.h"

#include <cstdio>
#include <initializer_list>
#include <memory>
#include <string>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_format.h"
#include "absl/strings/string_view.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"

#include <grpc/grpc_audit_logging.h>
#include <grpc/support/log.h>

#include "src/core/lib/json/json.h"

namespace grpc_core {
namespace experimental {

constexpr absl::string_view kName = "stdout_logger";
constexpr char kLogFormat[] =
    "{\"grpc_audit_log\":{\"timestamp\":%d,\"rpc_method\":\"%s\",\"principal\":"
    "\"%s\",\"policy_name\":\"%s\",\"matched_rule\":\"%s\",\"authorized\":%s}}"
    "\n";

void StdoutAuditLogger::Log(const AuditContext& context) {
  absl::FPrintF(stdout, kLogFormat, absl::ToUnixSeconds(absl::Now()),
                context.rpc_method(), context.principal(),
                context.policy_name(), context.matched_rule(),
                context.authorized() ? "true" : "false");
}

absl::string_view StdoutAuditLoggerFactory::Config::name() const {
  return kName;
}

std::string StdoutAuditLoggerFactory::Config::ToString() const { return "{}"; }

absl::string_view StdoutAuditLoggerFactory::name() const { return kName; }

absl::StatusOr<std::unique_ptr<AuditLoggerFactory::Config>>
StdoutAuditLoggerFactory::ParseAuditLoggerConfig(const Json& json) {
  if (json.type() != Json::Type::kObject) {
    return absl::InvalidArgumentError("config is not a json object");
  }
  return std::make_unique<StdoutAuditLoggerFactory::Config>();
}

std::unique_ptr<AuditLogger> StdoutAuditLoggerFactory::CreateAuditLogger(
    std::unique_ptr<AuditLoggerFactory::Config> config) {
  // Sanity check.
  GPR_ASSERT(config != nullptr && config->name() == name());
  return std::make_unique<StdoutAuditLogger>();
}

}  // namespace experimental
}  // namespace grpc_core
