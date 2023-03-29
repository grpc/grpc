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

#include <memory>

#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"

#include <grpc/grpc_audit_logging.h>
#include <grpcpp/security/audit_logging.h>
#include <grpcpp/support/string_ref.h>

namespace grpc {
namespace experimental {

using grpc_core::experimental::CoreAuditContext;
using grpc_core::experimental::CoreAuditLogger;
using grpc_core::experimental::CoreAuditLoggerFactory;

grpc::string_ref AuditContext::rpc_method() const {
  absl::string_view s = core_context_->rpc_method();
  return grpc::string_ref(s.data(), s.length());
}

grpc::string_ref AuditContext::principal() const {
  absl::string_view s = core_context_->principal();
  return grpc::string_ref(s.data(), s.length());
}

grpc::string_ref AuditContext::policy_name() const {
  absl::string_view s = core_context_->policy_name();
  return grpc::string_ref(s.data(), s.length());
}

grpc::string_ref AuditContext::matched_rule() const {
  absl::string_view s = core_context_->matched_rule();
  return grpc::string_ref(s.data(), s.length());
}

bool AuditContext::authorized() const { return core_context_->authorized(); }

void AuditLogger::CoreLog(const CoreAuditContext& core_audit_context) {
  AuditContext audit_context(&core_audit_context);
  Log(audit_context);
}

const char* AuditLoggerFactory::Config::core_name() const { return name(); }

std::string AuditLoggerFactory::Config::CoreToString() { return ToString(); }

const char* AuditLoggerFactory::core_name() const { return name(); }

absl::StatusOr<std::unique_ptr<CoreAuditLoggerFactory::CoreConfig>>
AuditLoggerFactory::ParseCoreAuditLoggerConfig(absl::string_view config_json) {
  grpc::string_ref config(config_json.data(), config_json.length());
  return ParseAuditLoggerConfig(config);
}

std::unique_ptr<CoreAuditLogger> AuditLoggerFactory::CreateCoreAuditLogger(
    std::unique_ptr<CoreAuditLoggerFactory::CoreConfig> config) {
  std::unique_ptr<AuditLoggerFactory::Config> c(
      static_cast<AuditLoggerFactory::Config*>(config.release()));
  return CreateAuditLogger(std::move(c));
}

void RegisterAuditLoggerFactory(std::unique_ptr<AuditLoggerFactory> factory) {
  std::unique_ptr<CoreAuditLoggerFactory> core_factory(
      static_cast<CoreAuditLoggerFactory*>(factory.release()));
  grpc_core::experimental::RegisterAuditLoggerFactory(std::move(core_factory));
};

}  // namespace experimental
}  // namespace grpc
