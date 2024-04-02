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

#include <grpc/support/port_platform.h>

#include "src/core/ext/transport/chaotic_good/settings_metadata.h"

#include "absl/status/status.h"

#include "src/core/lib/gprpp/crash.h"

namespace grpc_core {
namespace chaotic_good {

Arena::PoolPtr<grpc_metadata_batch> SettingsMetadata::ToMetadataBatch() {
  auto md = Arena::MakePooled<grpc_metadata_batch>();
  auto add = [&md](absl::string_view key, std::string value) {
    md->Append(key, Slice::FromCopiedString(value),
               [key, value](absl::string_view error, const Slice&) {
                 Crash(absl::StrCat("Failed to add metadata '", key, "' = '",
                                    value, "': ", error));
               });
  };
  if (connection_type.has_value()) {
    add("chaotic-good-connection-type",
        connection_type.value() == ConnectionType::kControl ? "control"
                                                            : "data");
  }
  if (connection_id.has_value()) {
    add("chaotic-good-connection-id", connection_id.value());
  }
  if (alignment.has_value()) {
    add("chaotic-good-alignment", absl::StrCat(alignment.value()));
  }
  return md;
}

absl::StatusOr<SettingsMetadata> SettingsMetadata::FromMetadataBatch(
    const grpc_metadata_batch& batch) {
  SettingsMetadata md;
  std::string buffer;
  auto v = batch.GetStringValue("chaotic-good-connection-type", &buffer);
  if (v.has_value()) {
    if (*v == "control") {
      md.connection_type = ConnectionType::kControl;
    } else if (*v == "data") {
      md.connection_type = ConnectionType::kData;
    } else {
      return absl::UnavailableError(
          absl::StrCat("Invalid connection type: ", *v));
    }
  }
  v = batch.GetStringValue("chaotic-good-connection-id", &buffer);
  if (v.has_value()) {
    md.connection_id = std::string(*v);
  }
  v = batch.GetStringValue("chaotic-good-alignment", &buffer);
  if (v.has_value()) {
    uint32_t alignment;
    if (!absl::SimpleAtoi(*v, &alignment)) {
      return absl::UnavailableError(absl::StrCat("Invalid alignment: ", *v));
    }
    md.alignment = alignment;
  }
  return md;
}

}  // namespace chaotic_good
}  // namespace grpc_core
