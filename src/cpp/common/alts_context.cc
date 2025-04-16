//
//
// Copyright 2019 gRPC authors.
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

#include <map>
#include <stddef.h>
#include <string>

#include <grpc/grpc_security_constants.h>
#include <grpcpp/security/alts_context.h>

#include "src/proto/grpc/gcp/altscontext.upb.h"
#include "src/proto/grpc/gcp/transport_security_common.upb.h"

#include "upb/base/string_view.h"
#include "upb/message/map.h"

namespace grpc {
namespace experimental {

// A upb-generated grpc_gcp_AltsContext is passed in to construct an
// AltsContext. Normal users should use GetAltsContextFromAuthContext to get
// AltsContext, instead of constructing their own.
AltsContext::AltsContext(const grpc_gcp_AltsContext* ctx) {
  upb_StringView application_protocol =
      grpc_gcp_AltsContext_application_protocol(ctx);
  if (application_protocol.data != nullptr && application_protocol.size > 0) {
    application_protocol_ =
        std::string(application_protocol.data, application_protocol.size);
  }
  upb_StringView record_protocol = grpc_gcp_AltsContext_record_protocol(ctx);
  if (record_protocol.data != nullptr && record_protocol.size > 0) {
    record_protocol_ = std::string(record_protocol.data, record_protocol.size);
  }
  upb_StringView peer_service_account =
      grpc_gcp_AltsContext_peer_service_account(ctx);
  if (peer_service_account.data != nullptr && peer_service_account.size > 0) {
    peer_service_account_ =
        std::string(peer_service_account.data, peer_service_account.size);
  }
  upb_StringView local_service_account =
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
  if (grpc_gcp_AltsContext_peer_attributes_size(ctx) != 0) {
    // TODO(b/397931390): Clean up the code after gRPC OSS migrates to proto
    // v30.0.
    grpc_gcp_AltsContext* ctx_upb = (grpc_gcp_AltsContext*)ctx;
    const upb_Map* ctx_upb_map =
        _grpc_gcp_AltsContext_peer_attributes_upb_map(ctx_upb);
    if (ctx_upb_map) {
      size_t iter = kUpb_Map_Begin;
      upb_MessageValue k, v;
      while (upb_Map_Next(ctx_upb_map, &k, &v, &iter)) {
        upb_StringView key = k.str_val;
        upb_StringView val = v.str_val;
        peer_attributes_map_[std::string(key.data, key.size)] =
            std::string(val.data, val.size);
      }
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
