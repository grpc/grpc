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

#ifndef GRPC_SRC_CORE_EXT_TRANSPORT_CHAOTIC_GOOD_SETTINGS_METADATA_H
#define GRPC_SRC_CORE_EXT_TRANSPORT_CHAOTIC_GOOD_SETTINGS_METADATA_H

#include "absl/types/optional.h"

#include <grpc/support/port_platform.h>

#include "src/core/lib/resource_quota/arena.h"
#include "src/core/lib/transport/metadata_batch.h"

namespace grpc_core {
namespace chaotic_good {

// Captures metadata sent in a chaotic good settings frame.
struct SettingsMetadata {
  enum class ConnectionType {
    kControl,
    kData,
  };
  absl::optional<ConnectionType> connection_type;
  absl::optional<std::string> connection_id;
  absl::optional<uint32_t> alignment;

  Arena::PoolPtr<grpc_metadata_batch> ToMetadataBatch();
  static absl::StatusOr<SettingsMetadata> FromMetadataBatch(
      const grpc_metadata_batch& batch);
};

}  // namespace chaotic_good
}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_EXT_TRANSPORT_CHAOTIC_GOOD_SETTINGS_METADATA_H
