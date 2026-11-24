// Copyright 2025 gRPC authors.
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

#ifndef GRPC_SRC_CORE_XDS_GRPC_XDS_MATCHER_CONTEXT_H
#define GRPC_SRC_CORE_XDS_GRPC_XDS_MATCHER_CONTEXT_H

#include <optional>
#include <string>

#include "src/core/call/metadata_batch.h"
#include "src/core/xds/grpc/xds_matcher.h"
#include "absl/functional/function_ref.h"
#include "absl/strings/string_view.h"

namespace grpc_core {

class RpcMatchContext : public XdsMatcher::MatchContext {
 public:
  explicit RpcMatchContext(grpc_metadata_batch* initial_metadata)
      : initial_metadata_(initial_metadata) {}

  static UniqueTypeName Type() {
    return GRPC_UNIQUE_TYPE_NAME_HERE("rpc_context");
  }
  UniqueTypeName type() const override { return Type(); }

  // Returns the metadata value(s) for the specified key.
  // As special cases, binary headers and "te" return a value of std::nullopt,
  // "content-type" header returns "application/grpc", and "host" and
  // ":authority" both return GetAuthorityOrHost().
  std::optional<absl::string_view> GetHeaderValue(
      absl::string_view header_name) const;

  // Calls fn once for each header name for which GetHeaderValue() returns
  // a value.  Names are visited in no particular order.
  void ForEachHeaderName(absl::FunctionRef<void(absl::string_view)> fn) const;

 private:
  grpc_metadata_batch* initial_metadata_;
  mutable std::string buffer_;
};

}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_XDS_GRPC_XDS_MATCHER_CONTEXT_H
