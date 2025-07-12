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

#include "absl/strings/str_cat.h"
#include "src/core/util/ref_counted.h"
#include "src/core/util/ref_counted_ptr.h"
#include "src/core/xds/grpc/xds_common_types.h"
#include "src/core/xds/grpc/xds_common_types_parser.h"
#include "src/core/xds/grpc/xds_matcher.h"
#include "src/core/xds/grpc/xds_matcher_context.h"
#include "xds/type/matcher/v3/matcher.upb.h"

namespace grpc_core {

// Forward declaration
template <typename T>
class InputFactory;

template <typename T>
class InputFactory final {
 public:
  virtual ~InputFactory() = default;
  virtual absl::string_view type() const = delete;
  virtual std::unique_ptr<XdsMatcher::InputValue<T>> ParseAndCreateInput(
      const XdsResourceType::DecodeContext& context,
      absl::string_view serialized_value,
      ValidationErrors* errors) const = delete;
};

template <>
class InputFactory<absl::string_view> {
 public:
  virtual ~InputFactory() = default;
  virtual absl::string_view type() const = 0;
  virtual std::unique_ptr<XdsMatcher::InputValue<absl::string_view>>
  ParseAndCreateInput(const XdsResourceType::DecodeContext& context,
                      absl::string_view serialized_value,
                      ValidationErrors* errors) const = 0;
};

template <typename T = absl::string_view>
class XdsMatcherInputRegistry {
 private:
  using FactoryMap =
      std::map<absl::string_view, std::unique_ptr<InputFactory<T>>>;

 public:
  XdsMatcherInputRegistry();
  std::unique_ptr<XdsMatcher::InputValue<T>> ParseAndCreateInput(
      const XdsResourceType::DecodeContext& context, const XdsExtension& input,
      ValidationErrors* errors) const {
    const auto it = factories_.find(input.type);
    if (it == factories_.cend()) return nullptr;
    const absl::string_view* serliased_value =
        std::get_if<absl::string_view>(&input.value);
    return it->second->ParseAndCreateInput(context, *serliased_value, errors);
  }

 private:
  FactoryMap factories_;
};

class MetadataInput : public XdsMatcher::InputValue<absl::string_view> {
 public:
  explicit MetadataInput(absl::string_view key) : key_(key) {}
  UniqueTypeName context_type() const override {
    return RpcMatchContext::Type();
  };
  std::optional<absl::string_view> GetValue(
      const XdsMatcher::MatchContext& context) const override;
  bool Equal(
      const XdsMatcher::InputValue<absl::string_view>& other) const override {
    const auto* o = dynamic_cast<const MetadataInput*>(&other);
    return o != nullptr && key_ == o->key_;
  }
  std::string ToString() const override {
    return absl::StrCat("MetadataInput(key=", key_, ")");
  }

 private:
  std::string key_;
  mutable std::string buffer_;
};

class MetadataInputFactory : public InputFactory<absl::string_view> {
 public:
  absl::string_view type() const override { return Type(); }
  static absl::string_view Type() {
    return "envoy.type.matcher.v3.HttpRequestHeaderMatchInput";
  }
  std::unique_ptr<XdsMatcher::InputValue<absl::string_view>>
  ParseAndCreateInput(const XdsResourceType::DecodeContext& context,
                      absl::string_view serialized_value,
                      ValidationErrors* errors) const override;
};

}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_XDS_GRPC_XDS_MATCHER_INPUT_H
