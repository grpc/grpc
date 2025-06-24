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

#include "envoy/extensions/filters/http/rate_limit_quota/v3/rate_limit_quota.upb.h"
#include "src/core/util/ref_counted.h"
#include "src/core/util/ref_counted_ptr.h"
#include "src/core/util/upb_utils.h"

namespace grpc_core {

ActionRegistry::ActionRegistry() {
  factories_.emplace(
      "envoy.extensions.filters.http.rate_limit_quota.v3."
      "RateLimitQuotaBucketSettings",
      std::make_unique<BucketingActionFactory>());
}

RefCountedPtr<ActionConfig> BucketingActionFactory::ParseConfig(
    const XdsResourceType::DecodeContext& context,
    const xds_core_v3_TypedExtensionConfig* action,
    ValidationErrors* errors) const {
  ValidationErrors::ScopedField field(errors, ".bucketaction");
  const google_protobuf_Any* any =
      xds_core_v3_TypedExtensionConfig_typed_config(action);
  auto extension = ExtractXdsExtension(context, any, errors);
  if (!extension.has_value()) {
    errors->AddError("Fail to extract XdsExtenstion");
    return nullptr;
  }
  if (extension->type !=
      "envoy.extensions.filters.http.rate_limit_quota.v3."
      "RateLimitQuotaBucketSettings") {
    errors->AddError("unsupported action type");
    return nullptr;
  }
  // Parse RLQS Bucketing action
  BucketingAction::BucketConfig config;
  absl::string_view* serialised_rate_limit_quota_bucket_settings =
      std::get_if<absl::string_view>(&extension->value);
  auto rate_limit_quota_bucket_settings =
      envoy_extensions_filters_http_rate_limit_quota_v3_RateLimitQuotaBucketSettings_parse(
          serialised_rate_limit_quota_bucket_settings->data(),
          serialised_rate_limit_quota_bucket_settings->size(), context.arena);
  // Other cases needed to be supported. Having only bucket_id_builder for now.
  if (!envoy_extensions_filters_http_rate_limit_quota_v3_RateLimitQuotaBucketSettings_has_bucket_id_builder(
          rate_limit_quota_bucket_settings)) {
    errors->AddError("bucket_id_builder missing, rest value are unsupported");
    return nullptr;
  }
  auto bucket_id_builder =
      envoy_extensions_filters_http_rate_limit_quota_v3_RateLimitQuotaBucketSettings_bucket_id_builder(
          rate_limit_quota_bucket_settings);
  // parse map to get generate key:value pair
  size_t iter = kUpb_Map_Begin;
  upb_StringView key;
  const envoy_extensions_filters_http_rate_limit_quota_v3_RateLimitQuotaBucketSettings_BucketIdBuilder_ValueBuilder*
      value;
  while (
      envoy_extensions_filters_http_rate_limit_quota_v3_RateLimitQuotaBucketSettings_BucketIdBuilder_bucket_id_builder_next(
          bucket_id_builder, &key, &value, &iter)) {
    // Checking only string value other values are also possible (May need to
    // support)
    if (envoy_extensions_filters_http_rate_limit_quota_v3_RateLimitQuotaBucketSettings_BucketIdBuilder_ValueBuilder_has_string_value(
            value)) {
      auto string_value =
          envoy_extensions_filters_http_rate_limit_quota_v3_RateLimitQuotaBucketSettings_BucketIdBuilder_ValueBuilder_string_value(
              value);
      // Add to Map
      config.map[UpbStringToStdString(key)] =
          UpbStringToStdString(string_value);
    }
  }
  return MakeRefCounted<BucketingActionConfig>(type(), std::move(config));
}

}  // namespace grpc_core