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

#ifndef GRPC_SRC_CORE_UTIL_XDS_UTILS_H
#define GRPC_SRC_CORE_UTIL_XDS_UTILS_H

#include <string>
#include <utility>

#include "envoy/config/core/v3/base.upb.h"
#include "src/core/xds/grpc/xds_common_types.h"
#include "upb/mem/arena.h"
#include "absl/strings/string_view.h"

namespace grpc_core {

// Parses a header value option config protobuf into a HeaderValueOption struct.
HeaderValueOption ParseHeaderValueOption(
    const envoy_config_core_v3_HeaderValueOption* header_value_option_config,
    ValidationErrors* errors);

// Parses a HeaderValue config protobuf into a key-value pair.
std::pair<std::string, std::string> ParseHeader(
    const envoy_config_core_v3_HeaderValue* header_value,
    ValidationErrors* errors);

// Creates an envoy_config_core_v3_HeaderValue from a key and value.
// If the key ends in "-bin", the value is set as raw_value.
// Otherwise, the value is set as value.
envoy_config_core_v3_HeaderValue* ParseEnvoyHeader(absl::string_view key,
                                                   absl::string_view value,
                                                   upb_Arena* arena);

}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_UTIL_XDS_UTILS_H