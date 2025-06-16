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

#ifndef GRPC_SRC_CORE_XDS_GRPC_XDS_MATCHER_INPUT_H
#define GRPC_SRC_CORE_XDS_GRPC_XDS_MATCHER_INPUT_H

#include "src/core/xds/grpc/xds_matcher.h"

namespace grpc_core {

class MetadataInput : public XdsMatcher::InputValue<absl::string_view> {
 public:
  explicit MetadataInput(absl::string_view key) : key_(key) {}

  // The supported MatchContext type.
  // When validating an xDS resource, if an input is specified in a
  // context that it doesn't support, the resource should be NACKed.
  UniqueTypeName context_type() const override {
    return GRPC_UNIQUE_TYPE_NAME_HERE("rpc_context");
  };

  // Gets the value to be matched from context.
  std::optional<absl::string_view> GetValue(
      const XdsMatcher::MatchContext& context) const override;

 private:
  std::string key_;
};

}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_XDS_GRPC_XDS_MATCHER_INPUT_H
