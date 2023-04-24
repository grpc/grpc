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

#include "src/core/lib/security/authorization/audit_logging.h"

#include <initializer_list>
#include <map>
#include <memory>
#include <utility>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_format.h"
#include "absl/strings/string_view.h"

#include <grpc/grpc_audit_logging.h>
#include <grpc/support/log.h>

#include "src/core/lib/gprpp/sync.h"
#include "src/core/lib/json/json.h"

namespace grpc_core {
namespace experimental {

namespace {

Mutex* g_mu = new Mutex();
AuditLoggerRegistry* g_registry ABSL_GUARDED_BY(g_mu) =
    new AuditLoggerRegistry();

}  // namespace

void AuditLoggerRegistry::RegisterFactory(
    std::unique_ptr<AuditLoggerFactory> factory) {
  if (factory == nullptr) return;
  MutexLock lock(g_mu);
  GPR_ASSERT(g_registry->logger_factories_map_.find(factory->name()) ==
             g_registry->logger_factories_map_.end());
  g_registry->logger_factories_map_[factory->name()] = std::move(factory);
}

bool AuditLoggerRegistry::FactoryExists(absl::string_view name) {
  MutexLock lock(g_mu);
  return g_registry->logger_factories_map_.find(name) !=
         g_registry->logger_factories_map_.end();
}

absl::StatusOr<std::unique_ptr<AuditLoggerFactory::Config>>
AuditLoggerRegistry::ParseConfig(absl::string_view name, const Json& json) {
  MutexLock lock(g_mu);
  auto factory_or = g_registry->GetAuditLoggerFactory(name);
  if (!factory_or.ok()) {
    return factory_or.status();
  }
  return factory_or.value()->ParseAuditLoggerConfig(json);
}

std::unique_ptr<AuditLogger> AuditLoggerRegistry::CreateAuditLogger(
    std::unique_ptr<AuditLoggerFactory::Config> config) {
  MutexLock lock(g_mu);
  auto factory_or = g_registry->GetAuditLoggerFactory(config->name());
  GPR_ASSERT(factory_or.ok());
  return factory_or.value()->CreateAuditLogger(std::move(config));
}

absl::StatusOr<AuditLoggerFactory*> AuditLoggerRegistry::GetAuditLoggerFactory(
    absl::string_view name) {
  auto it = logger_factories_map_.find(name);
  if (it == logger_factories_map_.end()) {
    return absl::NotFoundError(
        absl::StrFormat("audit logger factory for %s does not exist", name));
  }
  return it->second.get();
}

void AuditLoggerRegistry::TestOnlyResetRegistry() {
  MutexLock lock(g_mu);
  delete g_registry;
  g_registry = new AuditLoggerRegistry();
}

void RegisterAuditLoggerFactory(std::unique_ptr<AuditLoggerFactory> factory) {
  AuditLoggerRegistry::RegisterFactory(std::move(factory));
}

}  // namespace experimental
}  // namespace grpc_core
