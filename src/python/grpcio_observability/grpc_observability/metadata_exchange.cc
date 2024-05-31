//
//
// Copyright 2024 gRPC authors.
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

#include "metadata_exchange.h"

#include <stddef.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <unordered_map>

#include "absl/strings/string_view.h"
#include "constants.h"

#include <grpc/slice.h>

#include "src/core/telemetry/call_tracer.h"

namespace grpc_observability {

PythonLabelsInjector::PythonLabelsInjector(
    const std::vector<Label>& exchange_labels) {
  for (const auto& label : exchange_labels) {
    auto it = MetadataExchangeKeyNames.find(label.key);
    if (it != MetadataExchangeKeyNames.end()) {
      metadata_to_exchange_.emplace_back(label.key, label.value);
    }
  }
}

std::vector<Label> PythonLabelsInjector::GetExchangeLabels(
    grpc_metadata_batch* incoming_initial_metadata) const {
  std::vector<Label> labels;
  for (const auto& key : MetadataExchangeKeyNames) {
    if (key == kXEnvoyPeerMetadata) {
      auto xds_peer_metadata =
          incoming_initial_metadata->Take(grpc_core::XEnvoyPeerMetadata());
      grpc_core::Slice xds_remote_metadata = xds_peer_metadata.has_value()
                                                 ? *std::move(xds_peer_metadata)
                                                 : grpc_core::Slice();
      if (!xds_remote_metadata.empty()) {
        std::string xds_decoded_metadata;
        bool metadata_decoded = absl::Base64Unescape(
            xds_remote_metadata.as_string_view(), &xds_decoded_metadata);
        if (metadata_decoded) {
          labels.emplace_back(kXEnvoyPeerMetadata, xds_decoded_metadata);
        }
      }
    }
  }
  return labels;
}

void PythonLabelsInjector::AddExchangeLabelsToMetadata(
    grpc_metadata_batch* outgoing_initial_metadata) const {
  for (const auto& metadata : metadata_to_exchange_) {
    if (metadata.first == kXEnvoyPeerMetadata) {
      grpc_core::Slice metadata_slice = grpc_core::Slice::FromCopiedString(
          absl::Base64Escape(absl::string_view(metadata.second)));
      outgoing_initial_metadata->Set(grpc_core::XEnvoyPeerMetadata(),
                                     metadata_slice.Ref());
    }
  }
}

void PythonLabelsInjector::AddXdsOptionalLabels(
    bool is_client,
    absl::Span<const grpc_core::RefCountedStringValue> optional_labels_span,
    std::vector<Label>& labels) {
  if (!is_client) {
    // Currently the CSM optional labels are only set on client.
    return;
  }
  // Performs JSON label name format to CSM Observability Metric spec format
  // conversion.
  absl::string_view service_name =
      optional_labels_span[static_cast<size_t>(
                               grpc_core::ClientCallTracer::CallAttemptTracer::
                                   OptionalLabelKey::kXdsServiceName)]
          .as_string_view();
  absl::string_view service_namespace =
      optional_labels_span[static_cast<size_t>(
                               grpc_core::ClientCallTracer::CallAttemptTracer::
                                   OptionalLabelKey::kXdsServiceNamespace)]
          .as_string_view();
  // According to the CSM Observability Metric spec, if the control plane fails
  // to provide these labels, the client will set their values to "unknown".
  if (service_name.empty()) {
    service_name = "unknown";
  }
  if (service_namespace.empty()) {
    service_namespace = "unknown";
  }
  labels.emplace_back("csm.service_name", std::string(service_name));
  labels.emplace_back("csm.service_namespace_name",
                      std::string(service_namespace));
}

}  // namespace grpc_observability
