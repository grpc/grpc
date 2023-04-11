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

#ifndef GRPC_SRC_CPP_SERVER_AUDIT_LOGGING_H
#define GRPC_SRC_CPP_SERVER_AUDIT_LOGGING_H

#include <memory>
#include <string>
#include <utility>

#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"

#include <grpc/grpc_audit_logging.h>
#include <grpcpp/security/audit_logging.h>

#include "src/core/lib/json/json.h"

namespace grpc {
namespace experimental {

// Unregisters an audit logger factory. This should only be used in tests.
void UnregisterAuditLoggerFactory(absl::string_view name);

class CoreAuditLogger : public grpc_core::experimental::AuditLogger {
 public:
  explicit CoreAuditLogger(
      std::unique_ptr<grpc::experimental::AuditLogger> logger)
      : logger_(std::move(logger)) {}
  void Log(const grpc_core::experimental::AuditContext& audit_context) override;

 private:
  std::unique_ptr<grpc::experimental::AuditLogger> logger_;
};

class CoreAuditLoggerFactory
    : public grpc_core::experimental::AuditLoggerFactory {
 public:
  class Config : public grpc_core::experimental::AuditLoggerFactory::Config {
   public:
    explicit Config(
        std::unique_ptr<grpc::experimental::AuditLoggerFactory::Config> config)
        : config_(std::move(config)) {}

    const char* name() const override;
    std::string ToString() override;

    std::unique_ptr<grpc::experimental::AuditLoggerFactory::Config> config() {
      return std::move(config_);
    };

   private:
    std::unique_ptr<grpc::experimental::AuditLoggerFactory::Config> config_;
  };
  explicit CoreAuditLoggerFactory(
      std::unique_ptr<grpc::experimental::AuditLoggerFactory> factory)
      : factory_(std::move(factory)) {}

  const char* name() const override;
  std::unique_ptr<grpc_core::experimental::AuditLogger> CreateAuditLogger(
      std::unique_ptr<grpc_core::experimental::AuditLoggerFactory::Config>)
      override;
  absl::StatusOr<
      std::unique_ptr<grpc_core::experimental::AuditLoggerFactory::Config>>
  ParseAuditLoggerConfig(const Json& json) override;

 private:
  std::unique_ptr<grpc::experimental::AuditLoggerFactory> factory_;
};

}  // namespace experimental
}  // namespace grpc

#endif  // GRPC_SRC_CPP_SERVER_AUDIT_LOGGING_H
