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

#include "src/core/lib/security/audit_logging/grpc_audit_logging.h"

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"

#include "src/core/lib/gprpp/sync.h"

namespace grpc_core {
namespace experimental {

class AuditLoggerRegistry {
 public:
  void RegisterAuditLoggerFactory(std::unique_ptr<AuditLoggerFactory>);
  absl::StatusOr<const AuditLoggerFactory*> GetAuditLoggerFactory(
      absl::string_view);

 private:
  Mutex mu_;
  std::map<std::string, std::unique_ptr<AuditLoggerFactory>>
      logger_factories_map_ ABSL_GUARDED_BY(mu_);
};

void AuditLoggerRegistry::RegisterAuditLoggerFactory(
    std::unique_ptr<AuditLoggerFactory> factory) {
  MutexLock lock(&mu_);
  logger_factories_map_[factory->name()] = std::move(factory);
}

absl::StatusOr<const AuditLoggerFactory*>
AuditLoggerRegistry::GetAuditLoggerFactory(absl::string_view name) {
  MutexLock lock(&mu_);
  auto it = logger_factories_map_.find(std::string(name));
  if (it != logger_factories_map_.end()) {
    return it->second.get();
  }
  return absl::NotFoundError("factory does not exist");
}

AuditLoggerRegistry& GetAuditLoggerRegistry() {
  static AuditLoggerRegistry& registry = *new AuditLoggerRegistry();
  return registry;
}

void RegisterAuditLoggerFactory(std::unique_ptr<AuditLoggerFactory> factory) {
  AuditLoggerRegistry& registry = GetAuditLoggerRegistry();
  registry.RegisterAuditLoggerFactory(std::move(factory));
}

}  // namespace experimental
}  // namespace grpc_core
