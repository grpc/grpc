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

#ifndef GRPC_SRC_CORE_LIB_SECURITY_AUDIT_LOGGING_AUDIT_LOGGING_H
#define GRPC_SRC_CORE_LIB_SECURITY_AUDIT_LOGGING_AUDIT_LOGGING_H

#include <grpc/support/port_platform.h>

#include <map>
#include <memory>
#include <string>

#include "absl/base/thread_annotations.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"

#include <grpc/grpc_audit_logging.h>

#include "src/core/lib/gprpp/sync.h"

namespace grpc_core {
namespace experimental {

class AuditLoggerRegistry {
 public:
  void RegisterAuditLoggerFactory(std::unique_ptr<AuditLoggerFactory>);
  absl::StatusOr<AuditLoggerFactory*> GetAuditLoggerFactory(absl::string_view);

  // Factories are registered during initialization. They should never be
  // unregistered since they will be looked up at any time till the program
  // exits. This function should only be used in tests.
  void TestOnlyUnregisterAuditLoggerFactory(absl::string_view name);

 private:
  Mutex mu_;
  std::map<std::string, std::unique_ptr<AuditLoggerFactory>>
      logger_factories_map_ ABSL_GUARDED_BY(mu_);
};

// Get the static registry instance.
AuditLoggerRegistry& GetAuditLoggerRegistry();

}  // namespace experimental
}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_LIB_SECURITY_AUDIT_LOGGING_AUDIT_LOGGING_H
