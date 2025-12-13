//
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
//

#include "src/core/xds/grpc/xds_http_composite_filter.h"

#include "envoy/config/core/v3/extension.upb.h"
#include "envoy/extensions/common/matching/v3/extension_matcher.upb.h"
#include "envoy/extensions/common/matching/v3/extension_matcher.upbdefs.h"
#include "envoy/extensions/filters/common/matcher/action/v3/skip_action.upb.h"
#include "envoy/extensions/filters/common/matcher/action/v3/skip_action.upbdefs.h"
#include "envoy/extensions/filters/http/composite/v3/composite.upb.h"
#include "envoy/extensions/filters/http/composite/v3/composite.upbdefs.h"
#include "envoy/type/v3/percent.upb.h"
#include "src/core/filter/composite/composite_filter.h"
#include "src/core/util/validation_errors.h"
#include "src/core/xds/grpc/xds_bootstrap_grpc.h"
#include "src/core/xds/grpc/xds_http_filter.h"
#include "src/core/xds/grpc/xds_http_filter_registry.h"
#include "src/core/xds/grpc/xds_matcher.h"
#include "src/core/xds/grpc/xds_matcher_action.h"
#include "src/core/xds/grpc/xds_matcher_context.h"
#include "src/core/xds/grpc/xds_matcher_parse.h"
#include "src/core/xds/xds_client/xds_client.h"
#include "absl/strings/string_view.h"

namespace grpc_core {

absl::string_view XdsHttpCompositeFilter::ConfigProtoName() const {
  return "envoy.extensions.common.matching.v3.ExtensionWithMatcher";
}

absl::string_view XdsHttpCompositeFilter::OverrideConfigProtoName() const {
  return "envoy.extensions.common.matching.v3.ExtensionWithMatcherPerRoute";
}

void XdsHttpCompositeFilter::PopulateSymtab(upb_DefPool* symtab) const {
  envoy_extensions_common_matching_v3_ExtensionWithMatcher_getmsgdef(symtab);
  envoy_extensions_common_matching_v3_ExtensionWithMatcherPerRoute_getmsgdef(
      symtab);
  envoy_extensions_filters_common_matcher_action_v3_SkipFilter_getmsgdef(
      symtab);
  envoy_extensions_filters_http_composite_v3_Composite_getmsgdef(symtab);
  envoy_extensions_filters_http_composite_v3_ExecuteFilterAction_getmsgdef(
      symtab);
}

void XdsHttpCompositeFilter::AddFilter(
    FilterChainBuilder& builder,
    RefCountedPtr<const FilterConfig> config) const {
  builder.AddFilter<CompositeFilter>(std::move(config));
}

const grpc_channel_filter* XdsHttpCompositeFilter::channel_filter() const {
  return &CompositeFilter::kFilterVtable;
}

namespace {

// Matcher action factory for SkipFilter.
class SkipFilterActionFactory final : public XdsMatcherActionFactory {
 public:
  absl::string_view type() const override {
    return CompositeFilter::SkipFilterAction::Type().name();
  }

  std::unique_ptr<XdsMatcher::Action> ParseAndCreateAction(
      const XdsResourceType::DecodeContext& context,
      absl::string_view serialized_value,
      ValidationErrors* errors) const override {
    const auto* skip_filter =
        envoy_extensions_filters_common_matcher_action_v3_SkipFilter_parse(
            serialized_value.data(), serialized_value.size(), context.arena);
    if (skip_filter == nullptr) {
      errors->AddError("could not parse SkipFilter action");
      return nullptr;
    }
    return std::make_unique<CompositeFilter::SkipFilterAction>();
  }
};

// Returns the number per million.
uint32_t ParseFractionalPercent(
    const envoy_type_v3_FractionalPercent* fractional_percent) {
  if (fractional_percent == nullptr) return 1000000;
  uint32_t numerator =
      envoy_type_v3_FractionalPercent_numerator(fractional_percent);
  const auto denominator =
      static_cast<envoy_type_v3_FractionalPercent_DenominatorType>(
          envoy_type_v3_FractionalPercent_denominator(fractional_percent));
  switch (denominator) {
    case envoy_type_v3_FractionalPercent_MILLION:
      return numerator;
    case envoy_type_v3_FractionalPercent_TEN_THOUSAND:
      return numerator * 100;
    case envoy_type_v3_FractionalPercent_HUNDRED:
    default:
      return numerator * 10000;
  }
}

// Matcher action factory for ExecuteFilterAction.
class ExecuteFilterActionFactory final : public XdsMatcherActionFactory {
 public:
  absl::string_view type() const override {
    return CompositeFilter::ExecuteFilterAction::Type().name();
  }

  std::unique_ptr<XdsMatcher::Action> ParseAndCreateAction(
      const XdsResourceType::DecodeContext& context,
      absl::string_view serialized_value,
      ValidationErrors* errors) const override {
    const auto* execute_filter =
        envoy_extensions_filters_http_composite_v3_ExecuteFilterAction_parse(
            serialized_value.data(), serialized_value.size(), context.arena);
    if (execute_filter == nullptr) {
      errors->AddError("could not parse ExecuteFilterAction");
      return nullptr;
    }
    const auto& http_filter_registry =
        DownCast<const GrpcXdsBootstrap&>(context.client->bootstrap())
            .http_filter_registry();
    std::vector<CompositeFilter::ExecuteFilterAction::Filter> filters;
    // FIXME: check filter_chain field first
    const auto* typed_config =
        envoy_extensions_filters_http_composite_v3_ExecuteFilterAction_typed_config(
            execute_filter);
    if (typed_config != nullptr) {
      absl::string_view name = UpbStringToAbsl(
          envoy_config_core_v3_TypedExtensionConfig_name(typed_config));
      const auto* any =
          envoy_config_core_v3_TypedExtensionConfig_typed_config(typed_config);
      ValidationErrors::ScopedField field(errors, ".typed_config.typed_config");
      auto extension = ExtractXdsExtension(context, any, errors);
      if (extension.has_value()) {
        const XdsHttpFilterImpl* filter_impl =
            http_filter_registry.GetFilterForTopLevelType(extension->type);
        if (filter_impl == nullptr) {
          errors->AddError("unsupported filter type");
        } else {
          // FIXME: can we check is_client to determine if the filter is
          // supported on the client/server side?
          RefCountedPtr<const FilterConfig> config =
              filter_impl->ParseTopLevelConfig(name, context, *extension,
                                               errors);
          filters.push_back({filter_impl, std::move(config)});
        }
      }
    } else {
      errors->AddError("one of typed_config or filter_chain must be set");
    }
    uint32_t sample_per_million = 1000000;
    const auto* sample_percent_proto =
        envoy_extensions_filters_http_composite_v3_ExecuteFilterAction_sample_percent(
            execute_filter);
    if (sample_percent_proto != nullptr) {
      const auto* default_value =
          envoy_config_core_v3_RuntimeFractionalPercent_default_value(
              sample_percent_proto);
      if (default_value == nullptr) {
        ValidationErrors::ScopedField field(errors,
                                            ".sample_percent.default_value");
        errors->AddError("field not set");
      } else {
        sample_per_million = ParseFractionalPercent(default_value);
      }
    }
    return std::make_unique<CompositeFilter::ExecuteFilterAction>(
        std::move(filters), sample_per_million);
  }
};

std::unique_ptr<XdsMatcher> ParseMatcher(
    const XdsResourceType::DecodeContext& context,
    const xds_type_matcher_v3_Matcher* matcher, ValidationErrors* errors) {
  XdsMatcherActionRegistry action_registry;
  action_registry.AddActionFactory(std::make_unique<SkipFilterActionFactory>());
  action_registry.AddActionFactory(
      std::make_unique<ExecuteFilterActionFactory>());
  return ParseXdsMatcher(context, matcher, action_registry,
                         RpcMatchContext::Type(), errors);
}

}  // namespace

RefCountedPtr<const FilterConfig> XdsHttpCompositeFilter::ParseTopLevelConfig(
    absl::string_view /*instance_name*/,
    const XdsResourceType::DecodeContext& context,
    const XdsExtension& extension, ValidationErrors* errors) const {
  const absl::string_view* serialized_filter_config =
      std::get_if<absl::string_view>(&extension.value);
  if (serialized_filter_config == nullptr) {
    errors->AddError("could not parse composite filter config");
    return nullptr;
  }
  auto* extension_with_matcher =
      envoy_extensions_common_matching_v3_ExtensionWithMatcher_parse(
          serialized_filter_config->data(), serialized_filter_config->size(),
          context.arena);
  if (extension_with_matcher == nullptr) {
    errors->AddError("could not parse composite filter config");
    return nullptr;
  }
  // Check extension_config.
  const auto* extension_config =
      envoy_extensions_common_matching_v3_ExtensionWithMatcher_extension_config(
          extension_with_matcher);
  if (extension_config == nullptr) {
    ValidationErrors::ScopedField field(errors, ".extension_config");
    errors->AddError("field not set");
  } else {
    const auto* typed_config =
        envoy_config_core_v3_TypedExtensionConfig_typed_config(
            extension_config);
    auto extension = ExtractXdsExtension(context, typed_config, errors);
    if (extension.has_value()) {
      if (extension->type !=
          "envoy.extensions.filters.http.composite.v3.Composite") {
        errors->AddError("unsupported extension config type");
      }
    }
  }
  // Parse matcher.
  auto config = MakeRefCounted<CompositeFilter::Config>();
  config->matcher = ParseMatcher(
      context,
      envoy_extensions_common_matching_v3_ExtensionWithMatcher_xds_matcher(
          extension_with_matcher),
      errors);
  return config;
}

RefCountedPtr<const FilterConfig> XdsHttpCompositeFilter::ParseOverrideConfig(
    absl::string_view instance_name,
    const XdsResourceType::DecodeContext& context,
    const XdsExtension& extension, ValidationErrors* errors) const {
  const absl::string_view* serialized_filter_config =
      std::get_if<absl::string_view>(&extension.value);
  if (serialized_filter_config == nullptr) {
    errors->AddError("could not parse composite filter override config");
    return nullptr;
  }
  auto* extension_with_matcher =
      envoy_extensions_common_matching_v3_ExtensionWithMatcherPerRoute_parse(
          serialized_filter_config->data(), serialized_filter_config->size(),
          context.arena);
  if (extension_with_matcher == nullptr) {
    errors->AddError("could not parse composite filter override config");
    return nullptr;
  }
  auto config = MakeRefCounted<CompositeFilter::Config>();
  config->matcher = ParseMatcher(
      context,
      envoy_extensions_common_matching_v3_ExtensionWithMatcherPerRoute_xds_matcher(
          extension_with_matcher),
      errors);
  return config;
}

}  // namespace grpc_core
