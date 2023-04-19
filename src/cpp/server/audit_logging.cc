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

#include "src/core/lib/security/audit_logging/audit_logging.h"

#include <memory>

#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"

#include <grpc/grpc_audit_logging.h>
#include <grpcpp/security/audit_logging.h>
#include <grpcpp/support/string_ref.h>

#include "src/core/lib/json/json.h"

namespace grpc {
namespace experimental {

void RegisterAuditLoggerFactory(std::unique_ptr<AuditLoggerFactory> factory) {
  grpc_core::experimental::RegisterAuditLoggerFactory(std::move(factory));
};

}  // namespace experimental
}  // namespace grpc
