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

#include "src/core/lib/security/authorization/audit_logging/audit_logging.h"

#include <initializer_list>
#include <map>
#include <memory>
#include <utility>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_format.h"
#include "absl/strings/string_view.h"
#include "audit_logging.h"

#include <grpc/grpc_audit_logging.h>

#include "src/core/lib/gprpp/sync.h"

namespace grpc_core {
namespace experimental {

void AuditLoggerRegistry::RegisterAuditLoggerFactory(
    std::unique_ptr<AuditLoggerFactory> factory) {
  auto& registry = GetAuditLoggerRegistry();
  MutexLock lock(&registry.mu_);
  registry.logger_factories_map_[factory->name()] = std::move(factory);
}

absl::StatusOr<std::unique_ptr<AuditLoggerFactory::Config>>
AuditLoggerRegistry::ParseAuditLoggerConfig(const Json& json) {
  if (json.type() != Json::Type::kObject) {
    return absl::InvalidArgumentError("audit logger should be of type object");
  }
  if (json.object().empty()) {
    return absl::InvalidArgumentError("no audit logger found in the config");
  }
  if (json.object().size() > 1) {
    return absl::InvalidArgumentError("oneOf violation");
  }
  auto it = json.object().begin();
  if (it->second.type() != Json::Type::kObject) {
    return absl::InvalidArgumentError("logger config should be of type object");
  }
  auto factory_or = AuditLoggerRegistry::GetAuditLoggerFactory(it->first);
  if (!factory_or.ok()) {
    return absl::InvalidArgumentError("unsupported audit logger type");
  }
  return factory_or.value()->ParseAuditLoggerConfig(it->second);
}

std::unique_ptr<AuditLogger> AuditLoggerRegistry::CreateAuditLogger(
    std::unique_ptr<AuditLoggerFactory::Config> config) {
  auto factory_or = AuditLoggerRegistry::GetAuditLoggerFactory(config->name());
  GPR_ASSERT(factory_or.ok());
  return factory_or.value()->CreateAuditLogger(std::move(config));
}

AuditLoggerRegistry& AuditLoggerRegistry::GetAuditLoggerRegistry() {
  static AuditLoggerRegistry& registry = *new AuditLoggerRegistry();
  return registry;
}

absl::StatusOr<AuditLoggerFactory*> AuditLoggerRegistry::GetAuditLoggerFactory(
    absl::string_view name) {
  auto& registry = GetAuditLoggerRegistry();
  MutexLock lock(&registry.mu_);
  auto it = registry.logger_factories_map_.find(name);
  if (it != registry.logger_factories_map_.end()) {
    return it->second.get();
  }
  return absl::NotFoundError(
      absl::StrFormat("audit logger factory %s does not exist", name));
}

void AuditLoggerRegistry::TestOnlyResetRegistry() {
  auto& registry = GetAuditLoggerRegistry();
  MutexLock lock(&registry.mu_);
  registry.logger_factories_map_.clear();
}

void RegisterAuditLoggerFactory(std::unique_ptr<AuditLoggerFactory> factory) {
  AuditLoggerRegistry& registry = AuditLoggerRegistry::GetAuditLoggerRegistry();
  registry.RegisterAuditLoggerFactory(std::move(factory));
}

}  // namespace experimental
}  // namespace grpc_core
