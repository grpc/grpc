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
#include <grpcpp/security/alts_context.h>

#include "src/core/tsi/alts/handshaker/alts_tsi_handshaker.h"
#include "src/proto/grpc/gcp/altscontext.upb.h"

namespace grpc {
namespace experimental {

// A upb-generated grpc_gcp_AltsContext is passed in to construct an
// AltsContext. Normal users should use GetAltsContextFromAuthContext to get
// AltsContext, instead of constructing their own.
AltsContext::AltsContext(const grpc_gcp_AltsContext* ctx) {
  upb_strview application_protocol =
      grpc_gcp_AltsContext_application_protocol(ctx);
  if (application_protocol.data != nullptr && application_protocol.size > 0) {
    application_protocol_ =
        std::string(application_protocol.data, application_protocol.size);
  }
  upb_strview record_protocol = grpc_gcp_AltsContext_record_protocol(ctx);
  if (record_protocol.data != nullptr && record_protocol.size > 0) {
    record_protocol_ = std::string(record_protocol.data, record_protocol.size);
  }
  upb_strview peer_service_account =
      grpc_gcp_AltsContext_peer_service_account(ctx);
  if (peer_service_account.data != nullptr && peer_service_account.size > 0) {
    peer_service_account_ =
        std::string(peer_service_account.data, peer_service_account.size);
  }
  upb_strview local_service_account =
      grpc_gcp_AltsContext_local_service_account(ctx);
  if (local_service_account.data != nullptr && local_service_account.size > 0) {
    local_service_account_ =
        std::string(local_service_account.data, local_service_account.size);
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
      peer_rpc_versions_.max_rpc_version.major_version = max_version_major;
      peer_rpc_versions_.max_rpc_version.minor_version = max_version_minor;
    }
    const grpc_gcp_RpcProtocolVersions_Version* min_version =
        grpc_gcp_RpcProtocolVersions_min_rpc_version(versions);
    if (min_version != nullptr) {
      int min_version_major =
          grpc_gcp_RpcProtocolVersions_Version_major(min_version);
      int min_version_minor =
          grpc_gcp_RpcProtocolVersions_Version_minor(min_version);
      peer_rpc_versions_.min_rpc_version.major_version = min_version_major;
      peer_rpc_versions_.min_rpc_version.minor_version = min_version_minor;
    }
  }
  if (grpc_gcp_AltsContext_security_level(ctx) >= GRPC_SECURITY_MIN ||
      grpc_gcp_AltsContext_security_level(ctx) <= GRPC_SECURITY_MAX) {
    security_level_ = static_cast<grpc_security_level>(
        grpc_gcp_AltsContext_security_level(ctx));
  }
  if (grpc_gcp_AltsContext_has_peer_attributes(ctx)) {
    size_t iter = UPB_MAP_BEGIN;
    const grpc_gcp_AltsContext_PeerAttributesEntry* peer_attributes_entry =
        grpc_gcp_AltsContext_peer_attributes_next(ctx, &iter);
    while (peer_attributes_entry != nullptr) {
      upb_strview key =
          grpc_gcp_AltsContext_PeerAttributesEntry_key(peer_attributes_entry);
      upb_strview val =
          grpc_gcp_AltsContext_PeerAttributesEntry_value(peer_attributes_entry);
      peer_attributes_map_[std::string(key.data, key.size)] =
          std::string(val.data, val.size);
      peer_attributes_entry =
          grpc_gcp_AltsContext_peer_attributes_next(ctx, &iter);
    }
  }
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

grpc_security_level AltsContext::security_level() const {
  return security_level_;
}

AltsContext::RpcProtocolVersions AltsContext::peer_rpc_versions() const {
  return peer_rpc_versions_;
}

const std::map<std::string, std::string>& AltsContext::peer_attributes() const {
  return peer_attributes_map_;
}

}  // namespace experimental
}  // namespace grpc
