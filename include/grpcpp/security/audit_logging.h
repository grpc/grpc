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

#ifndef GRPCPP_SECURITY_AUDIT_LOGGING_H
#define GRPCPP_SECURITY_AUDIT_LOGGING_H

#include <grpc/grpc_audit_logging.h>
#include <grpcpp/support/string_ref.h>

namespace grpc {
namespace experimental {

// This class contains useful information to be consumed in an audit logging
// event.
class AuditContext {
 public:
  // Callers need to ensure the given reference outlives this class object.
  explicit AuditContext(
      const grpc_core::experimental::AuditContext& core_context)
      : core_context_(core_context) {}

  grpc::string_ref rpc_method() const;
  grpc::string_ref principal() const;
  grpc::string_ref policy_name() const;
  grpc::string_ref matched_rule() const;
  bool authorized() const;

 private:
  const grpc_core::experimental::AuditContext& core_context_;
};

// The base class for audit logger implementations.
// Users are expected to inherit this class and implement the Log() function.
class AuditLogger {
 public:
  // This function will be invoked synchronously when applicable during the
  // RBAC-based authorization process. It does not return anything and thus will
  // not impact whether the RPC will be rejected or not.
  virtual void Log(const AuditContext& audit_context) = 0;
};

// The base class for audit logger factory implementations.
// Users should inherit this class and implement those declared virtual
// funcitons.
class AuditLoggerFactory {
 public:
  // The base class for the audit logger config that the factory parses.
  // Users should inherit this class to define the configuration needed for
  // their custom loggers.
  class Config {
   public:
    virtual const char* name() const = 0;
    virtual std::string ToString() = 0;
  };
  virtual const char* name() const = 0;

  virtual absl::StatusOr<std::unique_ptr<Config>> ParseAuditLoggerConfig(
      grpc::string_ref config_json) = 0;

  virtual std::unique_ptr<AuditLogger> CreateAuditLogger(
      std::unique_ptr<AuditLoggerFactory::Config>) = 0;
};

// Registers an audit logger factory. This should only be called during
// initialization.
void RegisterAuditLoggerFactory(std::unique_ptr<AuditLoggerFactory> factory);

}  // namespace experimental
}  // namespace grpc

#endif  // GRPCPP_SECURITY_AUDIT_LOGGING_H