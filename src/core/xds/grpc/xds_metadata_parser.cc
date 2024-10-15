// Copyright 2018 gRPC authors.
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

#include "src/core/xds/grpc/xds_metadata_parser.h"

#include <memory>
#include <utility>

#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "absl/types/variant.h"
#include "envoy/config/core/v3/address.upb.h"
#include "envoy/config/core/v3/address.upbdefs.h"
#include "envoy/extensions/filters/http/gcp_authn/v3/gcp_authn.upb.h"
#include "envoy/extensions/filters/http/gcp_authn/v3/gcp_authn.upbdefs.h"
#include "src/core/lib/address_utils/sockaddr_utils.h"
#include "src/core/util/env.h"
#include "src/core/util/string.h"
#include "src/core/util/upb_utils.h"
#include "src/core/util/validation_errors.h"
#include "src/core/xds/grpc/xds_cluster_parser.h"
#include "src/core/xds/grpc/xds_common_types.h"
#include "src/core/xds/grpc/xds_common_types_parser.h"
#include "upb/base/string_view.h"
#include "upb/text/encode.h"

namespace grpc_core {

// TODO(roth): Remove this once GCP auth filter support is stable.
bool XdsGcpAuthFilterEnabled() {
  auto value = GetEnv("GRPC_EXPERIMENTAL_XDS_GCP_AUTHENTICATION_FILTER");
  if (!value.has_value()) return false;
  bool parsed_value;
  bool parse_succeeded = gpr_parse_bool_value(value->c_str(), &parsed_value);
  return parse_succeeded && parsed_value;
}

namespace {

std::unique_ptr<XdsMetadataValue> ParseGcpAuthnAudience(
    const XdsResourceType::DecodeContext& context, XdsExtension extension,
    ValidationErrors* errors) {
  absl::string_view* serialized_proto =
      absl::get_if<absl::string_view>(&extension.value);
  if (serialized_proto == nullptr) {
    errors->AddError("could not parse audience metadata");
    return nullptr;
  }
  auto* proto = envoy_extensions_filters_http_gcp_authn_v3_Audience_parse(
      serialized_proto->data(), serialized_proto->size(), context.arena);
  if (proto == nullptr) {
    errors->AddError("could not parse audience metadata");
    return nullptr;
  }
  if (GRPC_TRACE_FLAG_ENABLED_OBJ(*context.tracer) && ABSL_VLOG_IS_ON(2)) {
    const upb_MessageDef* msg_type =
        envoy_extensions_filters_http_gcp_authn_v3_Audience_getmsgdef(
            context.symtab);
    char buf[10240];
    upb_TextEncode(reinterpret_cast<const upb_Message*>(proto), msg_type,
                   nullptr, 0, buf, sizeof(buf));
    VLOG(2) << "[xds_client " << context.client
            << "] cluster metadata Audience: " << buf;
  }
  absl::string_view url = UpbStringToAbsl(
      envoy_extensions_filters_http_gcp_authn_v3_Audience_url(proto));
  if (url.empty()) {
    ValidationErrors::ScopedField field(errors, ".url");
    errors->AddError("must be non-empty");
    return nullptr;
  }
  return std::make_unique<XdsGcpAuthnAudienceMetadataValue>(url);
}

std::unique_ptr<XdsMetadataValue> ParseAddress(
    const XdsResourceType::DecodeContext& context, XdsExtension extension,
    ValidationErrors* errors) {
  absl::string_view* serialized_proto =
      absl::get_if<absl::string_view>(&extension.value);
  if (serialized_proto == nullptr) {
    errors->AddError("could not parse address metadata");
    return nullptr;
  }
  auto* proto = envoy_config_core_v3_Address_parse(
      serialized_proto->data(), serialized_proto->size(), context.arena);
  if (proto == nullptr) {
    errors->AddError("could not parse address metadata");
    return nullptr;
  }
  if (GRPC_TRACE_FLAG_ENABLED_OBJ(*context.tracer) && ABSL_VLOG_IS_ON(2)) {
    const upb_MessageDef* msg_type =
        envoy_config_core_v3_Address_getmsgdef(context.symtab);
    char buf[10240];
    upb_TextEncode(reinterpret_cast<const upb_Message*>(proto), msg_type,
                   nullptr, 0, buf, sizeof(buf));
    VLOG(2) << "[xds_client " << context.client
            << "] cluster metadata Address: " << buf;
  }
  auto addr = ParseXdsAddress(proto, errors);
  if (!addr.has_value()) return nullptr;
  auto addr_uri = grpc_sockaddr_to_string(&*addr, /*normalize=*/false);
  if (!addr_uri.ok()) {
    errors->AddError(addr_uri.status().message());
    return nullptr;
  }
  return std::make_unique<XdsAddressMetadataValue>(std::move(*addr_uri));
}

}  // namespace

XdsMetadataMap ParseXdsMetadataMap(
    const XdsResourceType::DecodeContext& context,
    const envoy_config_core_v3_Metadata* metadata, ValidationErrors* errors) {
  XdsMetadataMap metadata_map;
  if (metadata == nullptr) return metadata_map;  // Not present == empty.
  // First, try typed_filter_metadata.
  size_t iter = kUpb_Map_Begin;
  const envoy_config_core_v3_Metadata_TypedFilterMetadataEntry* typed_entry;
  while (
      (typed_entry = envoy_config_core_v3_Metadata_typed_filter_metadata_next(
           metadata, &iter)) != nullptr) {
    absl::string_view key = UpbStringToAbsl(
        envoy_config_core_v3_Metadata_TypedFilterMetadataEntry_key(
            typed_entry));
    ValidationErrors::ScopedField field(
        errors, absl::StrCat(".typed_filter_metadata[", key, "]"));
    auto extension = ExtractXdsExtension(
        context,
        envoy_config_core_v3_Metadata_TypedFilterMetadataEntry_value(
            typed_entry),
        errors);
    if (!extension.has_value()) continue;
    // TODO(roth): If we start to need a lot of types here, refactor
    // this into a separate registry.
    std::unique_ptr<XdsMetadataValue> metadata_value;
    if (XdsGcpAuthFilterEnabled() &&
        extension->type == XdsGcpAuthnAudienceMetadataValue::Type()) {
      metadata_value =
          ParseGcpAuthnAudience(context, std::move(*extension), errors);
    } else if (XdsHttpConnectEnabled() &&
               extension->type == XdsAddressMetadataValue::Type()) {
      metadata_value = ParseAddress(context, std::move(*extension), errors);
    }
    if (metadata_value != nullptr) {
      metadata_map.Insert(key, std::move(metadata_value));
    }
  }
  // Then, try filter_metadata.
  iter = kUpb_Map_Begin;
  const envoy_config_core_v3_Metadata_FilterMetadataEntry* entry;
  while ((entry = envoy_config_core_v3_Metadata_filter_metadata_next(
              metadata, &iter)) != nullptr) {
    absl::string_view key = UpbStringToAbsl(
        envoy_config_core_v3_Metadata_FilterMetadataEntry_key(entry));
    auto json = ParseProtobufStructToJson(
        context,
        envoy_config_core_v3_Metadata_FilterMetadataEntry_value(entry));
    if (!json.ok()) {
      ValidationErrors::ScopedField field(
          errors, absl::StrCat(".filter_metadata[", key, "]"));
      errors->AddError(json.status().message());
    }
    // Add only if not already added from typed_filter_metadata.
    else if (metadata_map.Find(key) == nullptr) {
      metadata_map.Insert(
          key, std::make_unique<XdsStructMetadataValue>(std::move(*json)));
    }
  }
  return metadata_map;
}

}  // namespace grpc_core
