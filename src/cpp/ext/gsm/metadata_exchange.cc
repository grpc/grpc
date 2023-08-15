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

#include <grpc/support/port_platform.h>

#include "src/cpp/ext/gsm/metadata_exchange.h"

#include <stddef.h>

#include "absl/strings/escaping.h"
#include "absl/strings/string_view.h"
#include "google/protobuf/struct.upb.h"
#include "opentelemetry/sdk/resource/semantic_conventions.h"
#include "upb/base/string_view.h"
#include "upb/mem/arena.h"
#include "upb/upb.hpp"

namespace grpc {
namespace internal {

namespace {

upb_StringView AbslStrToUpbStr(absl::string_view str) {
  return upb_StringView_FromDataAndSize(str.data(), str.size());
}

std::string UpbStrToStdStr(upb_StringView str) {
  return std::string(str.data, str.size);
}

void AddStringKeyValueToStructProto(google_protobuf_Struct* struct_pb,
                                    absl::string_view key,
                                    absl::string_view value, upb_Arena* arena) {
  google_protobuf_Value* value_pb = google_protobuf_Value_new(arena);
  google_protobuf_Value_set_string_value(value_pb, AbslStrToUpbStr(value));
  google_protobuf_Struct_fields_set(struct_pb, AbslStrToUpbStr(key), value_pb,
                                    arena);
}

std::string GetStringValueFromAttributeMap(
    const opentelemetry::sdk::common::AttributeMap& map,
    absl::string_view key) {
  return "unknown";
}

}  // namespace

ServiceMeshLabelsInjector::ServiceMeshLabelsInjector(
    const opentelemetry::sdk::common::AttributeMap& map) {
  upb::Arena arena;
  auto* metadata = google_protobuf_Struct_new(arena.ptr());
  // Assume kubernetes for now
  std::string pod_name_value = GetStringValueFromAttributeMap(
      map, opentelemetry::sdk::resource::SemanticConventions::kK8sPodName);
  std::string container_name_value = GetStringValueFromAttributeMap(
      map,
      opentelemetry::sdk::resource::SemanticConventions::kK8sContainerName);
  std::string type_value = GetStringValueFromAttributeMap(map, "NAME");
  std::string namespace_value = GetStringValueFromAttributeMap(
      map,
      opentelemetry::sdk::resource::SemanticConventions::kK8sNamespaceName);
  std::string cluster_name_value = GetStringValueFromAttributeMap(
      map, opentelemetry::sdk::resource::SemanticConventions::kK8sClusterName);
  std::string cluster_location_value = GetStringValueFromAttributeMap(
      map, opentelemetry::sdk::resource::SemanticConventions::kCloudRegion);
  std::string project_id_value = GetStringValueFromAttributeMap(
      map, opentelemetry::sdk::resource::SemanticConventions::kCloudAccountId);
  std::string canonical_service_value =
      GetStringValueFromAttributeMap(map, "NAME");
  // TODO(yashkt): Replace MonitoredResource values
  AddStringKeyValueToStructProto(metadata, "POD_NAME", pod_name_value,
                                 arena.ptr());
  AddStringKeyValueToStructProto(metadata, "CONTAINER_NAME",
                                 container_name_value, arena.ptr());
  AddStringKeyValueToStructProto(metadata, "TYPE", type_value, arena.ptr());
  AddStringKeyValueToStructProto(metadata, "NAMESPACE", namespace_value,
                                 arena.ptr());
  AddStringKeyValueToStructProto(metadata, "CLUSTER_NAME", cluster_name_value,
                                 arena.ptr());
  AddStringKeyValueToStructProto(metadata, "CLUSTER_LOCATION",
                                 cluster_location_value, arena.ptr());
  AddStringKeyValueToStructProto(metadata, "PROJECT_ID", project_id_value,
                                 arena.ptr());
  AddStringKeyValueToStructProto(metadata, "CANONICAL_SERVICE",
                                 canonical_service_value, arena.ptr());
  size_t output_length;
  char* output =
      google_protobuf_Struct_serialize(metadata, arena.ptr(), &output_length);
  serialized_labels_to_send_ = grpc_core::Slice::FromCopiedString(
      absl::Base64Escape(absl::string_view(output, output_length)));
  //   // Fill up local labels map
  //   local_labels_["name"] = pod_name_value;
  //   local_labels_["type"] = type_value;
  //   local_labels_["namespace"] = namespace_value;
  //   local_labels_["cluster_name"] = cluster_name_value;
  //   local_labels_["cluster_location"] = cluster_location_value;
  //   local_labels_["project_id"] = project_id_value;
  //   local_labels_["canonical_service"] = canonical_service_value;
}

std::string GetStringValueFromUpbStruct(google_protobuf_Struct* struct_pb,
                                        absl::string_view key,
                                        upb_Arena* arena) {
  google_protobuf_Value* value_pb = google_protobuf_Value_new(arena);
  bool present = google_protobuf_Struct_fields_get(
      struct_pb, AbslStrToUpbStr(key), &value_pb);
  if (present) {
    if (google_protobuf_Value_has_string_value(value_pb)) {
      return UpbStrToStdStr(google_protobuf_Value_string_value(value_pb));
    }
  }
  return "unknown";
}

absl::flat_hash_map<std::string, std::string>
ServiceMeshLabelsInjector::GetLabels(
    grpc_metadata_batch* incoming_initial_metadata) {
  auto peer_metadata =
      incoming_initial_metadata->Take(grpc_core::XEnvoyPeerMetadata());
  absl::flat_hash_map<std::string, std::string> labels_map = local_labels_;
  if (peer_metadata.has_value()) {
    upb::Arena arena;
    auto* struct_pb = google_protobuf_Struct_parse(
        reinterpret_cast<const char*>(peer_metadata.value().data()),
        peer_metadata.value().size(), arena.ptr());
    absl::flat_hash_map<std::string, std::string> labels_map = local_labels_;
    labels_map.emplace("peer_name", GetStringValueFromUpbStruct(
                                        struct_pb, "NAME", arena.ptr()));
    labels_map.emplace("peer_type", GetStringValueFromUpbStruct(
                                        struct_pb, "TYPE", arena.ptr()));
    labels_map.emplace(
        "peer_namespace",
        GetStringValueFromUpbStruct(struct_pb, "NAMESPACE", arena.ptr()));
    labels_map.emplace(
        "peer_cluster_name",
        GetStringValueFromUpbStruct(struct_pb, "CLUSTER_NAME", arena.ptr()));
    labels_map.emplace("peer_cluster_location",
                       GetStringValueFromUpbStruct(
                           struct_pb, "CLUSTER_LOCATION", arena.ptr()));
    labels_map.emplace(
        "peer_project_id",
        GetStringValueFromUpbStruct(struct_pb, "PROJECT_ID", arena.ptr()));
    labels_map.emplace("peer_canonical_service",
                       GetStringValueFromUpbStruct(
                           struct_pb, "CANONICAL_SERVICE", arena.ptr()));
  } else {
    labels_map.emplace("peer_name", "unknown");
    labels_map.emplace("peer_type", "unknown");
    labels_map.emplace("peer_namespace", "unknown");
    labels_map.emplace("peer_cluster_name", "unknown");
    labels_map.emplace("peer_cluster_location", "unknown");
    labels_map.emplace("peer_project_id", "unknown");
    labels_map.emplace("peer_canonical_service", "unknown");
  }
  return labels_map;
}

absl::flat_hash_map<std::string, std::string>
ServiceMeshLabelsInjector::GetLocalLabels() {
  return local_labels_;
}

void ServiceMeshLabelsInjector::AddLocalLabels(
    grpc_metadata_batch* outgoing_initial_metadata) {
  outgoing_initial_metadata->Set(grpc_core::XEnvoyPeerMetadata(),
                                 serialized_labels_to_send_.Ref());
}

}  // namespace internal
}  // namespace grpc