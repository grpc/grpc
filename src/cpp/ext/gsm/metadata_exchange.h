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

#ifndef GRPC_SRC_CPP_EXT_GSM_METADATA_EXCHANGE_H
#define GRPC_SRC_CPP_EXT_GSM_METADATA_EXCHANGE_H

#include <grpc/support/port_platform.h>

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "absl/strings/string_view.h"
#include "opentelemetry/sdk/common/attribute_utils.h"

#include "src/core/lib/slice/slice.h"
#include "src/core/lib/transport/metadata_batch.h"
#include "src/cpp/ext/otel/otel_plugin.h"

namespace grpc {
namespace internal {

class ServiceMeshLabelsInjector : public LabelsInjector {
 public:
  explicit ServiceMeshLabelsInjector(
      const opentelemetry::sdk::common::AttributeMap& map);
  // Read the incoming initial metadata to get the set of labels to be added to
  // metrics.
  std::unique_ptr<LabelsIterable> GetLabels(
      grpc_metadata_batch* incoming_initial_metadata) override;

  // Modify the outgoing initial metadata with metadata information to be sent
  // to the peer.
  void AddLabels(grpc_metadata_batch* outgoing_initial_metadata) override;

 private:
  std::vector<std::pair<absl::string_view, std::string>> local_labels_;
  grpc_core::Slice serialized_labels_to_send_;
};

// Returns the mesh ID by reading and parsing the bootstrap file. Returns
// "unknown" if for some reason, mesh ID could not be figured out.
// EXPOSED FOR TESTING PURPOSES ONLY.
std::string GetMeshId();

}  // namespace internal
}  // namespace grpc

#endif  // GRPC_SRC_CPP_EXT_GSM_METADATA_EXCHANGE_H
