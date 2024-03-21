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

#include "src/cpp/ext/csm/metadata_exchange.h"

#include <stddef.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <unordered_map>

#include "absl/status/statusor.h"
#include "absl/strings/escaping.h"
#include "absl/strings/str_split.h"
#include "absl/strings/string_view.h"
#include "absl/strings/strip.h"
#include "absl/types/optional.h"
#include "absl/types/variant.h"
#include "google/protobuf/struct.upb.h"
#include "opentelemetry/sdk/resource/semantic_conventions.h"
#include "upb/base/string_view.h"
#include "upb/mem/arena.hpp"

#include <grpc/slice.h>

#include "src/core/lib/channel/call_tracer.h"
#include "src/core/lib/gprpp/env.h"
#include "src/core/lib/gprpp/load_file.h"
#include "src/core/lib/iomgr/error.h"
#include "src/core/lib/json/json_args.h"
#include "src/core/lib/json/json_object_loader.h"
#include "src/core/lib/json/json_reader.h"
#include "src/core/lib/slice/slice_internal.h"
#include "src/cpp/ext/otel/key_value_iterable.h"

namespace grpc {
namespace internal {

using OptionalLabelComponent =
    grpc_core::ClientCallTracer::CallAttemptTracer::OptionalLabelComponent;

namespace {

// The keys that will be used in the Metadata Exchange between local and remote.
constexpr absl::string_view kMetadataExchangeTypeKey = "type";
constexpr absl::string_view kMetadataExchangeWorkloadNameKey = "workload_name";
constexpr absl::string_view kMetadataExchangeNamespaceNameKey =
    "namespace_name";
constexpr absl::string_view kMetadataExchangeClusterNameKey = "cluster_name";
constexpr absl::string_view kMetadataExchangeLocationKey = "location";
constexpr absl::string_view kMetadataExchangeProjectIdKey = "project_id";
constexpr absl::string_view kMetadataExchangeCanonicalServiceKey =
    "canonical_service";
// The keys that will be used for the local attributes when recording metrics.
constexpr absl::string_view kCanonicalServiceAttribute =
    "csm.workload_canonical_service";
constexpr absl::string_view kMeshIdAttribute = "csm.mesh_id";
// The keys that will be used for the peer attributes when recording metrics.
constexpr absl::string_view kPeerTypeAttribute = "csm.remote_workload_type";
constexpr absl::string_view kPeerWorkloadNameAttribute =
    "csm.remote_workload_name";
constexpr absl::string_view kPeerNamespaceNameAttribute =
    "csm.remote_workload_namespace_name";
constexpr absl::string_view kPeerClusterNameAttribute =
    "csm.remote_workload_cluster_name";
constexpr absl::string_view kPeerLocationAttribute =
    "csm.remote_workload_location";
constexpr absl::string_view kPeerProjectIdAttribute =
    "csm.remote_workload_project_id";
constexpr absl::string_view kPeerCanonicalServiceAttribute =
    "csm.remote_workload_canonical_service";
// Type values used by Google Cloud Resource Detector
constexpr absl::string_view kGkeType = "gcp_kubernetes_engine";
constexpr absl::string_view kGceType = "gcp_compute_engine";

enum class GcpResourceType : std::uint8_t { kGke, kGce, kUnknown };

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
    auto contents = grpc_core::LoadFile(*path, /*add_null_terminator=*/true);
    if (!contents.ok()) return "";
    return std::string(contents->as_string_view());
  }
  // Next, try GRPC_XDS_BOOTSTRAP_CONFIG env var.
  auto env_config = grpc_core::GetEnv("GRPC_XDS_BOOTSTRAP_CONFIG");
  if (env_config.has_value()) {
    return std::move(*env_config);
  }
  // No bootstrap config found.
  return "";
}

GcpResourceType StringToGcpResourceType(absl::string_view type) {
  if (type == kGkeType) {
    return GcpResourceType::kGke;
  } else if (type == kGceType) {
    return GcpResourceType::kGce;
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
    size_t local_labels_size = local_labels_.size();
    if (pos_ < local_labels_size) {
      return local_labels_[pos_++];
    }
    const size_t fixed_attribute_end =
        local_labels_size + kFixedAttributes.size();
    if (pos_ < fixed_attribute_end) {
      return NextFromAttributeList(struct_pb, kFixedAttributes,
                                   local_labels_size);
    }
    return NextFromAttributeList(struct_pb, GetAttributesForType(remote_type_),
                                 fixed_attribute_end);
  }

  size_t Size() const override {
    return local_labels_.size() + kFixedAttributes.size() +
           GetAttributesForType(remote_type_).size();
  }

  void ResetIteratorPosition() override { pos_ = 0; }

  // Returns true if the peer sent a non-empty base64 encoded
  // "x-envoy-peer-metadata" metadata.
  bool GotRemoteLabels() const {
    return GetDecodedMetadata().struct_pb != nullptr;
  }

 private:
  struct RemoteAttribute {
    absl::string_view otel_attribute;
    absl::string_view metadata_attribute;
  };

  struct StructPb {
    upb::Arena arena;
    google_protobuf_Struct* struct_pb = nullptr;
  };

  static constexpr std::array<RemoteAttribute, 2> kFixedAttributes = {
      RemoteAttribute{kPeerTypeAttribute, kMetadataExchangeTypeKey},
      RemoteAttribute{kPeerCanonicalServiceAttribute,
                      kMetadataExchangeCanonicalServiceKey},
  };

  static constexpr std::array<RemoteAttribute, 5> kGkeAttributeList = {
      RemoteAttribute{kPeerWorkloadNameAttribute,
                      kMetadataExchangeWorkloadNameKey},
      RemoteAttribute{kPeerNamespaceNameAttribute,
                      kMetadataExchangeNamespaceNameKey},
      RemoteAttribute{kPeerClusterNameAttribute,
                      kMetadataExchangeClusterNameKey},
      RemoteAttribute{kPeerLocationAttribute, kMetadataExchangeLocationKey},
      RemoteAttribute{kPeerProjectIdAttribute, kMetadataExchangeProjectIdKey},
  };
  static constexpr std::array<RemoteAttribute, 3> kGceAttributeList = {
      RemoteAttribute{kPeerWorkloadNameAttribute,
                      kMetadataExchangeWorkloadNameKey},
      RemoteAttribute{kPeerLocationAttribute, kMetadataExchangeLocationKey},
      RemoteAttribute{kPeerProjectIdAttribute, kMetadataExchangeProjectIdKey},
  };

  static absl::Span<const RemoteAttribute> GetAttributesForType(
      GcpResourceType remote_type) {
    switch (remote_type) {
      case GcpResourceType::kGke:
        return kGkeAttributeList;
      case GcpResourceType::kGce:
        return kGceAttributeList;
      default:
        return {};
    }
  }

  absl::optional<std::pair<absl::string_view, absl::string_view>>
  NextFromAttributeList(const StructPb& struct_pb,
                        absl::Span<const RemoteAttribute> attributes,
                        size_t start_index) {
    GPR_DEBUG_ASSERT(pos_ >= start_index);
    const size_t index = pos_ - start_index;
    if (index >= attributes.size()) return absl::nullopt;
    ++pos_;
    const auto& attribute = attributes[index];
    return std::make_pair(attribute.otel_attribute,
                          GetStringValueFromUpbStruct(
                              struct_pb.struct_pb, attribute.metadata_attribute,
                              struct_pb.arena.ptr()));
  }

  StructPb& GetDecodedMetadata() const {
    auto* slice = absl::get_if<grpc_core::Slice>(&metadata_);
    if (slice == nullptr) {
      return absl::get<StructPb>(metadata_);
    }
    // Treat an empty slice as an invalid metadata value.
    if (slice->empty()) {
      metadata_ = StructPb{};
      auto& struct_pb = absl::get<StructPb>(metadata_);
      return struct_pb;
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
      remote_type_ = StringToGcpResourceType(GetStringValueFromUpbStruct(
          struct_pb.struct_pb, kMetadataExchangeTypeKey,
          struct_pb.arena.ptr()));
    }
    return struct_pb;
  }

  const std::vector<std::pair<absl::string_view, std::string>>& local_labels_;
  // Holds either the metadata slice or the decoded proto struct.
  mutable absl::variant<grpc_core::Slice, StructPb> metadata_;
  mutable GcpResourceType remote_type_ = GcpResourceType::kUnknown;
  uint32_t pos_ = 0;
};

constexpr std::array<MeshLabelsIterable::RemoteAttribute, 2>
    MeshLabelsIterable::kFixedAttributes;
constexpr std::array<MeshLabelsIterable::RemoteAttribute, 5>
    MeshLabelsIterable::kGkeAttributeList;
constexpr std::array<MeshLabelsIterable::RemoteAttribute, 3>
    MeshLabelsIterable::kGceAttributeList;

}  // namespace

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
  // projects/[GCP Project number]/networks/mesh:[Mesh ID]/nodes/[UUID]
  std::vector<absl::string_view> parts =
      absl::StrSplit(bootstrap->node().id(), '/');
  if (parts.size() != 6) {
    return "unknown";
  }
  absl::string_view mesh_id = parts[3];
  if (!absl::ConsumePrefix(&mesh_id, "mesh:")) {
    return "unknown";
  }
  return std::string(mesh_id);
}

ServiceMeshLabelsInjector::ServiceMeshLabelsInjector(
    const opentelemetry::sdk::common::AttributeMap& map) {
  upb::Arena arena;
  auto* metadata = google_protobuf_Struct_new(arena.ptr());
  // Assume kubernetes for now
  absl::string_view type_value = GetStringValueFromAttributeMap(
      map, opentelemetry::sdk::resource::SemanticConventions::kCloudPlatform);
  std::string workload_name_value =
      grpc_core::GetEnv("CSM_WORKLOAD_NAME").value_or("unknown");
  absl::string_view namespace_value = GetStringValueFromAttributeMap(
      map,
      opentelemetry::sdk::resource::SemanticConventions::kK8sNamespaceName);
  absl::string_view cluster_name_value = GetStringValueFromAttributeMap(
      map, opentelemetry::sdk::resource::SemanticConventions::kK8sClusterName);
  absl::string_view location_value = GetStringValueFromAttributeMap(
      map, opentelemetry::sdk::resource::SemanticConventions::
               kCloudAvailabilityZone);  // if zonal
  if (location_value == "unknown") {
    location_value = GetStringValueFromAttributeMap(
        map, opentelemetry::sdk::resource::SemanticConventions::
                 kCloudRegion);  // if regional
  }
  absl::string_view project_id_value = GetStringValueFromAttributeMap(
      map, opentelemetry::sdk::resource::SemanticConventions::kCloudAccountId);
  std::string canonical_service_value =
      grpc_core::GetEnv("CSM_CANONICAL_SERVICE_NAME").value_or("unknown");
  // Create metadata to be sent over wire.
  AddStringKeyValueToStructProto(metadata, kMetadataExchangeTypeKey, type_value,
                                 arena.ptr());
  AddStringKeyValueToStructProto(metadata, kMetadataExchangeCanonicalServiceKey,
                                 canonical_service_value, arena.ptr());
  if (type_value == kGkeType) {
    AddStringKeyValueToStructProto(metadata, kMetadataExchangeWorkloadNameKey,
                                   workload_name_value, arena.ptr());
    AddStringKeyValueToStructProto(metadata, kMetadataExchangeNamespaceNameKey,
                                   namespace_value, arena.ptr());
    AddStringKeyValueToStructProto(metadata, kMetadataExchangeClusterNameKey,
                                   cluster_name_value, arena.ptr());
    AddStringKeyValueToStructProto(metadata, kMetadataExchangeLocationKey,
                                   location_value, arena.ptr());
    AddStringKeyValueToStructProto(metadata, kMetadataExchangeProjectIdKey,
                                   project_id_value, arena.ptr());
  } else if (type_value == kGceType) {
    AddStringKeyValueToStructProto(metadata, kMetadataExchangeWorkloadNameKey,
                                   workload_name_value, arena.ptr());
    AddStringKeyValueToStructProto(metadata, kMetadataExchangeLocationKey,
                                   location_value, arena.ptr());
    AddStringKeyValueToStructProto(metadata, kMetadataExchangeProjectIdKey,
                                   project_id_value, arena.ptr());
  }

  size_t output_length;
  char* output =
      google_protobuf_Struct_serialize(metadata, arena.ptr(), &output_length);
  serialized_labels_to_send_ = grpc_core::Slice::FromCopiedString(
      absl::Base64Escape(absl::string_view(output, output_length)));
  // Fill up local labels map. The rest we get from the detected Resource and
  // from the peer.
  local_labels_.emplace_back(kCanonicalServiceAttribute,
                             canonical_service_value);
  local_labels_.emplace_back(kMeshIdAttribute, GetMeshId());
}

std::unique_ptr<LabelsIterable> ServiceMeshLabelsInjector::GetLabels(
    grpc_metadata_batch* incoming_initial_metadata) const {
  auto peer_metadata =
      incoming_initial_metadata->Take(grpc_core::XEnvoyPeerMetadata());
  return std::make_unique<MeshLabelsIterable>(
      local_labels_, peer_metadata.has_value() ? *std::move(peer_metadata)
                                               : grpc_core::Slice());
}

void ServiceMeshLabelsInjector::AddLabels(
    grpc_metadata_batch* outgoing_initial_metadata,
    LabelsIterable* labels_from_incoming_metadata) const {
  // On the server, if the labels from incoming metadata did not have a
  // non-empty base64 encoded "x-envoy-peer-metadata", do not perform metadata
  // exchange.
  if (labels_from_incoming_metadata != nullptr &&
      !static_cast<MeshLabelsIterable*>(labels_from_incoming_metadata)
           ->GotRemoteLabels()) {
    return;
  }
  outgoing_initial_metadata->Set(grpc_core::XEnvoyPeerMetadata(),
                                 serialized_labels_to_send_.Ref());
}

bool ServiceMeshLabelsInjector::AddOptionalLabels(
    bool is_client,
    absl::Span<const std::shared_ptr<std::map<std::string, std::string>>>
        optional_labels_span,
    opentelemetry::nostd::function_ref<
        bool(opentelemetry::nostd::string_view,
             opentelemetry::common::AttributeValue)>
        callback) const {
  if (!is_client) {
    // Currently the CSM optional labels are only set on client.
    return true;
  }
  // According to the CSM Observability Metric spec, if the control plane fails
  // to provide these labels, the client will set their values to "unknown".
  // These default values are set below.
  absl::string_view service_name = "unknown";
  absl::string_view service_namespace = "unknown";
  // Performs JSON label name format to CSM Observability Metric spec format
  // conversion.
  if (optional_labels_span.size() >
      static_cast<size_t>(OptionalLabelComponent::kXdsServiceLabels)) {
    const auto& optional_labels = optional_labels_span[static_cast<size_t>(
        OptionalLabelComponent::kXdsServiceLabels)];
    if (optional_labels != nullptr) {
      auto it = optional_labels->find("service_name");
      if (it != optional_labels->end()) service_name = it->second;
      it = optional_labels->find("service_namespace");
      if (it != optional_labels->end()) service_namespace = it->second;
    }
  }
  return callback("csm.service_name",
                  AbslStrViewToOpenTelemetryStrView(service_name)) &&
         callback("csm.service_namespace_name",
                  AbslStrViewToOpenTelemetryStrView(service_namespace));
}

size_t ServiceMeshLabelsInjector::GetOptionalLabelsSize(
    bool is_client,
    absl::Span<const std::shared_ptr<std::map<std::string, std::string>>>)
    const {
  return is_client ? 2 : 0;
}

}  // namespace internal
}  // namespace grpc
