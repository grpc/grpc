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

#ifndef GRPC_SRC_CORE_FILTER_FILTER_UTIL_H
#define GRPC_SRC_CORE_FILTER_FILTER_UTIL_H

#include "envoy/config/core/v3/base.upb.h"
#include "src/core/call/metadata_batch.h"
#include "src/core/util/matchers.h"
#include "upb/mem/arena.h"
#include "absl/types/span.h"

namespace grpc_core {

// Adds the entries of batch to header_map, as envoy.config.core.v3.HeaderValue
// messages allocated on arena.
//
// A header is skipped if its key matches any of disallowed_headers, or if
// allowed_headers is non-empty and its key matches none of them.
//
// Per gRFC A102, the raw_value field is set (never the value field), and the
// values of binary ("-bin") headers are base64-encoded.
//
// The header map may reference memory owned by batch, so batch must outlive
// any use of header_map (e.g., its serialization).
void PopulateHeaderMap(const grpc_metadata_batch& batch,
                       absl::Span<const StringMatcher> allowed_headers,
                       absl::Span<const StringMatcher> disallowed_headers,
                       upb_Arena* arena,
                       envoy_config_core_v3_HeaderMap* header_map);

}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_FILTER_FILTER_UTIL_H
