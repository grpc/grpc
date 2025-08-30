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
#include "src/core/xds/grpc/xds_common_types.h"
#include "src/core/xds/grpc/xds_matcher.h"
#include "src/core/xds/grpc/xds_matcher_context.h"
#include "src/core/xds/xds_client/xds_resource_type.h"

namespace grpc_core {

template <typename T>
class XdsMatcherInputFactory final {
 public:
  virtual absl::string_view type() const = delete;
  virtual UniqueTypeName context_type() const = delete;
  virtual std::unique_ptr<XdsMatcher::InputValue<T>> ParseAndCreateInput(
      const XdsResourceType::DecodeContext& context,
      absl::string_view serialized_value,
      ValidationErrors* errors) const = delete;
  virtual ~XdsMatcherInputFactory() = default;
};

template <>
class XdsMatcherInputFactory<absl::string_view> {
 public:
  virtual absl::string_view type() const = 0;
  virtual UniqueTypeName context_type() const = 0;
  virtual std::unique_ptr<XdsMatcher::InputValue<absl::string_view>>
  ParseAndCreateInput(const XdsResourceType::DecodeContext& context,
                      absl::string_view serialized_value,
                      ValidationErrors* errors) const = 0;
  virtual ~XdsMatcherInputFactory() = default;
};

template <typename T = absl::string_view>
class XdsMatcherInputRegistry {
 public:
  XdsMatcherInputRegistry();
  std::unique_ptr<XdsMatcher::InputValue<T>> ParseAndCreateInput(
      const XdsResourceType::DecodeContext& context, const XdsExtension& input,
      const UniqueTypeName& matcher_context, ValidationErrors* errors) const;

 private:
  using FactoryMap =
      std::map<absl::string_view, std::unique_ptr<XdsMatcherInputFactory<T>>>;

  FactoryMap factories_;
};

class MetadataInput : public XdsMatcher::InputValue<absl::string_view> {
 public:
  explicit MetadataInput(absl::string_view key) : key_(key) {}
  std::optional<absl::string_view> GetValue(
      const XdsMatcher::MatchContext& context) const override;
  static UniqueTypeName Type() {
    return GRPC_UNIQUE_TYPE_NAME_HERE("MetadataInput");
  }
  UniqueTypeName type() const override { return Type(); }
  bool Equals(
      const XdsMatcher::InputValue<absl::string_view>& other) const override {
    if (type() != other.type()) return false;
    const auto& o = DownCast<const MetadataInput&>(other);
    return key_ == o.key_;
  }
  std::string ToString() const override {
    return absl::StrCat("MetadataInput(key=", key_, ")");
  }

 private:
  std::string key_;
};

class MetadataInputFactory : public XdsMatcherInputFactory<absl::string_view> {
 public:
  absl::string_view type() const override { return Type(); }
  static absl::string_view Type() {
    return "envoy.type.matcher.v3.HttpRequestHeaderMatchInput";
  }
  UniqueTypeName context_type() const override {
    return RpcMatchContext::Type();
  }
  std::unique_ptr<XdsMatcher::InputValue<absl::string_view>>
  ParseAndCreateInput(const XdsResourceType::DecodeContext& context,
                      absl::string_view serialized_value,
                      ValidationErrors* errors) const override;
};

}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_XDS_GRPC_XDS_MATCHER_INPUT_H
