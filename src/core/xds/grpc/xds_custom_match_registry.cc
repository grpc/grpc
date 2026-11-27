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

#include "src/core/xds/grpc/xds_custom_match_registry.h"

#include <memory>
#include <utility>
#include <variant>

#include "absl/strings/str_cat.h"

namespace grpc_core {

void XdsCustomMatchRegistry::RegisterFactory(
    std::unique_ptr<XdsCustomMatchFactory> factory) {
  absl::string_view type = factory->type();
  factories_.emplace(type, std::move(factory));
}

std::unique_ptr<XdsCustomMatchFactory::CustomMatch>
XdsCustomMatchRegistry::ParseCustomMatch(
    const XdsResourceType::DecodeContext& context,
    const XdsExtension& custom_match, ValidationErrors* errors) const {
  const auto it = factories_.find(custom_match.type);
  if (it == factories_.end()) {
    errors->AddError("unsupported custom_match type");
    return nullptr;
  }
  const absl::string_view* serialized_value =
      std::get_if<absl::string_view>(&custom_match.value);
  if (serialized_value == nullptr) {
    errors->AddError(absl::StrCat("could not parse ", custom_match.type));
    return nullptr;
  }
  return it->second->ParseCustomMatch(context, *serialized_value, errors);
}

}  // namespace grpc_core
