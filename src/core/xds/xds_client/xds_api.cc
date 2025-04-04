//
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

#include "src/core/xds/xds_client/xds_api.h"

#include "google/protobuf/struct.upb.h"
#include "src/core/util/json/json.h"
#include "src/core/util/upb_utils.h"
#include "upb/base/string_view.h"
#include "upb/mem/arena.hpp"

namespace grpc_core {

namespace {

void PopulateMetadataValue(google_protobuf_Value* value_pb, const Json& value,
                           upb_Arena* arena);

void PopulateListValue(google_protobuf_ListValue* list_value,
                       const Json::Array& values, upb_Arena* arena) {
  for (const auto& value : values) {
    auto* value_pb = google_protobuf_ListValue_add_values(list_value, arena);
    PopulateMetadataValue(value_pb, value, arena);
  }
}

void PopulateMetadata(google_protobuf_Struct* metadata_pb,
                      const Json::Object& metadata, upb_Arena* arena) {
  for (const auto& [key, value] : metadata) {
    google_protobuf_Value* value_proto = google_protobuf_Value_new(arena);
    PopulateMetadataValue(value_proto, value, arena);
    google_protobuf_Struct_fields_set(metadata_pb, StdStringToUpbString(key),
                                      value_proto, arena);
  }
}

void PopulateMetadataValue(google_protobuf_Value* value_pb, const Json& value,
                           upb_Arena* arena) {
  switch (value.type()) {
    case Json::Type::kNull:
      google_protobuf_Value_set_null_value(value_pb, 0);
      break;
    case Json::Type::kNumber:
      google_protobuf_Value_set_number_value(
          value_pb, strtod(value.string().c_str(), nullptr));
      break;
    case Json::Type::kString:
      google_protobuf_Value_set_string_value(
          value_pb, StdStringToUpbString(value.string()));
      break;
    case Json::Type::kBoolean:
      google_protobuf_Value_set_bool_value(value_pb, value.boolean());
      break;
    case Json::Type::kObject: {
      google_protobuf_Struct* struct_value =
          google_protobuf_Value_mutable_struct_value(value_pb, arena);
      PopulateMetadata(struct_value, value.object(), arena);
      break;
    }
    case Json::Type::kArray: {
      google_protobuf_ListValue* list_value =
          google_protobuf_Value_mutable_list_value(value_pb, arena);
      PopulateListValue(list_value, value.array(), arena);
      break;
    }
  }
}

}  // namespace

void PopulateXdsNode(const XdsBootstrap::Node* node,
                     absl::string_view user_agent_name,
                     absl::string_view user_agent_version,
                     envoy_config_core_v3_Node* node_msg, upb_Arena* arena) {
  if (node != nullptr) {
    if (!node->id().empty()) {
      envoy_config_core_v3_Node_set_id(node_msg,
                                       StdStringToUpbString(node->id()));
    }
    if (!node->cluster().empty()) {
      envoy_config_core_v3_Node_set_cluster(
          node_msg, StdStringToUpbString(node->cluster()));
    }
    if (!node->metadata().empty()) {
      google_protobuf_Struct* metadata =
          envoy_config_core_v3_Node_mutable_metadata(node_msg, arena);
      PopulateMetadata(metadata, node->metadata(), arena);
    }
    if (!node->locality_region().empty() || !node->locality_zone().empty() ||
        !node->locality_sub_zone().empty()) {
      envoy_config_core_v3_Locality* locality =
          envoy_config_core_v3_Node_mutable_locality(node_msg, arena);
      if (!node->locality_region().empty()) {
        envoy_config_core_v3_Locality_set_region(
            locality, StdStringToUpbString(node->locality_region()));
      }
      if (!node->locality_zone().empty()) {
        envoy_config_core_v3_Locality_set_zone(
            locality, StdStringToUpbString(node->locality_zone()));
      }
      if (!node->locality_sub_zone().empty()) {
        envoy_config_core_v3_Locality_set_sub_zone(
            locality, StdStringToUpbString(node->locality_sub_zone()));
      }
    }
  }
  envoy_config_core_v3_Node_set_user_agent_name(
      node_msg, StdStringToUpbString(user_agent_name));
  envoy_config_core_v3_Node_set_user_agent_version(
      node_msg, StdStringToUpbString(user_agent_version));
  envoy_config_core_v3_Node_add_client_features(
      node_msg,
      upb_StringView_FromString("envoy.lb.does_not_support_overprovisioning"),
      arena);
}

}  // namespace grpc_core
