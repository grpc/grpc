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

#include <unordered_map>

#include "absl/meta/type_traits.h"
#include "absl/strings/escaping.h"
#include "absl/strings/string_view.h"
#include "absl/types/optional.h"
#include "absl/types/variant.h"
#include "google/protobuf/struct.upb.h"
#include "opentelemetry/sdk/resource/semantic_conventions.h"
#include "upb/base/string_view.h"
#include "upb/mem/arena.h"
#include "upb/upb.hpp"

#include "src/core/lib/gprpp/env.h"

namespace grpc {
namespace internal {

namespace {

// The keys that will be used in the Metadata Exchange between local and remote.
constexpr absl::string_view kMetadataExchangeTypeKey = "type";
constexpr absl::string_view kMetadataExchangePodNameKey = "pod_name";
constexpr absl::string_view kMetadataExchangeContainerNameKey =
    "container_name";
constexpr absl::string_view kMetadataExchangeNamespaceNameKey =
    "namespace_name";
constexpr absl::string_view kMetadataExchangeClusterNameKey = "cluster_name";
constexpr absl::string_view kMetadataExchangeLocationKey = "location";
constexpr absl::string_view kMetadataExchangeProjectIdKey = "project_id";
constexpr absl::string_view kMetadataExchangeCanonicalServiceKey =
    "canonical_service";
// The keys that will be used for the peer attributes when recording metrics.
constexpr absl::string_view kPeerTypeAttribute = "gsm.remote_workload_type";
constexpr absl::string_view kPeerPodNameAttribute =
    "gsm.remote_workload_pod_name";
constexpr absl::string_view kPeerContainerNameAttribute =
    "gsm.remote_workload_container_name";
constexpr absl::string_view kPeerNamespaceNameAttribute =
    "gsm.remote_workload_namespace_name";
constexpr absl::string_view kPeerClusterNameAttribute =
    "gsm.remote_workload_cluster_name";
constexpr absl::string_view kPeerLocationAttribute =
    "gsm.remote_workload_location";
constexpr absl::string_view kPeerProjectIdAttribute =
    "gsm.remote_workload_project_id";
constexpr absl::string_view kPeerCanonicalServiceAttribute =
    "gsm.remote_workload_canonical_service";

upb_StringView AbslStrToUpbStr(absl::string_view str) {
  return upb_StringView_FromDataAndSize(str.data(), str.size());
}

absl::string_view UpbStrToAbslStr(upb_StringView str) {
  return absl::string_view(str.data, str.size);
}

void AddStringKeyValueToStructProto(google_protobuf_Struct* struct_pb,
                                    absl::string_view key,
                                    absl::string_view value, upb_Arena* arena) {
  google_protobuf_Value* value_pb = google_protobuf_Value_new(arena);
  google_protobuf_Value_set_string_value(value_pb, AbslStrToUpbStr(value));
  google_protobuf_Struct_fields_set(struct_pb, AbslStrToUpbStr(key), value_pb,
                                    arena);
}

absl::string_view GetStringValueFromAttributeMap(
    const opentelemetry::sdk::common::AttributeMap& map,
    absl::string_view key) {
  const auto& attributes = map.GetAttributes();
  const auto it = attributes.find(std::string(key));
  if (it == attributes.end()) {
    return "unknown";
  }
  const auto* string_value = absl::get_if<std::string>(&it->second);
  if (string_value == nullptr) {
    return "unknown";
  }
  return *string_value;
}

absl::string_view GetStringValueFromUpbStruct(google_protobuf_Struct* struct_pb,
                                              absl::string_view key,
                                              upb_Arena* arena) {
  if (struct_pb == nullptr) {
    return "unknown";
  }
  google_protobuf_Value* value_pb = google_protobuf_Value_new(arena);
  bool present = google_protobuf_Struct_fields_get(
      struct_pb, AbslStrToUpbStr(key), &value_pb);
  if (present) {
    if (google_protobuf_Value_has_string_value(value_pb)) {
      return UpbStrToAbslStr(google_protobuf_Value_string_value(value_pb));
    }
  }
  return "unknown";
}

}  // namespace

ServiceMeshLabelsInjector::ServiceMeshLabelsInjector(
    const opentelemetry::sdk::common::AttributeMap& map) {
  upb::Arena arena;
  auto* metadata = google_protobuf_Struct_new(arena.ptr());
  // Assume kubernetes for now
  absl::string_view type_value = GetStringValueFromAttributeMap(
      map, opentelemetry::sdk::resource::SemanticConventions::kCloudPlatform);
  absl::string_view pod_name_value = GetStringValueFromAttributeMap(
      map, opentelemetry::sdk::resource::SemanticConventions::kK8sPodName);
  absl::string_view container_name_value = GetStringValueFromAttributeMap(
      map,
      opentelemetry::sdk::resource::SemanticConventions::kK8sContainerName);
  absl::string_view namespace_value = GetStringValueFromAttributeMap(
      map,
      opentelemetry::sdk::resource::SemanticConventions::kK8sNamespaceName);
  absl::string_view cluster_name_value = GetStringValueFromAttributeMap(
      map, opentelemetry::sdk::resource::SemanticConventions::kK8sClusterName);
  absl::string_view cluster_location_value = GetStringValueFromAttributeMap(
      map, opentelemetry::sdk::resource::SemanticConventions::
               kCloudRegion);  // if regional
  if (cluster_location_value == "unknown") {
    cluster_location_value = GetStringValueFromAttributeMap(
        map, opentelemetry::sdk::resource::SemanticConventions::
                 kCloudAvailabilityZone);  // if zonal
  }
  absl::string_view project_id_value = GetStringValueFromAttributeMap(
      map, opentelemetry::sdk::resource::SemanticConventions::kCloudAccountId);
  std::string canonical_service_value =
      grpc_core::GetEnv("GSM_CANONICAL_SERVICE_NAME").value_or("unknown");
  // Create metadata to be sent over wire.
  AddStringKeyValueToStructProto(metadata, kMetadataExchangeTypeKey, type_value,
                                 arena.ptr());
  AddStringKeyValueToStructProto(metadata, kMetadataExchangePodNameKey,
                                 pod_name_value, arena.ptr());
  AddStringKeyValueToStructProto(metadata, kMetadataExchangeContainerNameKey,
                                 container_name_value, arena.ptr());
  AddStringKeyValueToStructProto(metadata, kMetadataExchangeNamespaceNameKey,
                                 namespace_value, arena.ptr());
  AddStringKeyValueToStructProto(metadata, kMetadataExchangeClusterNameKey,
                                 cluster_name_value, arena.ptr());
  AddStringKeyValueToStructProto(metadata, kMetadataExchangeLocationKey,
                                 cluster_location_value, arena.ptr());
  AddStringKeyValueToStructProto(metadata, kMetadataExchangeProjectIdKey,
                                 project_id_value, arena.ptr());
  AddStringKeyValueToStructProto(metadata, kMetadataExchangeCanonicalServiceKey,
                                 canonical_service_value, arena.ptr());
  size_t output_length;
  char* output =
      google_protobuf_Struct_serialize(metadata, arena.ptr(), &output_length);
  serialized_labels_to_send_ = grpc_core::Slice::FromCopiedString(
      absl::Base64Escape(absl::string_view(output, output_length)));
  // Fill up local labels map. The rest we get from the detected Resource and
  // from the peer.
  // TODO(yashykt): Add mesh_id
}

std::vector<std::pair<std::string, std::string>>
ServiceMeshLabelsInjector::GetPeerLabels(
    grpc_metadata_batch* incoming_initial_metadata) {
  auto remote_metadata =
      incoming_initial_metadata->Take(grpc_core::XEnvoyPeerMetadata());
  std::vector<std::pair<std::string, std::string>> labels;
  upb::Arena arena;
  google_protobuf_Struct* struct_pb = nullptr;
  if (remote_metadata.has_value()) {
    std::string decoded_metadata;
    bool metadata_decoded = absl::Base64Unescape(
        remote_metadata.value().as_string_view(), &decoded_metadata);
    if (metadata_decoded) {
      struct_pb = google_protobuf_Struct_parse(
          decoded_metadata.c_str(), decoded_metadata.size(), arena.ptr());
    }
  }
  labels.emplace_back(kPeerTypeAttribute,
                      GetStringValueFromUpbStruct(
                          struct_pb, kMetadataExchangeTypeKey, arena.ptr()));
  labels.emplace_back(kPeerPodNameAttribute,
                      GetStringValueFromUpbStruct(
                          struct_pb, kMetadataExchangePodNameKey, arena.ptr()));
  labels.emplace_back(
      kPeerContainerNameAttribute,
      GetStringValueFromUpbStruct(struct_pb, kMetadataExchangeContainerNameKey,
                                  arena.ptr()));
  labels.emplace_back(
      kPeerNamespaceNameAttribute,
      GetStringValueFromUpbStruct(struct_pb, kMetadataExchangeNamespaceNameKey,
                                  arena.ptr()));
  labels.emplace_back(
      kPeerClusterNameAttribute,
      GetStringValueFromUpbStruct(struct_pb, kMetadataExchangeClusterNameKey,
                                  arena.ptr()));
  labels.emplace_back(
      kPeerLocationAttribute,
      GetStringValueFromUpbStruct(struct_pb, kMetadataExchangeLocationKey,
                                  arena.ptr()));
  labels.emplace_back(
      kPeerProjectIdAttribute,
      GetStringValueFromUpbStruct(struct_pb, kMetadataExchangeProjectIdKey,
                                  arena.ptr()));
  labels.emplace_back(
      kPeerCanonicalServiceAttribute,
      GetStringValueFromUpbStruct(
          struct_pb, kMetadataExchangeCanonicalServiceKey, arena.ptr()));
  return labels;
}

std::vector<std::pair<absl::string_view, absl::string_view>>
ServiceMeshLabelsInjector::GetLocalLabels() {
  return local_labels_;
}

void ServiceMeshLabelsInjector::AddLabels(
    grpc_metadata_batch* outgoing_initial_metadata) {
  outgoing_initial_metadata->Set(grpc_core::XEnvoyPeerMetadata(),
                                 serialized_labels_to_send_.Ref());
}

}  // namespace internal
}  // namespace grpc
