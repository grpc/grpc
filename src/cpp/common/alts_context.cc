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

#include <grpc/grpc_security.h>
#include <grpcpp/alts_context.h>

#include "src/cpp/common/secure_auth_context.h"

namespace grpc {

AltsContext::AltsContext(const grpc_gcp_AltsContext* ctx) {
  upb_strview application_protocol =
      grpc_gcp_AltsContext_application_protocol(ctx);
  application_protocol_ =
      std::string(application_protocol.data, application_protocol.size);
  upb_strview record_protocol = grpc_gcp_AltsContext_record_protocol(ctx);
  record_protocol_ = std::string(record_protocol.data, record_protocol.size);
  upb_strview peer_service_account =
      grpc_gcp_AltsContext_peer_service_account(ctx);
  peer_service_account_ =
      std::string(peer_service_account.data, peer_service_account.size);
  upb_strview local_service_account =
      grpc_gcp_AltsContext_local_service_account(ctx);
  local_service_account_ =
      std::string(local_service_account.data, local_service_account.size);
  const grpc_gcp_RpcProtocolVersions* versions =
      grpc_gcp_AltsContext_peer_rpc_versions(ctx);
  if (versions != nullptr) {
    const grpc_gcp_RpcProtocolVersions_Version* max_versions =
        grpc_gcp_RpcProtocolVersions_max_rpc_version(versions);
    if (max_versions != nullptr) {
      int max_version_major =
          grpc_gcp_RpcProtocolVersions_Version_major(max_versions);
      int max_version_minor =
          grpc_gcp_RpcProtocolVersions_Version_minor(max_versions);
      peer_rpc_versions_.max_rpc_versions.major_version = max_version_major;
      peer_rpc_versions_.max_rpc_versions.minor_version = max_version_minor;
    }
    const grpc_gcp_RpcProtocolVersions_Version* min_versions =
        grpc_gcp_RpcProtocolVersions_min_rpc_version(versions);
    if (min_versions != nullptr) {
      int min_version_major =
          grpc_gcp_RpcProtocolVersions_Version_major(min_versions);
      int min_version_minor =
          grpc_gcp_RpcProtocolVersions_Version_minor(min_versions);
      peer_rpc_versions_.min_rpc_versions.major_version = min_version_major;
      peer_rpc_versions_.min_rpc_versions.minor_version = min_version_minor;
    }
  }
  security_level_ =
      static_cast<SecurityLevel>((int)grpc_gcp_AltsContext_security_level(ctx));
}

std::string AltsContext::application_protocol() const {
  return application_protocol_;
}

std::string AltsContext::record_protocol() const { return record_protocol_; }

std::string AltsContext::peer_service_account() const {
  return peer_service_account_;
}

std::string AltsContext::local_service_account() const {
  return local_service_account_;
}

SecurityLevel AltsContext::security_level() const { return security_level_; }

RpcProtocolVersions AltsContext::peer_rpc_versions() const {
  return peer_rpc_versions_;
}

std::unique_ptr<AltsContext> GetAltsContextFromAuthContext(
    const AuthContext& auth_context) {
  std::vector<string_ref> ctx_vector =
      auth_context.FindPropertyValues(TSI_ALTS_CONTEXT);
  if (ctx_vector.size() != 1) {
    gpr_log(GPR_ERROR, "contains zero or more than one ALTS context.");
    return nullptr;
  }
  upb::Arena context_arena;
  grpc_gcp_AltsContext* ctx = grpc_gcp_AltsContext_parse(
      ctx_vector[0].data(), ctx_vector[0].size(), context_arena.ptr());
  if (ctx == nullptr) {
    gpr_log(GPR_ERROR, "fails to parse ALTS context.");
    return nullptr;
  }
  std::unique_ptr<AltsContext> uniq_ctx(new AltsContext(ctx));
  return uniq_ctx;
}

}  // namespace grpc