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

#include "src/core/xds/grpc/xds_matcher_action.h"

#include <memory>
#include <variant>

#include "src/core/xds/grpc/xds_common_types.h"

namespace grpc_core {

void XdsMatcherActionRegistry::AddActionFactory(
    std::unique_ptr<XdsMatcherActionFactory> factory) {
  factories_.emplace(factory->type(), std::move(factory));
}

std::unique_ptr<XdsMatcher::Action>
XdsMatcherActionRegistry::ParseAndCreateAction(
    const XdsResourceType::DecodeContext& context, const XdsExtension& action,
    ValidationErrors* errors) const {
  const auto it = factories_.find(action.type);
  if (it == factories_.cend()) {
    errors->AddError("Unsupported Action. Not found in registry");
    return nullptr;
  }
  const absl::string_view* serialized_value =
      std::get_if<absl::string_view>(&action.value);
  if (serialized_value == nullptr) {
    errors->AddError("Unsuppored action format (Json found instead of string)");
    return nullptr;
  }
  return it->second->ParseAndCreateAction(context, *serialized_value, errors);
}

}  // namespace grpc_core
