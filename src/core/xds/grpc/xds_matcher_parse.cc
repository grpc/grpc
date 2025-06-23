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

#include "src/core/xds/grpc/xds_matcher_parse.h"

#include <upb/message/map.h>

#include <memory>

#include "envoy/config/core/v3/extension.upb.h"
#include "envoy/extensions/filters/http/rate_limit_quota/v3/rate_limit_quota.upb.h"
#include "envoy/type/matcher/v3/http_inputs.upb.h"
#include "envoy/type/matcher/v3/regex.upb.h"
#include "envoy/type/matcher/v3/string.upb.h"
#include "src/core/util/upb_utils.h"
#include "src/core/xds/grpc/xds_bootstrap_grpc.h"
#include "src/core/xds/grpc/xds_common_types_parser.h"
#include "src/core/xds/grpc/xds_matcher.h"
#include "src/core/xds/grpc/xds_matcher_action.h"
#include "src/core/xds/grpc/xds_matcher_input.h"
#include "src/core/xds/xds_client/xds_client.h"
#include "xds/core/v3/extension.upb.h"
#include "xds/type/matcher/v3/http_inputs.upb.h"
#include "xds/type/matcher/v3/matcher.upb.h"
#include "xds/type/matcher/v3/regex.upb.h"
#include "xds/type/matcher/v3/string.upb.h"

namespace grpc_core {

// Forward declaration for ParseMatcher to resolve circular dependency.
std::unique_ptr<XdsMatcher> ParseMatcher(
    const XdsResourceType::DecodeContext& context,
    const xds_type_matcher_v3_Matcher* matcher, ValidationErrors* errors);

// Forward declaration for ParsePredicate to resolve circular dependency.
std::unique_ptr<XdsMatcherList::Predicate> ParsePredicate(
    const XdsResourceType::DecodeContext& context,
    const xds_type_matcher_v3_Matcher_MatcherList_Predicate* predicate,
    ValidationErrors* errors);

// Function to parse "xds_core_v3_TypedExtensionConfig" to generate
// XdsMatcher::Input<T>
// The parsing is for input which return absl::string_view
std::unique_ptr<XdsMatcher::InputValue<absl::string_view>> ParseStringInput(
    const XdsResourceType::DecodeContext& context,
    const xds_core_v3_TypedExtensionConfig* input, ValidationErrors* errors) {
  const auto& registry =
      DownCast<const GrpcXdsBootstrap&>(context.client->bootstrap())
          .xds_matcher_input_registry();
  RefCountedPtr<InputConfig> parsed_config =
      registry.ParseConfig("envoy.type.matcher.v3.HttpRequestHeaderMatchInput",
                           context, input, errors);

  return registry.CreateInput(parsed_config);
}

// Function to parse "xds_core_v3_TypedExtensionConfig"  to generate
// supported Actions
std::unique_ptr<XdsMatcher::Action> ParseAction(
    const XdsResourceType::DecodeContext& context,
    const xds_core_v3_TypedExtensionConfig* action, ValidationErrors* errors) {
  ValidationErrors::ScopedField field(errors, ".action");
  const google_protobuf_Any* any =
      xds_core_v3_TypedExtensionConfig_typed_config(action);
  auto extension = ExtractXdsExtension(context, any, errors);
  if (!extension.has_value()) {
    errors->AddError("Fail to extract XdsExtenstion");
    return nullptr;
  }
  // Supported Extension check
  // Add other supported Actions here (switch case or registery ??)
  if (extension->type !=
      "envoy.extensions.filters.http.rate_limit_quota.v3."
      "RateLimitQuotaBucketSettings") {
    errors->AddError("unsupported action type");
    return nullptr;
  }
  // Parse RLQS Bucketing action
  // Move to seprate function
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
  // Create and return Bucketing Action
  if (config.map.empty()) {
    errors->AddError("bucket_id_builder map parsing failed");
    return nullptr;
  }
  return std::make_unique<BucketingAction>(config);
}

// Parse and generate input matcher with type string_view
// Parsing "xds_type_matcher_v3_StringMatcher" to generate StringMatcher
std::unique_ptr<XdsMatcherList::InputMatcher<absl::string_view>>
ParseStringMatcher(const XdsResourceType::DecodeContext& /*context*/,
                   const xds_type_matcher_v3_StringMatcher* string_matcher,
                   ValidationErrors* errors) {
  ValidationErrors::ScopedField field(errors, ".string_matcher");
  std::string matcher;
  StringMatcher::Type type;
  if (xds_type_matcher_v3_StringMatcher_has_exact(string_matcher)) {
    type = StringMatcher::Type::kExact;
    matcher = UpbStringToStdString(
        xds_type_matcher_v3_StringMatcher_exact(string_matcher));
  } else if (xds_type_matcher_v3_StringMatcher_has_prefix(string_matcher)) {
    type = StringMatcher::Type::kPrefix;
    matcher = UpbStringToStdString(
        xds_type_matcher_v3_StringMatcher_prefix(string_matcher));
  } else if (xds_type_matcher_v3_StringMatcher_has_suffix(string_matcher)) {
    type = StringMatcher::Type::kSuffix;
    matcher = UpbStringToStdString(
        xds_type_matcher_v3_StringMatcher_suffix(string_matcher));
  } else if (xds_type_matcher_v3_StringMatcher_has_contains(string_matcher)) {
    type = StringMatcher::Type::kContains;
    matcher = UpbStringToStdString(
        xds_type_matcher_v3_StringMatcher_contains(string_matcher));
  } else if (xds_type_matcher_v3_StringMatcher_has_safe_regex(string_matcher)) {
    type = StringMatcher::Type::kSafeRegex;
    auto* regex_matcher =
        xds_type_matcher_v3_StringMatcher_safe_regex(string_matcher);
    matcher = UpbStringToStdString(
        xds_type_matcher_v3_RegexMatcher_regex(regex_matcher));
  } else {
    errors->AddError("invalid StringMatcher specified");
    return nullptr;
  }
  bool ignore_case =
      xds_type_matcher_v3_StringMatcher_ignore_case(string_matcher);
  auto matcher_result = StringMatcher::Create(type, matcher, ignore_case);
  if (!matcher_result.ok()) {
    errors->AddError(absl::StrCat("Failed to create StringMatcher: ",
                                  matcher_result.status().message()));
    return nullptr;
  }
  return std::make_unique<XdsMatcherList::StringInputMatcher>(
      matcher_result.value());
}

// Parse OnMatch components of the matcher
std::unique_ptr<XdsMatcher::OnMatch> ParseOnMatch(
    const XdsResourceType::DecodeContext& context,
    const xds_type_matcher_v3_Matcher_OnMatch* on_match,
    ValidationErrors* errors) {
  ValidationErrors::ScopedField field(errors, ".OnMatch");
  // Parse keep matching once we move to latest xds protos
  bool keep_matching = false;
  // action is a variant which can have Action or a Nested Matcher
  if (xds_type_matcher_v3_Matcher_OnMatch_has_action(on_match)) {
    auto action = ParseAction(
        context, xds_type_matcher_v3_Matcher_OnMatch_action(on_match), errors);
    if (!action) {
      errors->AddError("Failed to parse action");
      return nullptr;
    }
    return std::make_unique<XdsMatcher::OnMatch>(std::move(action),
                                                 keep_matching);
  } else if (xds_type_matcher_v3_Matcher_OnMatch_has_matcher(on_match)) {
    auto nested_matcher = ParseMatcher(
        context, xds_type_matcher_v3_Matcher_OnMatch_matcher(on_match), errors);
    if (!nested_matcher) {
      errors->AddError("Failed to parse nested matcher");
      return nullptr;
    }
    return std::make_unique<XdsMatcher::OnMatch>(std::move(nested_matcher),
                                                 keep_matching);
  } else {
    errors->AddError("Unknown field in OnMatch");
    return nullptr;
  }
  return nullptr;
}

// Parse MatchTree Map
absl::flat_hash_map<std::string, std::unique_ptr<XdsMatcher::OnMatch>>
ParseMatchMap(const XdsResourceType::DecodeContext& context,
              const xds_type_matcher_v3_Matcher_MatcherTree_MatchMap* match_map,
              ValidationErrors* errors) {
  absl::flat_hash_map<std::string, std::unique_ptr<XdsMatcher::OnMatch>> result;
  ValidationErrors::ScopedField field(errors, ".match_map");
  auto iter = kUpb_Map_Begin;
  upb_StringView upb_key;
  const xds_type_matcher_v3_Matcher_OnMatch* value;
  while (xds_type_matcher_v3_Matcher_MatcherTree_MatchMap_map_next(
      match_map, &upb_key, &value, &iter)) {
    auto on_match = ParseOnMatch(context, value, errors);
    if (!on_match) {
      // should we break here with error or ignore this error
      errors->AddError("Failed to parse OnMatch");
      continue;
    }
    std::string key(UpbStringToStdString(upb_key));
    result[key] = std::move(on_match);
  }
  return result;
}

// Parse SinglePredicate
std::unique_ptr<XdsMatcherList::Predicate> ParseSinglePredicate(
    const XdsResourceType::DecodeContext& context,
    const xds_type_matcher_v3_Matcher_MatcherList_Predicate_SinglePredicate*
        single_predicate,
    ValidationErrors* errors) {
  ValidationErrors::ScopedField field(errors, ".single_predicate");
  // Supporting value match now, need to add custom match
  if (!xds_type_matcher_v3_Matcher_MatcherList_Predicate_SinglePredicate_has_value_match(
          single_predicate)) {
    return nullptr;
  }
  // StringMatcher creation from value match
  auto input_string_matcher = ParseStringMatcher(
      context,
      xds_type_matcher_v3_Matcher_MatcherList_Predicate_SinglePredicate_value_match(
          single_predicate),
      errors);
  auto input_string_value = ParseStringInput(
      context,
      xds_type_matcher_v3_Matcher_MatcherList_Predicate_SinglePredicate_input(
          single_predicate),
      errors);
  return XdsMatcherList::CreateSinglePredicate(std::move(input_string_value),
                                               std::move(input_string_matcher));
}

std::vector<std::unique_ptr<XdsMatcherList::Predicate>> ParsePredicateList(
    const XdsResourceType::DecodeContext& context,
    const xds_type_matcher_v3_Matcher_MatcherList_Predicate_PredicateList*
        predicate_list,
    ValidationErrors* errors) {
  std::vector<std::unique_ptr<XdsMatcherList::Predicate>> predicates;
  ValidationErrors::ScopedField field(errors, ".predicate_list");
  size_t predicate_list_size;
  auto list =
      xds_type_matcher_v3_Matcher_MatcherList_Predicate_PredicateList_predicate(
          predicate_list, &predicate_list_size);
  // check bpawan
  for (size_t i = 0; i < predicate_list_size; i++) {
    // Parse and push each predicate
    auto predicate = ParsePredicate(context, list[i], errors);
    if (predicate) {
      predicates.push_back(std::move(predicate));
    } else {
      // Should we break from here or ignore one of the error ??
      errors->AddError("Failed to parse predicate");
    }
  }
  return predicates;
}

// Parse Predicate field of the Matcher
std::unique_ptr<XdsMatcherList::Predicate> ParsePredicate(
    const XdsResourceType::DecodeContext& context,
    const xds_type_matcher_v3_Matcher_MatcherList_Predicate* predicate,
    ValidationErrors* errors) {
  ValidationErrors::ScopedField field(errors, ".predicate");
  // Predicate can be Single, Or, And and Not
  if (xds_type_matcher_v3_Matcher_MatcherList_Predicate_has_single_predicate(
          predicate)) {
    return ParseSinglePredicate(
        context,
        xds_type_matcher_v3_Matcher_MatcherList_Predicate_single_predicate(
            predicate),
        errors);
  } else if (xds_type_matcher_v3_Matcher_MatcherList_Predicate_has_or_matcher(
                 predicate)) {
    auto predicate_list = ParsePredicateList(
        context,
        xds_type_matcher_v3_Matcher_MatcherList_Predicate_or_matcher(predicate),
        errors);
    if (predicate_list.empty()) {
      return nullptr;
    }
    return std::make_unique<XdsMatcherList::OrPredicate>(
        std::move(predicate_list));
  } else if (xds_type_matcher_v3_Matcher_MatcherList_Predicate_has_and_matcher(
                 predicate)) {
    auto predicate_list = ParsePredicateList(
        context,
        xds_type_matcher_v3_Matcher_MatcherList_Predicate_and_matcher(
            predicate),
        errors);
    if (predicate_list.empty()) {
      return nullptr;
    }
    return std::make_unique<XdsMatcherList::AndPredicate>(
        std::move(predicate_list));
  } else if (xds_type_matcher_v3_Matcher_MatcherList_Predicate_has_not_matcher(
                 predicate)) {
    auto not_predicate = ParsePredicate(
        context,
        xds_type_matcher_v3_Matcher_MatcherList_Predicate_not_matcher(
            predicate),
        errors);
    if (!not_predicate) {
      return nullptr;
    }
    return std::make_unique<XdsMatcherList::NotPredicate>(
        std::move(not_predicate));
  }
  // Should not reach here
  errors->AddError("Unsupported value");
  return nullptr;
}

// Parse Field Matchers (List of Predicate-OnMatch pairs)
std::vector<XdsMatcherList::FieldMatcher> ParseFieldMatcherList(
    const XdsResourceType::DecodeContext& context,
    const xds_type_matcher_v3_Matcher_MatcherList* matcher_list,
    ValidationErrors* errors) {
  auto field_matcher_list = std::vector<XdsMatcherList::FieldMatcher>();
  size_t matcher_list_size;
  ValidationErrors::ScopedField field(errors, ".FieldMatcher");
  auto field_matchers = xds_type_matcher_v3_Matcher_MatcherList_matchers(
      matcher_list, &matcher_list_size);
  for (size_t i = 0; i < matcher_list_size; i++) {
    // Parse OnMatch component
    auto on_match = ParseOnMatch(
        context,
        xds_type_matcher_v3_Matcher_MatcherList_FieldMatcher_on_match(
            field_matchers[i]),
        errors);
    // Parse Predicate
    auto predicate = ParsePredicate(
        context,
        xds_type_matcher_v3_Matcher_MatcherList_FieldMatcher_predicate(
            field_matchers[i]),
        errors);
    // Create and add Field matcher in the list
    if (on_match && predicate) {
      field_matcher_list.emplace_back(std::move(predicate),
                                      std::move(on_match));
    } else {
      // should we break from error
      errors->AddError("Error in parsing field matcher");
    }
  }
  return field_matcher_list;
}

// Parse Matcher Proto
// This the top level function expected to be called for the matcher.proto
std::unique_ptr<XdsMatcher> ParseMatcher(
    const XdsResourceType::DecodeContext& context,
    const xds_type_matcher_v3_Matcher* matcher, ValidationErrors* errors) {
  ValidationErrors::ScopedField field(errors, ".Matcher");
  std::unique_ptr<XdsMatcher::OnMatch> on_no_match = nullptr;
  // Parse on_no_match if present (optional field)
  if (xds_type_matcher_v3_Matcher_has_on_no_match(matcher)) {
    on_no_match = ParseOnMatch(
        context, xds_type_matcher_v3_Matcher_on_no_match(matcher), errors);
    // Should we bail if parsing fail for on_no_match ??
  }
  // Matcher can be of type List, Map, or Custom
  if (xds_type_matcher_v3_Matcher_has_matcher_list(matcher)) {
    // Case Matcher List
    auto matcher_list = xds_type_matcher_v3_Matcher_matcher_list(matcher);
    auto field_matcher_list =
        ParseFieldMatcherList(context, matcher_list, errors);
    if (field_matcher_list.empty()) {
      errors->AddError("Field Matcher list is empty");
      return nullptr;
    }
    return std::make_unique<XdsMatcherList>(std::move(field_matcher_list),
                                            std::move(on_no_match));
  } else if (xds_type_matcher_v3_Matcher_has_matcher_tree(matcher)) {
    // Matcher Tree can be exact Match map ,Prefix match Map tree or Custom
    auto matcher_tree = xds_type_matcher_v3_Matcher_matcher_tree(matcher);
    // Exact Match Map Matcher
    if (xds_type_matcher_v3_Matcher_MatcherTree_has_exact_match_map(
            matcher_tree)) {
      auto map = ParseMatchMap(
          context,
          xds_type_matcher_v3_Matcher_MatcherTree_exact_match_map(matcher_tree),
          errors);
      auto input = ParseStringInput(
          context, xds_type_matcher_v3_Matcher_MatcherTree_input(matcher_tree),
          errors);
      if (map.empty() || !input) {
        errors->AddError("Failed to parse exact match map");
        return nullptr;
      }
      return std::make_unique<XdsMatcherExactMap>(
          std::move(input), std::move(map), std::move(on_no_match));
    } else if (xds_type_matcher_v3_Matcher_MatcherTree_has_prefix_match_map(
                   matcher_tree)) {
      // PRefix Match Map matcher
      auto map =
          ParseMatchMap(context,
                        xds_type_matcher_v3_Matcher_MatcherTree_exact_match_map(
                            matcher_tree),  // TODO(bpawan): This should likely
                                            // be prefix_match_map
                        errors);
      auto input = ParseStringInput(
          context, xds_type_matcher_v3_Matcher_MatcherTree_input(matcher_tree),
          errors);
      if (map.empty() || !input) {
        errors->AddError("Failed to parse prefix match map");
        return nullptr;
      }
      return std::make_unique<XdsMatcherPrefixMap>(
          std::move(input), std::move(map), std::move(on_no_match));
    } else if (xds_type_matcher_v3_Matcher_MatcherTree_has_custom_match(
                   matcher_tree)) {
      errors->AddError("Custom match in MatcherTree is not yet supported.");
      return nullptr;
    } else {
      // Should not reach here
      errors->AddError(
          "Invalid MatcherTree configuration: no known match type specified.");
      return nullptr;
    }
  } else {
    // Should not reach here
    errors->AddError(
        "Invalid Matcher configuration: no matcher_list or matcher_tree "
        "specified.");
    return nullptr;
  }
  return nullptr;
}
}  // namespace grpc_core