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

#include <algorithm>
#include <array>
#include <cstdint>
#include <unordered_map>

#include "absl/meta/type_traits.h"
#include "absl/status/statusor.h"
#include "absl/strings/escaping.h"
#include "absl/strings/str_split.h"
#include "absl/strings/string_view.h"
#include "absl/types/optional.h"
#include "absl/types/variant.h"
#include "google/protobuf/struct.upb.h"
#include "opentelemetry/sdk/resource/semantic_conventions.h"
#include "upb/base/string_view.h"
#include "upb/mem/arena.h"
#include "upb/upb.hpp"

#include <grpc/slice.h>

#include "src/core/lib/gprpp/env.h"
#include "src/core/lib/iomgr/error.h"
#include "src/core/lib/iomgr/load_file.h"
#include "src/core/lib/json/json.h"
#include "src/core/lib/json/json_args.h"
#include "src/core/lib/json/json_object_loader.h"
#include "src/core/lib/json/json_reader.h"
#include "src/core/lib/slice/slice_internal.h"

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
// The keys that will be used for the local attributes when recording metrics.
constexpr absl::string_view kMeshIdAttribute = "gsm.mesh_id";
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
// Type values used by Google Cloud Resource Detector
constexpr absl::string_view kGkeType = "gcp_kubernetes_engine";

enum class GcpResourceType : std::uint8_t { kGke, kUnknown };

// A minimal class for helping with the information we need from the xDS
// bootstrap file for GSM Observability reasons.
class XdsBootstrapForGSM {
 public:
  class Node {
   public:
    const std::string& id() const { return id_; }

    static const grpc_core::JsonLoaderInterface* JsonLoader(
        const grpc_core::JsonArgs&) {
      static const auto* loader =
          grpc_core::JsonObjectLoader<Node>().Field("id", &Node::id_).Finish();
      return loader;
    }

   private:
    std::string id_;
  };

  const Node& node() const { return node_; }

  static const grpc_core::JsonLoaderInterface* JsonLoader(
      const grpc_core::JsonArgs&) {
    static const auto* loader =
        grpc_core::JsonObjectLoader<XdsBootstrapForGSM>()
            .Field("node", &XdsBootstrapForGSM::node_)
            .Finish();
    return loader;
  }

 private:
  Node node_;
};

// Returns an empty string if no bootstrap config is found.
std::string GetXdsBootstrapContents() {
  // First, try GRPC_XDS_BOOTSTRAP env var.
  auto path = grpc_core::GetEnv("GRPC_XDS_BOOTSTRAP");
  if (path.has_value()) {
    grpc_slice contents;
    grpc_error_handle error =
        grpc_load_file(path->c_str(), /*add_null_terminator=*/true, &contents);
    if (!error.ok()) return "";
    std::string contents_str(grpc_core::StringViewFromSlice(contents));
    grpc_core::CSliceUnref(contents);
    return contents_str;
  }
  // Next, try GRPC_XDS_BOOTSTRAP_CONFIG env var.
  auto env_config = grpc_core::GetEnv("GRPC_XDS_BOOTSTRAP_CONFIG");
  if (env_config.has_value()) {
    return std::move(*env_config);
  }
  // No bootstrap config found.
  return "";
}

// Returns the mesh ID by reading and parsing the bootstrap file. Returns
// "unknown" if for some reason, mesh ID could not be figured out.
std::string GetMeshId() {
  auto json = grpc_core::JsonParse(GetXdsBootstrapContents());
  if (!json.ok()) {
    return "unknown";
  }
  auto bootstrap = grpc_core::LoadFromJson<XdsBootstrapForGSM>(*json);
  if (!bootstrap.ok()) {
    return "unknown";
  }
  // The format of the Node ID is -
  // projects/[GCP Project number]/networks/[Mesh ID]/nodes/[UUID]
  std::vector<absl::string_view> parts =
      absl::StrSplit(bootstrap->node().id(), '/');
  if (parts.size() != 6) {
    return "unknown";
  }
  return std::string(parts[3]);
}

GcpResourceType StringToGcpResourceType(absl::string_view type) {
  if (type == kGkeType) {
    return GcpResourceType::kGke;
  }
  return GcpResourceType::kUnknown;
}

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

class MeshLabelsIterable : public LabelsIterable {
 public:
  explicit MeshLabelsIterable(
      const std::vector<std::pair<absl::string_view, std::string>>&
          local_labels,
      grpc_core::Slice remote_metadata)
      : local_labels_(local_labels), metadata_(std::move(remote_metadata)) {}

  absl::optional<std::pair<absl::string_view, absl::string_view>> Next()
      override {
    auto& struct_pb = GetDecodedMetadata();
    auto local_labels_size = local_labels_.size();
    if (pos_ < local_labels_size) {
      return local_labels_[pos_++];
    }
    if (struct_pb.struct_pb == nullptr) {
      return absl::nullopt;
    }
    if (++pos_ == local_labels_size + 1) {
      return std::make_pair(kPeerTypeAttribute,
                            GetStringValueFromUpbStruct(
                                struct_pb.struct_pb, kMetadataExchangeTypeKey,
                                struct_pb.arena.ptr()));
    }
    // Only handle GKE type for now.
    switch (type_) {
      case GcpResourceType::kGke:
        if ((pos_ - 2 - local_labels_size) >= kGkeAttributeList.size()) {
          return absl::nullopt;
        }
        return std::make_pair(
            kGkeAttributeList[pos_ - 2 - local_labels_size].otel_attribute,
            GetStringValueFromUpbStruct(
                struct_pb.struct_pb,
                kGkeAttributeList[pos_ - 2 - local_labels_size]
                    .metadata_attribute,
                struct_pb.arena.ptr()));
      case GcpResourceType::kUnknown:
        return absl::nullopt;
    }
  }

  size_t Size() const override {
    auto& struct_pb = GetDecodedMetadata();
    if (struct_pb.struct_pb == nullptr) {
      return local_labels_.size();
    }
    if (type_ != GcpResourceType::kGke) {
      return local_labels_.size() + 1;
    }
    return local_labels_.size() + kGkeAttributeList.size();
  }

  void ResetIteratorPosition() override { pos_ = 0; }

 private:
  struct GkeAttribute {
    absl::string_view otel_attribute;
    absl::string_view metadata_attribute;
  };

  struct StructPb {
    upb::Arena arena;
    google_protobuf_Struct* struct_pb = nullptr;
  };

  static constexpr std::array<GkeAttribute, 7> kGkeAttributeList = {
      GkeAttribute{kPeerPodNameAttribute, kMetadataExchangePodNameKey},
      GkeAttribute{kPeerContainerNameAttribute,
                   kMetadataExchangeContainerNameKey},
      GkeAttribute{kPeerNamespaceNameAttribute,
                   kMetadataExchangeNamespaceNameKey},
      GkeAttribute{kPeerClusterNameAttribute, kMetadataExchangeClusterNameKey},
      GkeAttribute{kPeerLocationAttribute, kMetadataExchangeLocationKey},
      GkeAttribute{kPeerProjectIdAttribute, kMetadataExchangeProjectIdKey},
      GkeAttribute{kPeerCanonicalServiceAttribute,
                   kMetadataExchangeCanonicalServiceKey},
  };

  StructPb& GetDecodedMetadata() const {
    auto* slice = absl::get_if<grpc_core::Slice>(&metadata_);
    if (slice == nullptr) {
      return absl::get<StructPb>(metadata_);
    }
    std::string decoded_metadata;
    bool metadata_decoded =
        absl::Base64Unescape(slice->as_string_view(), &decoded_metadata);
    metadata_ = StructPb{};
    auto& struct_pb = absl::get<StructPb>(metadata_);
    if (metadata_decoded) {
      struct_pb.struct_pb = google_protobuf_Struct_parse(
          decoded_metadata.c_str(), decoded_metadata.size(),
          struct_pb.arena.ptr());
      type_ = StringToGcpResourceType(GetStringValueFromUpbStruct(
          struct_pb.struct_pb, kMetadataExchangeTypeKey,
          struct_pb.arena.ptr()));
    }
    return struct_pb;
  }

  const std::vector<std::pair<absl::string_view, std::string>>& local_labels_;
  // Holds either the metadata slice or the decoded proto struct.
  mutable absl::variant<grpc_core::Slice, StructPb> metadata_;
  mutable GcpResourceType type_;
  uint32_t pos_ = 0;
};

constexpr std::array<MeshLabelsIterable::GkeAttribute, 7>
    MeshLabelsIterable::kGkeAttributeList;

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
  // Only handle GKE for now
  if (type_value == kGkeType) {
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
                                   kMetadataExchangeCanonicalServiceKey,
                                   canonical_service_value, arena.ptr());
  }

  size_t output_length;
  char* output =
      google_protobuf_Struct_serialize(metadata, arena.ptr(), &output_length);
  serialized_labels_to_send_ = grpc_core::Slice::FromCopiedString(
      absl::Base64Escape(absl::string_view(output, output_length)));
  // Fill up local labels map. The rest we get from the detected Resource and
  // from the peer.
  local_labels_.emplace_back(kMeshIdAttribute, GetMeshId());
}

std::unique_ptr<LabelsIterable> ServiceMeshLabelsInjector::GetLabels(
    grpc_metadata_batch* incoming_initial_metadata) {
  auto peer_metadata =
      incoming_initial_metadata->Take(grpc_core::XEnvoyPeerMetadata());
  if (!peer_metadata.has_value()) {
    return nullptr;
  }
  return std::make_unique<MeshLabelsIterable>(local_labels_,
                                              *std::move(peer_metadata));
}

void ServiceMeshLabelsInjector::AddLabels(
    grpc_metadata_batch* outgoing_initial_metadata) {
  outgoing_initial_metadata->Set(grpc_core::XEnvoyPeerMetadata(),
                                 serialized_labels_to_send_.Ref());
}

}  // namespace internal
}  // namespace grpc
