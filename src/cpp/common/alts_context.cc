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

#include "src/core/lib/gprpp/memory.h"
#include "src/core/tsi/alts/handshaker/alts_tsi_handshaker.h"
#include "src/cpp/common/secure_auth_context.h"
#include "src/proto/grpc/gcp/altscontext.upb.h"

namespace grpc {

AltsContext::AltsContext(const grpc_gcp_AltsContext* ctx) {
  upb_strview application_protocol =
      grpc_gcp_AltsContext_application_protocol(ctx);
  if (application_protocol.data != nullptr && application_protocol.size > 0) {
    application_protocol_ =
        grpc::string(application_protocol.data, application_protocol.size);
  }
  upb_strview record_protocol = grpc_gcp_AltsContext_record_protocol(ctx);
  if (record_protocol.data != nullptr && record_protocol.size > 0) {
    record_protocol_ = grpc::string(record_protocol.data, record_protocol.size);
  }
  upb_strview peer_service_account =
      grpc_gcp_AltsContext_peer_service_account(ctx);
  if (peer_service_account.data != nullptr && peer_service_account.size > 0) {
    peer_service_account_ =
        grpc::string(peer_service_account.data, peer_service_account.size);
  }
  upb_strview local_service_account =
      grpc_gcp_AltsContext_local_service_account(ctx);
  if (local_service_account.data != nullptr && local_service_account.size > 0) {
    local_service_account_ =
        grpc::string(local_service_account.data, local_service_account.size);
  }
  const grpc_gcp_RpcProtocolVersions* versions =
      grpc_gcp_AltsContext_peer_rpc_versions(ctx);
  if (versions != nullptr) {
    const grpc_gcp_RpcProtocolVersions_Version* max_version =
        grpc_gcp_RpcProtocolVersions_max_rpc_version(versions);
    if (max_version != nullptr) {
      int max_version_major =
          grpc_gcp_RpcProtocolVersions_Version_major(max_version);
      int max_version_minor =
          grpc_gcp_RpcProtocolVersions_Version_minor(max_version);
      peer_rpc_versions_.max_rpc_versions.major_version = max_version_major;
      peer_rpc_versions_.max_rpc_versions.minor_version = max_version_minor;
    }
    const grpc_gcp_RpcProtocolVersions_Version* min_version =
        grpc_gcp_RpcProtocolVersions_min_rpc_version(versions);
    if (min_version != nullptr) {
      int min_version_major =
          grpc_gcp_RpcProtocolVersions_Version_major(min_version);
      int min_version_minor =
          grpc_gcp_RpcProtocolVersions_Version_minor(min_version);
      peer_rpc_versions_.min_rpc_versions.major_version = min_version_major;
      peer_rpc_versions_.min_rpc_versions.minor_version = min_version_minor;
    }
  }
  if ((int)grpc_gcp_AltsContext_security_level(ctx) >= GRPC_SECURITY_MIN ||
      (int)grpc_gcp_AltsContext_security_level(ctx) <= GRPC_SECURITY_MAX) {
    security_level_ = static_cast<grpc_security_level>(
        (int)grpc_gcp_AltsContext_security_level(ctx));
  }
}

const grpc::string& AltsContext::application_protocol() const {
  return application_protocol_;
}

const grpc::string& AltsContext::record_protocol() const {
  return record_protocol_;
}

const grpc::string& AltsContext::peer_service_account() const {
  return peer_service_account_;
}

const grpc::string& AltsContext::local_service_account() const {
  return local_service_account_;
}

grpc_security_level AltsContext::security_level() const {
  return security_level_;
}

const RpcProtocolVersions& AltsContext::peer_rpc_versions() const {
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
  if ((int)grpc_gcp_AltsContext_security_level(ctx) < GRPC_SECURITY_MIN ||
      (int)grpc_gcp_AltsContext_security_level(ctx) > GRPC_SECURITY_MAX) {
    gpr_log(GPR_ERROR, "security_level is invalid.");
    return nullptr;
  }
  return grpc_core::MakeUnique<AltsContext>(AltsContext(ctx));
}

//// checkContext is to check the validity of the grpc_gcp_AltsContext.
//// It will return false if any field inside grpc_gcp_AltsContext is invalid.
// bool checkContext(const grpc_gcp_AltsContext* ctx) {
//  if (ctx == nullptr) {
//    gpr_log(GPR_ERROR, "grpc_gcp_AltsContext is nullptr.");
//    return false;
//  }
//  upb_strview application_protocol =
//      grpc_gcp_AltsContext_application_protocol(ctx);
//  if (application_protocol.data == nullptr || application_protocol.size <= 0)
//  {
//    gpr_log(GPR_ERROR, "application_protocol is invalid.");
//    return false;
//  }
//  upb_strview record_protocol = grpc_gcp_AltsContext_record_protocol(ctx);
//  if (record_protocol.data == nullptr || record_protocol.size <= 0) {
//    gpr_log(GPR_ERROR, "record_protocol is invalid.");
//    return false;
//  }
//  upb_strview peer_service_account =
//      grpc_gcp_AltsContext_peer_service_account(ctx);
//  if (peer_service_account.data == nullptr || peer_service_account.size <= 0)
//  {
//    gpr_log(GPR_ERROR, "peer_service_account is invalid.");
//    return false;
//  }
//  upb_strview local_service_account =
//      grpc_gcp_AltsContext_local_service_account(ctx);
//  if (local_service_account.data == nullptr || local_service_account.size <=
//  0) {
//    gpr_log(GPR_ERROR, "local_service_account is invalid.");
//    return false;
//  }
//  if ((int)grpc_gcp_AltsContext_security_level(ctx) <
//  grpc_security_level.GRPC_SECURITY_MIN ||
//  (int)grpc_gcp_AltsContext_security_level(ctx) >
//  grpc_security_level.GRPC_SECURITY_MAX) {
//    gpr_log(GPR_ERROR, "security_level is invalid.");
//    return false;
//  }
//  return true;
//}

}  // namespace grpc
