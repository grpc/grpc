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

#ifndef GRPC_SRC_CORE_XDS_GRPC_XDS_MATCHER_ACTION_H
#define GRPC_SRC_CORE_XDS_GRPC_XDS_MATCHER_ACTION_H

#include "src/core/util/ref_counted.h"
#include "src/core/util/ref_counted_ptr.h"
#include "src/core/xds/grpc/xds_common_types.h"
#include "src/core/xds/grpc/xds_common_types_parser.h"
#include "src/core/xds/grpc/xds_matcher.h"
#include "xds/core/v3/extension.upb.h"

namespace grpc_core {

class ActionFactory {
 public:
  virtual ~ActionFactory() = default;
  virtual absl::string_view type() const = 0;
  virtual std::unique_ptr<XdsMatcher::Action> ParseAndCreateAction(
      const XdsResourceType::DecodeContext& context,
      absl::string_view serialized_value, ValidationErrors* errors) const = 0;
};

class XdsMatcherActionRegistry {
 private:
  using FactoryMap =
      std::map<absl::string_view, std::unique_ptr<ActionFactory>>;

 public:
  void AddActionFactory(std::unique_ptr<ActionFactory> factory);
  std::unique_ptr<XdsMatcher::Action> ParseAndCreateAction(
      const XdsResourceType::DecodeContext& context, const XdsExtension& action,
      ValidationErrors* errors) const {
    const auto it = factories_.find(action.type);
    if (it == factories_.cend()) return nullptr;
    const absl::string_view* serialized_value =
        std::get_if<absl::string_view>(&action.value);
    return it->second->ParseAndCreateAction(context, *serialized_value, errors);
  }

 private:
  FactoryMap factories_;
};

}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_XDS_GRPC_XDS_MATCHER_ACTION_H
