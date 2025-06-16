#include <upb/reflection/common.h>

#include <memory>
#include <string>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/strings/string_view.h"
#include "envoy/config/common/matcher/v3/matcher.upb.h"
#include "envoy/type/matcher/v3/string.upb.h"
#include "src/core/xds/grpc/xds_common_types.h"
#include "src/core/xds/grpc/xds_common_types_parser.h"
#include "src/core/xds/grpc/xds_matcher.h"

namespace grpc_core {

// Parses a TypedExtensionConfig representing a string input.
std::unique_ptr<XdsMatcher::InputValue<absl::string_view>> ParseStringInput(
    const XdsResourceType::DecodeContext& context,
    const envoy_config_core_v3_TypedExtensionConfig* input,
    ValidationErrors* errors);

// Parses an envoy_type_matcher_v3_StringMatcher.
std::unique_ptr<XdsMatcherList::InputMatcher<absl::string_view>>
ParseStringMatcher(const XdsResourceType::DecodeContext& context,
                   const envoy_type_matcher_v3_StringMatcher* string_matcher,
                   ValidationErrors* errors);

// Parses a TypedExtensionConfig representing an action.
std::unique_ptr<XdsMatcher::Action> ParseAction(
    const XdsResourceType::DecodeContext& context,
    const envoy_config_core_v3_TypedExtensionConfig* action,
    ValidationErrors* errors);

// Parses an envoy_config_common_matcher_v3_Matcher_OnMatch.
std::unique_ptr<XdsMatcher::OnMatch> ParseOnMatch(
    const XdsResourceType::DecodeContext& context,
    const envoy_config_common_matcher_v3_Matcher_OnMatch* on_match,
    ValidationErrors* errors);

// Parses an envoy_config_common_matcher_v3_Matcher_MatcherTree_MatchMap.
absl::flat_hash_map<std::string, std::unique_ptr<XdsMatcher::OnMatch>>
ParseMatchMap(const XdsResourceType::DecodeContext& context,
              const envoy_config_common_matcher_v3_Matcher_MatcherTree_MatchMap*
                  match_map,
              ValidationErrors* errors);

// Parses an
// envoy_config_common_matcher_v3_Matcher_MatcherList_Predicate_SinglePredicate.
std::unique_ptr<XdsMatcherList::Predicate> ParseSinglePredicate(
    const XdsResourceType::DecodeContext& context,
    const envoy_config_common_matcher_v3_Matcher_MatcherList_Predicate_SinglePredicate*
        single_predicate,
    ValidationErrors* errors);

// Parses an envoy_config_common_matcher_v3_Matcher_MatcherList_Predicate.
std::unique_ptr<XdsMatcherList::Predicate> ParsePredicate(
    const XdsResourceType::DecodeContext& context,
    const envoy_config_common_matcher_v3_Matcher_MatcherList_Predicate*
        predicate,
    ValidationErrors* errors);

// Parses an
// envoy_config_common_matcher_v3_Matcher_MatcherList_Predicate_PredicateList.
std::vector<std::unique_ptr<XdsMatcherList::Predicate>> ParsePredicateList(
    const XdsResourceType::DecodeContext& context,
    const envoy_config_common_matcher_v3_Matcher_MatcherList_Predicate_PredicateList*
        predicate_list,
    ValidationErrors* errors);

// Parses a list of FieldMatchers from
// envoy_config_common_matcher_v3_Matcher_MatcherList.
std::vector<XdsMatcherList::FieldMatcher> ParseFieldMatcherList(
    const XdsResourceType::DecodeContext& context,
    const envoy_config_common_matcher_v3_Matcher_MatcherList* matcher_list,
    ValidationErrors* errors);

// Parses an envoy_config_common_matcher_v3_Matcher.
std::unique_ptr<XdsMatcher> ParseMatcher(
    const XdsResourceType::DecodeContext& context,
    const envoy_config_common_matcher_v3_Matcher* matcher,
    ValidationErrors* errors);

}  // namespace grpc_core