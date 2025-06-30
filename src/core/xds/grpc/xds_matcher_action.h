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

class ActionConfig : public RefCounted<ActionConfig> {
 public:
  ~ActionConfig() override = default;
  virtual absl::string_view type() const = 0;
  virtual bool Equals(const ActionConfig& other) const = 0;
  virtual std::string ToString() const = 0;
};

class ActionFactory {
 public:
  virtual ~ActionFactory() = default;
  virtual absl::string_view type() const = 0;
  virtual RefCountedPtr<ActionConfig> ParseConfig(
      const XdsResourceType::DecodeContext& context, XdsExtension& action,
      ValidationErrors* errors) const = 0;
  virtual std::unique_ptr<XdsMatcher::Action> CreateAction(
      RefCountedPtr<ActionConfig> config) const = 0;
};

class ActionRegistry {
 private:
  using FactoryMap =
      std::map<absl::string_view, std::unique_ptr<ActionFactory>>;

 public:
  explicit ActionRegistry();

  bool IsSupported(absl::string_view type) const {
    return factories_.find(type) != factories_.end();
  }

  RefCountedPtr<ActionConfig> ParseConfig(
      const XdsResourceType::DecodeContext& context, XdsExtension& action,
      ValidationErrors* errors) const {
    const auto it = factories_.find(action.type);
    if (it == factories_.cend()) return nullptr;
    return it->second->ParseConfig(context, action, errors);
  }

  std::unique_ptr<XdsMatcher::Action> CreateAction(
      RefCountedPtr<ActionConfig> config) const {
    if (config == nullptr) return nullptr;
    const auto it = factories_.find(config->type());
    if (it == factories_.cend()) return nullptr;
    return it->second->CreateAction(std::move(config));
  }

 private:
  FactoryMap factories_;
};

// --- Concrete Action : BucketingAction ---
// Need to implement this completely.
class BucketingAction : public XdsMatcher::Action {
 public:
  struct BucketConfig {
    absl::flat_hash_map<std::string, std::string> map;
    bool operator==(const BucketConfig& other) const {
      return map == other.map;
    }
  };
  explicit BucketingAction(BucketConfig config)
      : bucket_config_(std::move(config)) {}
  absl::string_view type_url() const override {
    return "envoy.extensions.filters.http.rate_limit_quota.v3."
           "RateLimitQuotaBucketSettings";
  }
  absl::string_view GetConfigValue(absl::string_view key) const {
    auto it = bucket_config_.map.find(key);
    if (it == bucket_config_.map.end()) return "";
    return it->second;
  }
  const BucketConfig& bucket_config() const { return bucket_config_; }

 private:
  BucketConfig bucket_config_;
};

class BucketingActionConfig : public ActionConfig {
 public:
  BucketingActionConfig(absl::string_view type_url,
                        BucketingAction::BucketConfig config)
      : type_(type_url), config_(std::move(config)) {}
  absl::string_view type() const override { return type_; }
  std::string ToString() const override {
    std::string map_str;
    for (const auto& pair : config_.map) {
      absl::StrAppend(&map_str, !map_str.empty() ? ", " : "", "{", pair.first,
                      ": ", pair.second, "}");
    }
    return absl::StrCat("type_url=", type_, " buckets=[", map_str, "]");
  }
  bool Equals(const ActionConfig& other) const override {
    if (type() != other.type()) return false;
    const auto* o = static_cast<const BucketingActionConfig*>(&other);
    return config_ == o->config_;
  }
  const BucketingAction::BucketConfig& config() const { return config_; }

 private:
  absl::string_view type_;
  BucketingAction::BucketConfig config_;
};

class BucketingActionFactory : public ActionFactory {
 public:
  absl::string_view type() const override { return Type(); }
  absl::string_view Type() const {
    return "envoy.extensions.filters.http.rate_limit_quota.v3."
           "RateLimitQuotaBucketSettings";
  }
  RefCountedPtr<ActionConfig> ParseConfig(
      const XdsResourceType::DecodeContext& context, XdsExtension& action,
      ValidationErrors* errors) const override;

  std::unique_ptr<XdsMatcher::Action> CreateAction(
      RefCountedPtr<ActionConfig> config) const override {
    return std::make_unique<BucketingAction>(
        static_cast<BucketingActionConfig*>(config.get())->config());
  }
};

}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_XDS_GRPC_XDS_MATCHER_ACTION_H
