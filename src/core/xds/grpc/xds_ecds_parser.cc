//
// Copyright 2025 gRPC authors.
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

#include "src/core/xds/grpc/xds_ecds_parser.h"

#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "envoy/config/core/v3/extension.upb.h"
#include "envoy/config/core/v3/extension.upbdefs.h"
#include "src/core/util/upb_utils.h"
#include "src/core/util/validation_errors.h"
#include "src/core/xds/grpc/xds_common_types.h"
#include "src/core/xds/grpc/xds_common_types_parser.h"
#include "src/core/xds/grpc/xds_ecds.h"
#include "src/core/xds/xds_client/xds_resource_type.h"
#include "upb/text/encode.h"

namespace grpc_core {

namespace {

absl::StatusOr<std::shared_ptr<const XdsEcdsResource>> EcdsResourceParse(
    const XdsResourceType::DecodeContext& context,
    const envoy_config_core_v3_TypedExtensionConfig* ecds) {
  // FIXME
  return absl::UnimplementedError("FIXME");
}

void MaybeLogEcds(const XdsResourceType::DecodeContext& context,
                  const envoy_config_core_v3_TypedExtensionConfig* ecds) {
  if (GRPC_TRACE_FLAG_ENABLED(xds_client) && ABSL_VLOG_IS_ON(2)) {
    const upb_MessageDef* msg_type =
        envoy_config_core_v3_TypedExtensionConfig_getmsgdef(context.symtab);
    char buf[10240];
    upb_TextEncode(reinterpret_cast<const upb_Message*>(ecds), msg_type,
                   nullptr, 0, buf, sizeof(buf));
    VLOG(2) << "[xds_client " << context.client << "] ECDS resource: " << buf;
  }
}

}  // namespace

XdsResourceType::DecodeResult XdsEcdsResourceType::Decode(
    const XdsResourceType::DecodeContext& context,
    absl::string_view serialized_resource) const {
  DecodeResult result;
  // Parse serialized proto.
  auto* resource = envoy_config_core_v3_TypedExtensionConfig_parse(
      serialized_resource.data(), serialized_resource.size(), context.arena);
  if (resource == nullptr) {
    result.resource = absl::InvalidArgumentError("Can't parse ECDS resource.");
    return result;
  }
  MaybeLogEcds(context, resource);
  // Validate resource.
  result.name = UpbStringToStdString(
      envoy_config_core_v3_TypedExtensionConfig_name(resource));
  auto ecds = EcdsResourceParse(context, resource);
  if (!ecds.ok()) {
    if (GRPC_TRACE_FLAG_ENABLED(xds_client)) {
      LOG(ERROR) << "[xds_client " << context.client << "] invalid ECDS "
                 << *result.name << ": " << ecds.status();
    }
    result.resource = ecds.status();
  } else {
    if (GRPC_TRACE_FLAG_ENABLED(xds_client)) {
      LOG(INFO) << "[xds_client " << context.client << "] parsed ECDS "
                << *result.name << ": " << (*ecds)->ToString();
    }
    result.resource = std::move(*ecds);
  }
  return result;
}

}  // namespace grpc_core
