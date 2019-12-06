/*
 *
 * Copyright 2019 gRPC authors.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

#ifndef GRPCPP_ALTS_CONTEXT_H
#define GRPCPP_ALTS_CONTEXT_H

#include <grpcpp/impl/codegen/security/auth_context.h>

#include <memory>
// might need forward declaration
#include "src/core/tsi/alts/handshaker/alts_tsi_handshaker.h"
#include "src/proto/grpc/gcp/altscontext.upb.h"

namespace grpc {

enum SecurityLevel {
  SECURITY_NONE = 0,
  INTEGRITY_ONLY = 1,
  INTEGRITY_AND_PRIVACY = 2
};

typedef struct Versions {
  int major_version;
  int minor_version;
} Versions;

typedef struct RpcProtocolVersions {
  Versions max_rpc_versions;
  Versions min_rpc_versions;
} RpcProtocolVersions;

// AltsContext is wrapper class for grpc_gcp_AltsContext.
// It should only be instantiated by calling GetAltsContextFromAuthContext.
class AltsContext {
 public:
  AltsContext& operator=(const AltsContext&) = default;
  AltsContext(const AltsContext&) = default;
  AltsContext& operator=(AltsContext&&) = default;
  AltsContext(AltsContext&&) = default;

  std::string application_protocol() const;
  std::string record_protocol() const;
  std::string peer_service_account() const;
  std::string local_service_account() const;
  SecurityLevel security_level() const;
  RpcProtocolVersions peer_rpc_versions() const;

 private:
  // TODO(ZhenLian): Also plumb field peer_attributes when it is in use
  std::string application_protocol_;
  std::string record_protocol_;
  std::string peer_service_account_;
  std::string local_service_account_;
  SecurityLevel security_level_ = SECURITY_NONE;
  RpcProtocolVersions peer_rpc_versions_ = {{0, 0}, {0, 0}};
  explicit AltsContext(const grpc_gcp_AltsContext* ctx);
  friend std::unique_ptr<AltsContext> GetAltsContextFromAuthContext(
      const AuthContext& auth_context);
};

// GetAltsContextFromAuthContext helps to get the AltsContext from AuthContext.
// Please make sure the underlying protocol is ALTS before calling this
// function. Otherwise a nullptr will be returned.
std::unique_ptr<AltsContext> GetAltsContextFromAuthContext(
    const AuthContext& auth_context);

}  // namespace grpc

#endif  // GRPCPP_ALTS_CONTEXT_H
