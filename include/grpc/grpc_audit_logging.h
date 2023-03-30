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

#ifndef GRPC_GRPC_AUDIT_LOGGING_H
#define GRPC_GRPC_AUDIT_LOGGING_H

#include <grpc/support/port_platform.h>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"

namespace grpc_core {
namespace experimental {

// The base struct for audit context.
class AuditContext {
 public:
  absl::string_view rpc_method() const;
  absl::string_view principal() const;
  absl::string_view policy_name() const;
  absl::string_view matched_rule() const;
  bool authorized() const;
};

// This base class for audit logger implementations.
class AuditLogger {
 public:
  virtual void Log(const AuditContext& audit_context) = 0;
};

// This is the base class for audit logger factory implementations.
class AuditLoggerFactory {
 public:
  class Config {
   public:
    virtual const char* name() const = 0;
    virtual std::string ToString() = 0;
  };
  virtual const char* name() const = 0;

  // TODO(lwge): change to grpc_core::Json once it's exposed.
  virtual absl::StatusOr<std::unique_ptr<Config>> ParseAuditLoggerConfig(
      absl::string_view config_json) = 0;

  virtual std::unique_ptr<AuditLogger> CreateAuditLogger(
      std::unique_ptr<AuditLoggerFactory::Config>) = 0;
};

// Registers an audit logger factory. This should only be called during
// initialization.
void RegisterAuditLoggerFactory(std::unique_ptr<AuditLoggerFactory> factory);

}  // namespace experimental
}  // namespace grpc_core

#endif /* GRPC_GRPC_AUDIT_LOGGING_H */
