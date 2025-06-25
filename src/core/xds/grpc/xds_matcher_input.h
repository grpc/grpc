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

#include "src/core/util/ref_counted.h"
#include "src/core/util/ref_counted_ptr.h"
#include "src/core/xds/grpc/xds_common_types.h"
#include "src/core/xds/grpc/xds_common_types_parser.h"
#include "src/core/xds/grpc/xds_matcher.h"
#include "xds/type/matcher/v3/matcher.upb.h"

namespace grpc_core {

// Forward declaration
template <typename T>
class InputFactory;

class InputConfig : public RefCounted<InputConfig> {
 public:
  ~InputConfig() override = default;
  virtual absl::string_view type() const = 0;
  virtual bool Equals(const InputConfig& other) const = 0;
  virtual std::string ToString() const = 0;
};

template <typename T>
class InputFactory final {
 public:
  virtual ~InputFactory() = default;
  virtual absl::string_view type() const = delete;
  virtual RefCountedPtr<InputConfig> ParseConfig(
      const XdsResourceType::DecodeContext& context, XdsExtension& input,
      ValidationErrors* errors) const = delete;
  virtual std::unique_ptr<XdsMatcher::InputValue<T>> CreateInput(
      RefCountedPtr<InputConfig> config) const = delete;
};

template <>
class InputFactory<absl::string_view> {
 public:
  virtual ~InputFactory() = default;
  virtual absl::string_view type() const = 0;
  virtual RefCountedPtr<InputConfig> ParseConfig(
      const XdsResourceType::DecodeContext& context, XdsExtension& input,
      ValidationErrors* errors) const = 0;
  virtual std::unique_ptr<XdsMatcher::InputValue<absl::string_view>>
  CreateInput(RefCountedPtr<InputConfig> config) const = 0;
};

template <typename T = absl::string_view>
class InputRegistry {
 private:
  using FactoryMap =
      std::map<absl::string_view, std::unique_ptr<InputFactory<T>>>;

 public:
  explicit InputRegistry();

  bool IsSupported(absl::string_view type) const {
    return factories_.find(type) != factories_.end();
  }

  RefCountedPtr<InputConfig> ParseConfig(
      const XdsResourceType::DecodeContext& context, XdsExtension& input,
      ValidationErrors* errors) const {
    const auto it = factories_.find(input.type);
    if (it == factories_.cend()) return nullptr;
    return it->second->ParseConfig(context, input, errors);
  }

  std::unique_ptr<XdsMatcher::InputValue<T>> CreateInput(
      RefCountedPtr<InputConfig> config) const {
    if (config == nullptr) return nullptr;
    const auto it = factories_.find(config->type());
    if (it == factories_.cend()) return nullptr;
    return it->second->CreateInput(std::move(config));
  }

 private:
  FactoryMap factories_;
};

class MetadataInput : public XdsMatcher::InputValue<absl::string_view> {
 public:
  explicit MetadataInput(absl::string_view key) : key_(key) {}

  UniqueTypeName context_type() const override {
    return GRPC_UNIQUE_TYPE_NAME_HERE("rpc_context");
  };

  std::optional<absl::string_view> GetValue(
      const XdsMatcher::MatchContext& context) const override;

 private:
  std::string key_;
};

// Input for "envoy.type.matcher.v3.HttpRequestHeaderMatchInput"
class MetadataInputConfig : public InputConfig {
 public:
  MetadataInputConfig(absl::string_view type, std::string key)
      : type_(type), key_(std::move(key)) {}

  absl::string_view type() const override { return type_; }
  const std::string& key() const { return key_; }

  bool Equals(const InputConfig& other) const override {
    if (type() != other.type()) return false;
    const auto* o = static_cast<const MetadataInputConfig*>(&other);
    return key_ == o->key_;
  }

  std::string ToString() const override {
    return absl::StrCat("type=", type_, " key=", key_);
  }

 private:
  absl::string_view type_;
  std::string key_;
};

// 2. Concrete Factory for MetadataInput
class MetadataInputFactory : public InputFactory<absl::string_view> {
 public:
  absl::string_view type() const override { return Type(); }

  static absl::string_view Type() {
    return "envoy.type.matcher.v3.HttpRequestHeaderMatchInput";
  }

  RefCountedPtr<InputConfig> ParseConfig(
      const XdsResourceType::DecodeContext& context, XdsExtension& input,
      ValidationErrors* errors) const override;

  std::unique_ptr<XdsMatcher::InputValue<absl::string_view>> CreateInput(
      RefCountedPtr<InputConfig> config) const override {
    auto* metadata_config = static_cast<MetadataInputConfig*>(config.get());
    return std::make_unique<MetadataInput>(metadata_config->key());
  }
};

}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_XDS_GRPC_XDS_MATCHER_INPUT_H
