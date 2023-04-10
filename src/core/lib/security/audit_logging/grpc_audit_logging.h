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

#ifndef GRPC_SRC_CORE_LIB_SECURITY_AUDIT_LOGGING_GRPC_AUDIT_LOGGING_H
#define GRPC_SRC_CORE_LIB_SECURITY_AUDIT_LOGGING_GRPC_AUDIT_LOGGING_H

#include <grpc/support/port_platform.h>

#include <memory>
#include <string>

#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"

#include "src/core/lib/json/json.h"

namespace grpc_core {
namespace experimental {

using grpc_core::Json;

// The base struct for audit context.
class AuditContext {
 public:
  absl::string_view rpc_method() const { return rpc_method_; }
  absl::string_view principal() const { return principal_; }
  absl::string_view policy_name() const { return policy_name_; }
  absl::string_view matched_rule() const { return matched_rule_; }
  bool authorized() const { return authorized_; }

 private:
  absl::string_view rpc_method_;
  absl::string_view principal_;
  absl::string_view policy_name_;
  absl::string_view matched_rule_;
  bool authorized_;
};

// This base class for audit logger implementations.
class AuditLogger {
 public:
  virtual ~AuditLogger() = default;
  virtual void Log(const AuditContext& audit_context) = 0;
};

// This is the base class for audit logger factory implementations.
class AuditLoggerFactory {
 public:
  class Config {
   public:
    virtual ~Config() = default;
    virtual const char* name() const = 0;
    virtual std::string ToString() = 0;
  };

  virtual ~AuditLoggerFactory() = default;
  virtual const char* name() const = 0;

  virtual absl::StatusOr<std::unique_ptr<Config>> ParseAuditLoggerConfig(
      const Json& json) = 0;

  virtual std::unique_ptr<AuditLogger> CreateAuditLogger(
      std::unique_ptr<AuditLoggerFactory::Config>) = 0;
};

// Registers an audit logger factory. This should only be called during
// initialization.
void RegisterAuditLoggerFactory(std::unique_ptr<AuditLoggerFactory> factory);

}  // namespace experimental
}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_LIB_SECURITY_AUDIT_LOGGING_GRPC_AUDIT_LOGGING_H
