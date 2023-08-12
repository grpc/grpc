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

#include "src/cpp/ext/gsm/metadata_exchange.h"

#include <grpc/support/port_platform.h>
#include <stddef.h>

#include "absl/strings/escaping.h"
#include "absl/strings/string_view.h"
#include "google/protobuf/struct.upb.h"
#include "upb/base/string_view.h"
#include "upb/mem/arena.h"
#include "upb/upb.hpp"

namespace grpc {
namespace internal {

namespace {

void AddStringKeyValueToStructProto(google_protobuf_Struct* struct_pb,
                                    absl::string_view key,
                                    absl::string_view value, upb_Arena* arena) {
  google_protobuf_Value* value_pb = google_protobuf_Value_new(arena);
  google_protobuf_Value_set_string_value(
      value_pb, upb_StringView_FromDataAndSize(value.data(), value.size()));
  google_protobuf_Struct_fields_set(
      struct_pb, upb_StringView_FromDataAndSize(key.data(), key.size()),
      value_pb, arena);
}

}  // namespace

ServiceMeshLabelsInjector::ServiceMeshLabelsInjector() {
  upb::Arena arena;
  auto* metadata = google_protobuf_Struct_new(arena.ptr());
  // Replace MonitoredResource values
  AddStringKeyValueToStructProto(metadata, "NAME", "name", arena.ptr());
  AddStringKeyValueToStructProto(metadata, "TYPE", "type", arena.ptr());
  AddStringKeyValueToStructProto(metadata, "NAMESPACE", "namespace",
                                 arena.ptr());
  AddStringKeyValueToStructProto(metadata, "CLUSTER_NAME", "cluster_name",
                                 arena.ptr());
  AddStringKeyValueToStructProto(metadata, "CLUSTER_LOCATION",
                                 "cluster_location", arena.ptr());
  AddStringKeyValueToStructProto(metadata, "PROJECT_ID", "project_id",
                                 arena.ptr());
  AddStringKeyValueToStructProto(metadata, "CANONICAL_SERVICE",
                                 "canonical_service", arena.ptr());
  size_t output_length;
  char* output =
      google_protobuf_Struct_serialize(metadata, arena.ptr(), &output_length);
  serialized_labels_to_send_ = grpc_core::Slice::FromCopiedString(
      absl::Base64Escape(absl::string_view(output, output_length)));
}

absl::flat_hash_map<std::string, std::string>
ServiceMeshLabelsInjector::GetPeerLabels(
    grpc_metadata_batch* incoming_initial_metadata) {
  auto peer_metadata =
      incoming_initial_metadata->Take(grpc_core::XEnvoyPeerMetadata());
  if (peer_metadata.has_value()) {
  }
  return {};
}

void ServiceMeshLabelsInjector::AddLocalLabels(
    grpc_metadata_batch* outgoing_initial_metadata) {
  outgoing_initial_metadata->Set(grpc_core::XEnvoyPeerMetadata(),
                                 serialized_labels_to_send_.Ref());
}

}  // namespace internal
}  // namespace grpc