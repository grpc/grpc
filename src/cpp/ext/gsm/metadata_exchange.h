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
#include <string>

#include "absl/container/flat_hash_map.h"
#include "src/core/lib/transport/metadata_batch.h"
#include "src/cpp/ext/otel/otel_plugin.h"
#include "src/core/lib/slice/slice.h"

namespace grpc {
namespace internal {

class ServiceMeshLabelsInjector : public LabelsInjector {
 public:
  ServiceMeshLabelsInjector();
  // Read the incoming initial metadata to get the set of labels to be added to
  // metrics.
  absl::flat_hash_map<std::string, std::string> GetPeerLabels(
      grpc_metadata_batch* incoming_initial_metadata) override;

  // Modify the outgoing initial metadata with metadata information to be sent
  // to the peer.
  void AddLocalLabels(grpc_metadata_batch* outgoing_initial_metadata) override;

 private:
  grpc_core::Slice serialized_labels_to_send_;
};

}  // namespace internal
}  // namespace grpc

#endif  // GRPC_SRC_CPP_EXT_GSM_METADATA_EXCHANGE_H
