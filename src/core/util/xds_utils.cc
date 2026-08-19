//
// Copyright 2026 gRPC authors.
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

#include "src/core/util/xds_utils.h"

#include "envoy/config/core/v3/base.upb.h"
#include "src/core/lib/surface/validate_metadata.h"
#include "src/core/util/upb_utils.h"
#include "upb/mem/arena.h"
#include "absl/strings/match.h"
#include "absl/strings/string_view.h"

namespace grpc_core {

envoy_config_core_v3_HeaderValue* ParseEnvoyHeader(absl::string_view key,
                                                   absl::string_view value,
                                                   upb_Arena* arena) {
  if (key.empty()) return nullptr;
  if (key.size() > 16384) return nullptr;
  if (key == "host") return nullptr;
  if (ValidateHeaderKeyIsLegal(key) != ValidateMetadataResult::kOk) {
    return nullptr;
  }
  if (value.size() > 16384) return nullptr;
  auto* header_value = envoy_config_core_v3_HeaderValue_new(arena);
  envoy_config_core_v3_HeaderValue_set_key(header_value,
                                           StdStringToUpbString(key));
  if (absl::EndsWith(key, "-bin")) {
    envoy_config_core_v3_HeaderValue_set_raw_value(header_value,
                                                   StdStringToUpbString(value));
  } else {
    envoy_config_core_v3_HeaderValue_set_value(header_value,
                                               StdStringToUpbString(value));
  }
  return header_value;
}

}  // namespace grpc_core