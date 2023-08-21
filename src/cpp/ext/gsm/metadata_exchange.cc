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

const char kMetadataExchangeTypeKey[] = "type";
const char kMetadataExchangePodNameKey[] = "pod_name";
const char kMetadataExchangeContainerNameKey[] = "container_name";
const char kMetadataExchangeNamespaceNameKey[] = "namespace_name";
const char kMetadataExchangeClusterNameKey[] = "cluster_name";
const char kMetadataExchangeLocationKey[] = "location";
const char kMetadataExchangeProjectIdKey[] = "project_id";
const char kMetadataExchangeCanonicalServiceNameKey[] =
    "canonical_service_name";
const char kPeerTypeAttribute[] = "peer_type";
const char kPeerPodNameAttribute[] = "peer_pod_name";
const char kPeerContainerNameAttribute[] = "peer_container_name";
const char kPeerNamespaceNameAttribute[] = "peer_namespace_name";
const char kPeerClusterNameAttribute[] = "peer_cluster_name";
const char kPeerLocationAttribute[] = "peer_location";
const char kPeerProjectIdAttribute[] = "peer_project_id";
const char kPeerCanonicalServiceNameAttribute[] = "peer_canonical_service_name";

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

}  // namespace

ServiceMeshLabelsInjector::ServiceMeshLabelsInjector(
    const opentelemetry::sdk::common::AttributeMap& map) {
  upb::Arena arena;
  auto* metadata = google_protobuf_Struct_new(arena.ptr());
  // Assume kubernetes for now
  std::string type_value = GetStringValueFromAttributeMap(
      map, opentelemetry::sdk::resource::SemanticConventions::kCloudPlatform);
  std::string pod_name_value = GetStringValueFromAttributeMap(
      map, opentelemetry::sdk::resource::SemanticConventions::kK8sPodName);
  std::string container_name_value = GetStringValueFromAttributeMap(
      map,
      opentelemetry::sdk::resource::SemanticConventions::kK8sContainerName);
  std::string namespace_value = GetStringValueFromAttributeMap(
      map,
      opentelemetry::sdk::resource::SemanticConventions::kK8sNamespaceName);
  std::string cluster_name_value = GetStringValueFromAttributeMap(
      map, opentelemetry::sdk::resource::SemanticConventions::kK8sClusterName);
  std::string cluster_location_value = GetStringValueFromAttributeMap(
      map, opentelemetry::sdk::resource::SemanticConventions::
               kCloudRegion);  // if regional
  if (cluster_location_value == "unknown") {
    cluster_location_value = GetStringValueFromAttributeMap(
        map, opentelemetry::sdk::resource::SemanticConventions::
                 kCloudAvailabilityZone);  // if zonal
  }
  std::string project_id_value = GetStringValueFromAttributeMap(
      map, opentelemetry::sdk::resource::SemanticConventions::kCloudAccountId);
  std::string canonical_service_value =
      grpc_core::GetEnv("CANONICAL_SERVICE_NAME").value_or("unknown");
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
  AddStringKeyValueToStructProto(metadata,
                                 kMetadataExchangeCanonicalServiceNameKey,
                                 canonical_service_value, arena.ptr());
  size_t output_length;
  char* output =
      google_protobuf_Struct_serialize(metadata, arena.ptr(), &output_length);
  serialized_labels_to_send_ = grpc_core::Slice::FromCopiedString(
      absl::Base64Escape(absl::string_view(output, output_length)));
  // Fill up local labels map. The rest we get from the detected Resource and
  // from the peer.
  local_labels_.emplace_back("canonical_service_name", canonical_service_value);
}

std::string GetStringValueFromUpbStruct(google_protobuf_Struct* struct_pb,
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
      return UpbStrToStdStr(google_protobuf_Value_string_value(value_pb));
    }
  }
  return "unknown";
}

std::vector<std::pair<std::string, std::string>>
ServiceMeshLabelsInjector::GetPeerLabels(
    grpc_metadata_batch* incoming_initial_metadata) {
  auto peer_metadata =
      incoming_initial_metadata->Take(grpc_core::XEnvoyPeerMetadata());
  std::vector<std::pair<std::string, std::string>> labels;
  bool metadata_found = false;
  std::string decoded_metadata;
  if (peer_metadata.has_value()) {
    metadata_found = absl::Base64Unescape(
        peer_metadata.value().as_string_view(), &decoded_metadata);
  }
  if (metadata_found) {
    upb::Arena arena;
    auto* struct_pb = google_protobuf_Struct_parse(
        decoded_metadata.c_str(), decoded_metadata.size(), arena.ptr());
    labels.emplace_back(kPeerTypeAttribute,
                        GetStringValueFromUpbStruct(
                            struct_pb, kMetadataExchangeTypeKey, arena.ptr()));
    labels.emplace_back(
        kPeerPodNameAttribute,
        GetStringValueFromUpbStruct(struct_pb, kMetadataExchangePodNameKey,
                                    arena.ptr()));
    labels.emplace_back(
        kPeerContainerNameAttribute,
        GetStringValueFromUpbStruct(
            struct_pb, kMetadataExchangeContainerNameKey, arena.ptr()));
    labels.emplace_back(
        kPeerNamespaceNameAttribute,
        GetStringValueFromUpbStruct(
            struct_pb, kMetadataExchangeNamespaceNameKey, arena.ptr()));
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
        kPeerCanonicalServiceNameAttribute,
        GetStringValueFromUpbStruct(
            struct_pb, kMetadataExchangeCanonicalServiceNameKey, arena.ptr()));
  } else {
    labels.emplace_back(kPeerTypeAttribute, "unknown");
    labels.emplace_back(kPeerPodNameAttribute, "unknown");
    labels.emplace_back(kPeerContainerNameAttribute, "unknown");
    labels.emplace_back(kPeerNamespaceNameAttribute, "unknown");
    labels.emplace_back(kPeerClusterNameAttribute, "unknown");
    labels.emplace_back(kPeerLocationAttribute, "unknown");
    labels.emplace_back(kPeerProjectIdAttribute, "unknown");
    labels.emplace_back(kPeerCanonicalServiceNameAttribute, "unknown");
  }
  return labels;
}

std::vector<std::pair<std::string, std::string>>
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
