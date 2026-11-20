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

#include "src/core/xds/grpc/xds_header_map.h"

#include <string>

#include "envoy/config/core/v3/base.upb.h"
#include "src/core/call/metadata_batch.h"
#include "src/core/lib/slice/slice.h"
#include "src/core/util/matchers.h"
#include "src/core/util/upb_utils.h"
#include "upb/base/string_view.h"
#include "upb/mem/arena.h"
#include "absl/base/attributes.h"
#include "absl/strings/escaping.h"
#include "absl/strings/match.h"
#include "absl/strings/string_view.h"
#include "absl/types/span.h"

namespace grpc_core {

namespace {

// An encoder used with grpc_metadata_batch::Encode() to add the metadata
// entries of a batch to a upb envoy.config.core.v3.HeaderMap message.
class UpbHeaderMapEncoder {
 public:
  UpbHeaderMapEncoder(envoy_config_core_v3_HeaderMap* header_map,
                      upb_Arena* arena,
                      absl::Span<const StringMatcher> allowed_headers,
                      absl::Span<const StringMatcher> disallowed_headers)
      : header_map_(header_map),
        arena_(arena),
        allowed_headers_(allowed_headers),
        disallowed_headers_(disallowed_headers) {}

  void Encode(const Slice& key, const Slice& value) {
    // The slices are owned by the metadata batch, which outlives the use of
    // the header map, so their contents do not need to be copied.
    Append(key.as_string_view(), value.as_string_view(),
           /*copy_value=*/false);
  }

  template <typename Which>
  void Encode(Which, const typename Which::ValueType& value) {
    // Which::Encode() returns a temporary Slice, and for inlined slices the
    // bytes live inside that temporary, so they must be copied onto the arena.
    Append(Which::key(), Which::Encode(value).as_string_view(),
           /*copy_value=*/true);
  }

 private:
  ABSL_ATTRIBUTE_NOINLINE static bool HeaderInMatcher(
      absl::string_view key, absl::Span<const StringMatcher> matchers) {
    for (const auto& matcher : matchers) {
      if (matcher.Match(key)) return true;
    }
    return false;
  }

  ABSL_ATTRIBUTE_NOINLINE bool ShouldForwardHeader(
      absl::string_view key) const {
    if (HeaderInMatcher(key, disallowed_headers_)) return false;
    return allowed_headers_.empty() || HeaderInMatcher(key, allowed_headers_);
  }

  ABSL_ATTRIBUTE_NOINLINE void Append(absl::string_view key,
                                      absl::string_view value,
                                      bool copy_value) {
    if (!ShouldForwardHeader(key)) return;
    auto* header =
        envoy_config_core_v3_HeaderMap_add_headers(header_map_, arena_);
    // Keys are either owned by the metadata batch or static strings, so they
    // do not need to be copied.
    envoy_config_core_v3_HeaderValue_set_key(header, StdStringToUpbString(key));
    // Per gRFC A102, when writing, we always set the raw_value field and never
    // the value field, and the values of binary headers are base64-encoded.
    upb_StringView raw_value;
    if (absl::EndsWith(key, "-bin")) {
      raw_value = CopyStdStringToUpbString(absl::Base64Escape(value), arena_);
    } else if (copy_value) {
      raw_value = CopyStdStringToUpbString(value, arena_);
    } else {
      raw_value = StdStringToUpbString(value);
    }
    envoy_config_core_v3_HeaderValue_set_raw_value(header, raw_value);
  }

  envoy_config_core_v3_HeaderMap* header_map_;
  upb_Arena* arena_;
  absl::Span<const StringMatcher> allowed_headers_;
  absl::Span<const StringMatcher> disallowed_headers_;
};

}  // namespace

void PopulateHeaderMap(const grpc_metadata_batch& batch,
                       absl::Span<const StringMatcher> allowed_headers,
                       absl::Span<const StringMatcher> disallowed_headers,
                       upb_Arena* arena,
                       envoy_config_core_v3_HeaderMap* header_map) {
  UpbHeaderMapEncoder encoder(header_map, arena, allowed_headers,
                              disallowed_headers);
  batch.Encode(&encoder);
}

}  // namespace grpc_core
