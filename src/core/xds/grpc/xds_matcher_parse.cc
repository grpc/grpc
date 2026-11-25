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

#include <memory>
#include <optional>
#include <utility>

#include "src/core/util/down_cast.h"
#include "src/core/util/upb_utils.h"
#include "src/core/xds/grpc/xds_bootstrap_grpc.h"
#include "src/core/xds/grpc/xds_common_types.h"
#include "src/core/xds/grpc/xds_common_types_parser.h"
#include "src/core/xds/grpc/xds_custom_match_registry.h"
#include "src/core/xds/grpc/xds_matcher.h"
#include "src/core/xds/grpc/xds_matcher_action.h"
#include "src/core/xds/grpc/xds_matcher_input.h"
#include "src/core/xds/xds_client/xds_client.h"
#include "xds/core/v3/extension.upb.h"
#include "xds/type/matcher/v3/matcher.upb.h"
#include "xds/type/matcher/v3/regex.upb.h"
#include "xds/type/matcher/v3/string.upb.h"
#include "absl/strings/string_view.h"

namespace grpc_core {
namespace {

// Forward declarations
std::unique_ptr<XdsMatcherList::Predicate> ParsePredicate(
    const XdsResourceType::DecodeContext& context,
    const xds_type_matcher_v3_Matcher_MatcherList_Predicate* predicate,
    const UniqueTypeName& matcher_context, ValidationErrors* errors);
std::unique_ptr<XdsMatcher> ParseXdsMatcherRecursive(
    const XdsResourceType::DecodeContext& context,
    const xds_type_matcher_v3_Matcher* matcher,
    const XdsMatcherActionRegistry& action_registry,
    const UniqueTypeName& matcher_context, bool allow_keep_matching, int depth,
    ValidationErrors* errors);

// Extracts the extension from an input "xds_core_v3_TypedExtensionConfig".
// The returned extension's validation fields remain in scope until it is
// destroyed.
std::optional<XdsExtension> ExtractInputExtension(
    const XdsResourceType::DecodeContext& context,
    const xds_core_v3_TypedExtensionConfig* input, ValidationErrors* errors) {
  if (input == nullptr) {
    errors->AddError("field not present");
    return std::nullopt;
  }
  const google_protobuf_Any* any =
      xds_core_v3_TypedExtensionConfig_typed_config(input);
  return ExtractXdsExtension(context, any, errors);
}

// The parsing is for input which return absl::string_view
std::unique_ptr<XdsMatcher::InputValue<absl::string_view>> ParseStringInput(
    const XdsResourceType::DecodeContext& context,
    const xds_core_v3_TypedExtensionConfig* input,
    const UniqueTypeName& matcher_context, ValidationErrors* errors) {
  auto extension = ExtractInputExtension(context, input, errors);
  if (!extension.has_value()) return nullptr;
  const auto& registry =
      DownCast<const GrpcXdsBootstrap&>(context.client->bootstrap())
          .matcher_string_input_registry();
  return registry.ParseAndCreateInput(context, *extension, matcher_context,
                                      errors);
}

// Parses SinglePredicate.custom_match using the custom_match registry.
std::unique_ptr<XdsCustomMatchFactory::CustomMatch> ParseCustomMatch(
    const XdsResourceType::DecodeContext& context,
    const xds_core_v3_TypedExtensionConfig* custom_match,
    ValidationErrors* errors) {
  auto extension = ExtractXdsExtension(
      context, xds_core_v3_TypedExtensionConfig_typed_config(custom_match),
      errors);
  if (!extension.has_value()) return nullptr;
  const auto& registry =
      DownCast<const GrpcXdsBootstrap&>(context.client->bootstrap())
          .custom_match_registry();
  return registry.ParseCustomMatch(context, *extension, errors);
}

// Function to parse "xds_core_v3_TypedExtensionConfig"  to generate
// supported Actions
std::unique_ptr<XdsMatcher::Action> ParseAction(
    const XdsResourceType::DecodeContext& context,
    const xds_core_v3_TypedExtensionConfig* action,
    const XdsMatcherActionRegistry& action_registry, ValidationErrors* errors) {
  const google_protobuf_Any* any =
      xds_core_v3_TypedExtensionConfig_typed_config(action);
  auto extension = ExtractXdsExtension(context, any, errors);
  if (!extension.has_value()) {
    return nullptr;
  }
  return action_registry.ParseAndCreateAction(context, *extension, errors);
}

// Parse OnMatch components of the matcher
XdsMatcher::OnMatch ParseOnMatch(
    const XdsResourceType::DecodeContext& context,
    const xds_type_matcher_v3_Matcher_OnMatch* on_match,
    const XdsMatcherActionRegistry& action_registry,
    const UniqueTypeName& matcher_context, bool allow_keep_matching, int depth,
    ValidationErrors* errors) {
  if (on_match == nullptr) {
    errors->AddError("field not present");
    return XdsMatcher::OnMatch(std::unique_ptr<XdsMatcher::Action>(nullptr),
                               false);
  }
  // Handle keep_matching.
  bool keep_matching =
      xds_type_matcher_v3_Matcher_OnMatch_keep_matching(on_match);
  if (keep_matching && !allow_keep_matching) {
    ValidationErrors::ScopedField field(errors, ".keep_matching");
    errors->AddError("not supported in this component");
  }
  // Action is a variant which can have Action or a Nested Matcher
  if (const auto* action_proto =
          xds_type_matcher_v3_Matcher_OnMatch_action(on_match);
      action_proto != nullptr) {
    ValidationErrors::ScopedField field(errors, ".action");
    auto action = ParseAction(context, action_proto, action_registry, errors);
    return XdsMatcher::OnMatch(std::move(action), keep_matching);
  } else if (const auto* matcher_proto =
                 xds_type_matcher_v3_Matcher_OnMatch_matcher(on_match);
             matcher_proto != nullptr) {
    ValidationErrors::ScopedField field(errors, ".matcher");
    auto nested_matcher = ParseXdsMatcherRecursive(
        context, matcher_proto, action_registry, matcher_context,
        allow_keep_matching, depth + 1, errors);
    return XdsMatcher::OnMatch(std::move(nested_matcher), keep_matching);
  } else {
    errors->AddError("One of action or matcher should be present");
    return XdsMatcher::OnMatch(std::unique_ptr<XdsMatcher::Action>(nullptr),
                               false);
  }
}

// Parse MatchTree Map
absl::flat_hash_map<std::string, XdsMatcher::OnMatch> ParseMatchMap(
    const XdsResourceType::DecodeContext& context,
    const xds_type_matcher_v3_Matcher_MatcherTree_MatchMap* match_map,
    const XdsMatcherActionRegistry& action_registry,
    const UniqueTypeName& matcher_context, bool allow_keep_matching, int depth,
    ValidationErrors* errors) {
  absl::flat_hash_map<std::string, XdsMatcher::OnMatch> result;
  if (xds_type_matcher_v3_Matcher_MatcherTree_MatchMap_map_size(match_map) ==
      0) {
    errors->AddError("map is empty");
    return result;
  }
  auto iter = kUpb_Map_Begin;
  upb_StringView upb_key;
  const xds_type_matcher_v3_Matcher_OnMatch* value;
  while (xds_type_matcher_v3_Matcher_MatcherTree_MatchMap_map_next(
      match_map, &upb_key, &value, &iter)) {
    ValidationErrors::ScopedField field(errors, ".on_match");
    auto on_match =
        ParseOnMatch(context, value, action_registry, matcher_context,
                     allow_keep_matching, depth, errors);
    result.emplace(UpbStringToStdString(upb_key), std::move(on_match));
  }
  return result;
}

// gRFC A106 requires prefix, suffix, contains and regex patterns to be
// non-empty.  This is checked here rather than in StringMatcherParse()
// because its other callers (e.g. route and RBAC matchers) allow empty
// patterns.
void ValidateStringMatcherPatternNotEmpty(
    const xds_type_matcher_v3_StringMatcher* proto, ValidationErrors* errors) {
  absl::string_view field;
  if (xds_type_matcher_v3_StringMatcher_has_prefix(proto)) {
    if (xds_type_matcher_v3_StringMatcher_prefix(proto).size != 0) return;
    field = ".prefix";
  } else if (xds_type_matcher_v3_StringMatcher_has_suffix(proto)) {
    if (xds_type_matcher_v3_StringMatcher_suffix(proto).size != 0) return;
    field = ".suffix";
  } else if (xds_type_matcher_v3_StringMatcher_has_contains(proto)) {
    if (xds_type_matcher_v3_StringMatcher_contains(proto).size != 0) return;
    field = ".contains";
  } else if (xds_type_matcher_v3_StringMatcher_has_safe_regex(proto)) {
    if (xds_type_matcher_v3_RegexMatcher_regex(
            xds_type_matcher_v3_StringMatcher_safe_regex(proto))
            .size != 0) {
      return;
    }
    field = ".safe_regex.regex";
  } else {
    return;
  }
  ValidationErrors::ScopedField scoped_field(errors, field);
  errors->AddError("must be non-empty");
}

// Parse SinglePredicate
std::unique_ptr<XdsMatcherList::Predicate> ParseSinglePredicate(
    const XdsResourceType::DecodeContext& context,
    const xds_type_matcher_v3_Matcher_MatcherList_Predicate_SinglePredicate*
        single_predicate,
    const UniqueTypeName& matcher_context, ValidationErrors* errors) {
  const auto* input_proto =
      xds_type_matcher_v3_Matcher_MatcherList_Predicate_SinglePredicate_input(
          single_predicate);
  if (const auto* value_match_proto =
          xds_type_matcher_v3_Matcher_MatcherList_Predicate_SinglePredicate_value_match(
              single_predicate);
      value_match_proto != nullptr) {
    std::unique_ptr<XdsMatcherList::InputMatcher<absl::string_view>> matcher;
    {
      ValidationErrors::ScopedField field(errors, ".value_match");
      ValidateStringMatcherPatternNotEmpty(value_match_proto, errors);
      matcher = std::make_unique<XdsMatcherList::StringInputMatcher>(
          StringMatcherParse(context, value_match_proto, errors));
    }
    std::unique_ptr<XdsMatcher::InputValue<absl::string_view>> input;
    {
      ValidationErrors::ScopedField field(errors, ".input");
      input = ParseStringInput(context, input_proto, matcher_context, errors);
    }
    if (input == nullptr) return nullptr;
    return XdsMatcherList::CreateSinglePredicate(std::move(input),
                                                 std::move(matcher));
  }
  if (const auto* custom_match_proto =
          xds_type_matcher_v3_Matcher_MatcherList_Predicate_SinglePredicate_custom_match(
              single_predicate);
      custom_match_proto != nullptr) {
    std::unique_ptr<XdsCustomMatchFactory::CustomMatch> custom_match;
    {
      ValidationErrors::ScopedField field(errors, ".custom_match");
      custom_match = ParseCustomMatch(context, custom_match_proto, errors);
    }
    ValidationErrors::ScopedField field(errors, ".input");
    auto input = ExtractInputExtension(context, input_proto, errors);
    if (!input.has_value() || custom_match == nullptr) return nullptr;
    return custom_match->CreatePredicate(context, *input, matcher_context,
                                         errors);
  }
  errors->AddError("one of value_match or custom_match must be set");
  return nullptr;
}

std::vector<std::unique_ptr<XdsMatcherList::Predicate>> ParsePredicateList(
    const XdsResourceType::DecodeContext& context,
    const xds_type_matcher_v3_Matcher_MatcherList_Predicate_PredicateList*
        predicate_list,
    const UniqueTypeName& matcher_context, ValidationErrors* errors) {
  std::vector<std::unique_ptr<XdsMatcherList::Predicate>> predicates;
  size_t predicate_list_size;
  auto list =
      xds_type_matcher_v3_Matcher_MatcherList_Predicate_PredicateList_predicate(
          predicate_list, &predicate_list_size);
  // gRFC A106: or_matcher and and_matcher must have at least 2 predicates.
  if (predicate_list_size < 2) {
    errors->AddError("predicate_list must contain at least 2 predicates");
    return predicates;
  }
  // gRFC A106: or_matcher and and_matcher must have at least 2 predicates.
  if (predicate_list_size < 2) {
    errors->AddError("predicate_list must contain at least 2 predicates");
    return predicates;
  }
  ValidationErrors::ScopedField field(errors, ".predicate_list");
  for (size_t i = 0; i < predicate_list_size; ++i) {
    ValidationErrors::ScopedField field(errors, absl::StrCat("[", i, "]"));
    auto predicate = ParsePredicate(context, list[i], matcher_context, errors);
    if (predicate) predicates.push_back(std::move(predicate));
  }
  return predicates;
}

// Parse Predicate field of the Matcher
std::unique_ptr<XdsMatcherList::Predicate> ParsePredicate(
    const XdsResourceType::DecodeContext& context,
    const xds_type_matcher_v3_Matcher_MatcherList_Predicate* predicate,
    const UniqueTypeName& matcher_context, ValidationErrors* errors) {
  if (predicate == nullptr) {
    errors->AddError("field not present");
    return nullptr;
  }
  if (xds_type_matcher_v3_Matcher_MatcherList_Predicate_has_single_predicate(
          predicate)) {
    ValidationErrors::ScopedField field(errors, ".single_predicate");
    return ParseSinglePredicate(
        context,
        xds_type_matcher_v3_Matcher_MatcherList_Predicate_single_predicate(
            predicate),
        matcher_context, errors);
  } else if (xds_type_matcher_v3_Matcher_MatcherList_Predicate_has_or_matcher(
                 predicate)) {
    ValidationErrors::ScopedField field(errors, ".or_matcher");
    auto predicate_list = ParsePredicateList(
        context,
        xds_type_matcher_v3_Matcher_MatcherList_Predicate_or_matcher(predicate),
        matcher_context, errors);
    return XdsMatcherList::OrPredicate::Create(std::move(predicate_list));
  } else if (xds_type_matcher_v3_Matcher_MatcherList_Predicate_has_and_matcher(
                 predicate)) {
    ValidationErrors::ScopedField field(errors, ".and_matcher");
    auto predicate_list = ParsePredicateList(
        context,
        xds_type_matcher_v3_Matcher_MatcherList_Predicate_and_matcher(
            predicate),
        matcher_context, errors);
    return XdsMatcherList::AndPredicate::Create(std::move(predicate_list));
  } else if (xds_type_matcher_v3_Matcher_MatcherList_Predicate_has_not_matcher(
                 predicate)) {
    ValidationErrors::ScopedField field(errors, ".not_matcher");
    auto not_predicate = ParsePredicate(
        context,
        xds_type_matcher_v3_Matcher_MatcherList_Predicate_not_matcher(
            predicate),
        matcher_context, errors);
    return XdsMatcherList::NotPredicate::Create(std::move(not_predicate));
  }
  errors->AddError("unsupported predicate type");
  return nullptr;
}

// Parse Field Matchers (List of Predicate-OnMatch pairs)
std::vector<XdsMatcherList::FieldMatcher> ParseFieldMatcherList(
    const XdsResourceType::DecodeContext& context,
    const xds_type_matcher_v3_Matcher_MatcherList* matcher_list,
    const XdsMatcherActionRegistry& action_registry,
    const UniqueTypeName& matcher_context, bool allow_keep_matching, int depth,
    ValidationErrors* errors) {
  std::vector<XdsMatcherList::FieldMatcher> field_matcher_list;
  size_t matcher_list_size;
  auto field_matchers = xds_type_matcher_v3_Matcher_MatcherList_matchers(
      matcher_list, &matcher_list_size);
  if (matcher_list_size == 0) {
    errors->AddError("matcher_list is empty");
    return field_matcher_list;
  }
  ValidationErrors::ScopedField field(errors, ".matchers");
  for (size_t i = 0; i < matcher_list_size; ++i) {
    ValidationErrors::ScopedField field(errors, absl::StrCat("[", i, "]"));
    auto on_match = [&]() {
      ValidationErrors::ScopedField field(errors, ".on_match");
      auto* on_match_upb =
          xds_type_matcher_v3_Matcher_MatcherList_FieldMatcher_on_match(
              field_matchers[i]);
      return ParseOnMatch(context, on_match_upb, action_registry,
                          matcher_context, allow_keep_matching, depth, errors);
    }();
    auto predicate = [&]() {
      ValidationErrors::ScopedField field(errors, ".predicate");
      auto* predicate_upb =
          xds_type_matcher_v3_Matcher_MatcherList_FieldMatcher_predicate(
              field_matchers[i]);
      return ParsePredicate(context, predicate_upb, matcher_context, errors);
    }();
    field_matcher_list.emplace_back(std::move(predicate), std::move(on_match));
  }
  return field_matcher_list;
}

std::unique_ptr<XdsMatcher> ParseXdsMatcherRecursive(
    const XdsResourceType::DecodeContext& context,
    const xds_type_matcher_v3_Matcher* matcher,
    const XdsMatcherActionRegistry& action_registry,
    const UniqueTypeName& matcher_context, bool allow_keep_matching, int depth,
    ValidationErrors* errors) {
  if (depth == 16) {
    errors->AddError("matcher tree exceeds max recursion depth");
    return nullptr;
  }
  std::optional<XdsMatcher::OnMatch> on_no_match;
  if (xds_type_matcher_v3_Matcher_has_on_no_match(matcher)) {
    ValidationErrors::ScopedField field(errors, ".on_no_match");
    on_no_match = ParseOnMatch(
        context, xds_type_matcher_v3_Matcher_on_no_match(matcher),
        action_registry, matcher_context, allow_keep_matching, depth, errors);
  }
  if (xds_type_matcher_v3_Matcher_has_matcher_list(matcher)) {
    ValidationErrors::ScopedField field(errors, ".matcher_list");
    auto matcher_list = xds_type_matcher_v3_Matcher_matcher_list(matcher);
    auto field_matcher_list = ParseFieldMatcherList(
        context, matcher_list, action_registry, matcher_context,
        allow_keep_matching, depth, errors);
    return std::make_unique<XdsMatcherList>(std::move(field_matcher_list),
                                            std::move(on_no_match));
  } else if (xds_type_matcher_v3_Matcher_has_matcher_tree(matcher)) {
    ValidationErrors::ScopedField field(errors, ".matcher_tree");
    auto matcher_tree = xds_type_matcher_v3_Matcher_matcher_tree(matcher);
    std::unique_ptr<XdsMatcher::InputValue<absl::string_view>> input;
    auto* input_upb =
        xds_type_matcher_v3_Matcher_MatcherTree_input(matcher_tree);
    {
      ValidationErrors::ScopedField field(errors, ".input");
      input = ParseStringInput(context, input_upb, matcher_context, errors);
    }
    if (xds_type_matcher_v3_Matcher_MatcherTree_has_exact_match_map(
            matcher_tree)) {
      ValidationErrors::ScopedField field(errors, ".exact_match_map");
      auto map = ParseMatchMap(
          context,
          xds_type_matcher_v3_Matcher_MatcherTree_exact_match_map(matcher_tree),
          action_registry, matcher_context, allow_keep_matching, depth, errors);
      return std::make_unique<XdsMatcherExactMap>(
          std::move(input), std::move(map), std::move(on_no_match));
    } else if (xds_type_matcher_v3_Matcher_MatcherTree_has_prefix_match_map(
                   matcher_tree)) {
      ValidationErrors::ScopedField field(errors, ".prefix_match_map");
      auto map = ParseMatchMap(
          context,
          xds_type_matcher_v3_Matcher_MatcherTree_prefix_match_map(
              matcher_tree),
          action_registry, matcher_context, allow_keep_matching, depth, errors);
      return std::make_unique<XdsMatcherPrefixMap>(
          std::move(input), std::move(map), std::move(on_no_match));
    } else {
      errors->AddError("no known match tree type specified");
    }
  } else {
    errors->AddError(
        "no matcher_list or matcher_tree "
        "specified.");
  }
  return nullptr;
}

}  // namespace

// Parse Matcher Proto
// This the top level function expected to be called for the matcher.proto
std::unique_ptr<XdsMatcher> ParseXdsMatcher(
    const XdsResourceType::DecodeContext& context,
    const xds_type_matcher_v3_Matcher* matcher,
    const XdsMatcherActionRegistry& action_registry,
    const UniqueTypeName& matcher_context, bool allow_keep_matching,
    ValidationErrors* errors) {
  if (matcher == nullptr) {
    errors->AddError("field not set");
    return nullptr;
  }
  return ParseXdsMatcherRecursive(context, matcher, action_registry,
                                  matcher_context, allow_keep_matching,
                                  /*depth=*/0, errors);
}

}  // namespace grpc_core
