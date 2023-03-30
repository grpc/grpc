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

#include "src/cpp/server/audit_logging.h"

#include <memory>

#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "audit_logging.h"

#include <grpc/grpc_audit_logging.h>
#include <grpcpp/security/audit_logging.h>
#include <grpcpp/support/string_ref.h>

namespace grpc {
namespace experimental {

grpc::string_ref AuditContext::rpc_method() const {
  absl::string_view s = core_context_.rpc_method();
  return grpc::string_ref(s.data(), s.length());
}

grpc::string_ref AuditContext::principal() const {
  absl::string_view s = core_context_.principal();
  return grpc::string_ref(s.data(), s.length());
}

grpc::string_ref AuditContext::policy_name() const {
  absl::string_view s = core_context_.policy_name();
  return grpc::string_ref(s.data(), s.length());
}

grpc::string_ref AuditContext::matched_rule() const {
  absl::string_view s = core_context_.matched_rule();
  return grpc::string_ref(s.data(), s.length());
}

bool AuditContext::authorized() const { return core_context_.authorized(); }

void CoreAuditLogger::Log(
    const grpc_core::experimental::AuditContext& core_audit_context) {
  logger_->Log(AuditContext(core_audit_context));
}

const char* CoreAuditLoggerFactory::Config::name() const {
  return config_->name();
}

std::string CoreAuditLoggerFactory::Config::ToString() {
  return config_->ToString();
}

const char* CoreAuditLoggerFactory::name() const { return factory_->name(); }

std::unique_ptr<grpc_core::experimental::AuditLogger>
CoreAuditLoggerFactory::CreateAuditLogger(
    std::unique_ptr<grpc_core::experimental::AuditLoggerFactory::Config>
        core_config) {
  std::unique_ptr<CoreAuditLoggerFactory::Config> config(
      static_cast<CoreAuditLoggerFactory::Config*>(core_config.release()));
  std::unique_ptr<grpc::experimental::AuditLogger> logger =
      factory_->CreateAuditLogger(config->config());
  return std::make_unique<CoreAuditLogger>(std::move(logger));
}

absl::StatusOr<
    std::unique_ptr<grpc_core::experimental::AuditLoggerFactory::Config>>
CoreAuditLoggerFactory::ParseAuditLoggerConfig(absl::string_view config_json) {
  grpc::string_ref s(config_json.data(), config_json.length());
  auto result = factory_->ParseAuditLoggerConfig(s);
  if (!result.ok()) {
    return result.status();
  }
  return std::make_unique<CoreAuditLoggerFactory::Config>(
      std::move(result.value()));
};

void RegisterAuditLoggerFactory(std::unique_ptr<AuditLoggerFactory> factory){};

}  // namespace experimental
}  // namespace grpc
