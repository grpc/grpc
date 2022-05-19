//
// Copyright 2020 gRPC authors.
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

#include <grpc/support/port_platform.h>

#include "src/core/ext/filters/client_channel/config_selector.h"

#include <memory>

#include "absl/strings/str_format.h"

#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/gpr/useful.h"
#include "src/core/lib/gprpp/ref_counted_ptr.h"
#include "src/core/lib/resolver/server_address.h"

namespace grpc_core {

namespace {

class ConfigSelectorResolverAttribute
    : public ResolverAttributeMap::AttributeInterface {
 public:
  explicit ConfigSelectorResolverAttribute(
      RefCountedPtr<ConfigSelector> config_selector)
      : config_selector_(std::move(config_selector)) {}

  static UniqueTypeName Type() {
    static auto* kFactory = new UniqueTypeName::Factory("config_selector");
    return kFactory->Create();
  }

  UniqueTypeName type() const override { return Type(); }

  std::unique_ptr<AttributeInterface> Copy() const override {
    return absl::make_unique<ConfigSelectorResolverAttribute>(config_selector_);
  }

  int Compare(const AttributeInterface* other) const override {
    auto* other_attribute =
        static_cast<const ConfigSelectorResolverAttribute*>(other);
    return QsortCompare(config_selector_.get(),
                        other_attribute->config_selector_.get());
  }

  std::string ToString() const override {
    return absl::StrFormat("{config_selector=%p}", config_selector_.get());
  }

  ConfigSelector* config_selector() const { return config_selector_.get(); }

 private:
  RefCountedPtr<ConfigSelector> config_selector_;
};

}  // namespace

std::unique_ptr<ResolverAttributeMap::AttributeInterface>
ConfigSelector::MakeResolverAttribute() {
  return absl::make_unique<ConfigSelectorResolverAttribute>(Ref());
}

RefCountedPtr<ConfigSelector> ConfigSelector::GetFromResolverAttributes(
    const ResolverAttributeMap& attributes) {
  auto* attribute = static_cast<const ConfigSelectorResolverAttribute*>(
      attributes.Get(ConfigSelectorResolverAttribute::Type()));
  if (attribute == nullptr) return nullptr;
  return attribute->config_selector()->Ref(DEBUG_LOCATION,
                                           "GetFromChannelArgs");
}

}  // namespace grpc_core
