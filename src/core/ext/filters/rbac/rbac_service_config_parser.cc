//
// Copyright 2021 gRPC authors.
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

#include "src/core/ext/filters/rbac/rbac_service_config_parser.h"

#include <stdint.h>

#include <map>
#include <string>

#include "absl/memory/memory.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/types/optional.h"

#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/iomgr/error.h"
#include "src/core/lib/json/json_util.h"
#include "src/core/lib/matchers/matchers.h"
#include "src/core/lib/transport/error_utils.h"

namespace grpc_core {

namespace {

std::string ParseRegexMatcher(const Json::Object& regex_matcher_json,
                              std::vector<grpc_error_handle>* error_list) {
  std::string regex;
  ParseJsonObjectField(regex_matcher_json, "regex", &regex, error_list);
  return regex;
}

absl::StatusOr<HeaderMatcher> ParseHeaderMatcher(
    const Json::Object& header_matcher_json,
    std::vector<grpc_error_handle>* error_list) {
  std::string name;
  ParseJsonObjectField(header_matcher_json, "name", &name, error_list);
  std::string match;
  HeaderMatcher::Type type = HeaderMatcher::Type();
  const Json::Object* inner_json;
  int64_t start = 0;
  int64_t end = 0;
  bool present_match = false;
  bool invert_match = false;
  ParseJsonObjectField(header_matcher_json, "invertMatch", &invert_match,
                       error_list, /*required=*/false);
  if (ParseJsonObjectField(header_matcher_json, "exactMatch", &match,
                           error_list, /*required=*/false)) {
    type = HeaderMatcher::Type::kExact;
  } else if (ParseJsonObjectField(header_matcher_json, "safeRegexMatch",
                                  &inner_json, error_list,
                                  /*required=*/false)) {
    type = HeaderMatcher::Type::kSafeRegex;
    std::vector<grpc_error_handle> safe_regex_matcher_error_list;
    match = ParseRegexMatcher(*inner_json, &safe_regex_matcher_error_list);
    if (!safe_regex_matcher_error_list.empty()) {
      error_list->push_back(GRPC_ERROR_CREATE_FROM_VECTOR(
          "safeRegexMatch", &safe_regex_matcher_error_list));
    }
  } else if (ParseJsonObjectField(header_matcher_json, "rangeMatch",
                                  &inner_json, error_list,
                                  /*required=*/false)) {
    type = HeaderMatcher::Type::kRange;
    std::vector<grpc_error_handle> range_error_list;
    ParseJsonObjectField(*inner_json, "start", &start, &range_error_list);
    ParseJsonObjectField(*inner_json, "end", &end, &range_error_list);
    if (!range_error_list.empty()) {
      error_list->push_back(
          GRPC_ERROR_CREATE_FROM_VECTOR("rangeMatch", &range_error_list));
    }
  } else if (ParseJsonObjectField(header_matcher_json, "presentMatch",
                                  &present_match, error_list,
                                  /*required=*/false)) {
    type = HeaderMatcher::Type::kPresent;
  } else if (ParseJsonObjectField(header_matcher_json, "prefixMatch", &match,
                                  error_list, /*required=*/false)) {
    type = HeaderMatcher::Type::kPrefix;
  } else if (ParseJsonObjectField(header_matcher_json, "suffixMatch", &match,
                                  error_list, /*required=*/false)) {
    type = HeaderMatcher::Type::kSuffix;
  } else if (ParseJsonObjectField(header_matcher_json, "containsMatch", &match,
                                  error_list, /*required=*/false)) {
    type = HeaderMatcher::Type::kContains;
  } else {
    return absl::InvalidArgumentError("No valid matcher found");
  }
  return HeaderMatcher::Create(name, type, match, start, end, present_match,
                               invert_match);
}

absl::StatusOr<StringMatcher> ParseStringMatcher(
    const Json::Object& string_matcher_json,
    std::vector<grpc_error_handle>* error_list) {
  std::string match;
  StringMatcher::Type type = StringMatcher::Type();
  const Json::Object* inner_json;
  bool ignore_case = false;
  ParseJsonObjectField(string_matcher_json, "ignoreCase", &ignore_case,
                       error_list, /*required=*/false);
  if (ParseJsonObjectField(string_matcher_json, "exact", &match, error_list,
                           /*required=*/false)) {
    type = StringMatcher::Type::kExact;
  } else if (ParseJsonObjectField(string_matcher_json, "prefix", &match,
                                  error_list, /*required=*/false)) {
    type = StringMatcher::Type::kPrefix;
  } else if (ParseJsonObjectField(string_matcher_json, "suffix", &match,
                                  error_list, /*required=*/false)) {
    type = StringMatcher::Type::kSuffix;
  } else if (ParseJsonObjectField(string_matcher_json, "safeRegex", &inner_json,
                                  error_list, /*required=*/false)) {
    type = StringMatcher::Type::kSafeRegex;
    std::vector<grpc_error_handle> safe_regex_matcher_error_list;
    match = ParseRegexMatcher(*inner_json, &safe_regex_matcher_error_list);
    if (!safe_regex_matcher_error_list.empty()) {
      error_list->push_back(GRPC_ERROR_CREATE_FROM_VECTOR(
          "safeRegex", &safe_regex_matcher_error_list));
    }
  } else if (ParseJsonObjectField(string_matcher_json, "contains", &match,
                                  error_list, /*required=*/false)) {
    type = StringMatcher::Type::kContains;
  } else {
    return absl::InvalidArgumentError("No valid matcher found");
  }
  return StringMatcher::Create(type, match, ignore_case);
}

absl::StatusOr<StringMatcher> ParsePathMatcher(
    const Json::Object& path_matcher_json,
    std::vector<grpc_error_handle>* error_list) {
  const Json::Object* string_matcher_json;
  if (ParseJsonObjectField(path_matcher_json, "path", &string_matcher_json,
                           error_list)) {
    std::vector<grpc_error_handle> sub_error_list;
    auto matcher = ParseStringMatcher(*string_matcher_json, &sub_error_list);
    if (!sub_error_list.empty()) {
      error_list->push_back(
          GRPC_ERROR_CREATE_FROM_VECTOR("path", &sub_error_list));
    }
    return matcher;
  }
  return absl::InvalidArgumentError("No path found");
}

Rbac::CidrRange ParseCidrRange(const Json::Object& cidr_range_json,
                               std::vector<grpc_error_handle>* error_list) {
  std::string address_prefix;
  ParseJsonObjectField(cidr_range_json, "addressPrefix", &address_prefix,
                       error_list);
  const Json::Object* uint32_json;
  uint32_t prefix_len = 0;  // default value
  if (ParseJsonObjectField(cidr_range_json, "prefixLen", &uint32_json,
                           error_list, /*required=*/false)) {
    std::vector<grpc_error_handle> sub_error_list;
    ParseJsonObjectField(*uint32_json, "value", &prefix_len, &sub_error_list);
    if (!sub_error_list.empty()) {
      error_list->push_back(
          GRPC_ERROR_CREATE_FROM_VECTOR("prefixLen", &sub_error_list));
    }
  }
  return Rbac::CidrRange(std::move(address_prefix), prefix_len);
}

Rbac::Permission ParsePermission(const Json::Object& permission_json,
                                 std::vector<grpc_error_handle>* error_list) {
  auto parse_permission_set = [](const Json::Object& permission_set_json,
                                 std::vector<grpc_error_handle>* error_list) {
    const Json::Array* rules_json;
    std::vector<std::unique_ptr<Rbac::Permission>> permissions;
    if (ParseJsonObjectField(permission_set_json, "rules", &rules_json,
                             error_list)) {
      for (size_t i = 0; i < rules_json->size(); ++i) {
        const Json::Object* permission_json;
        if (!ExtractJsonType((*rules_json)[i],
                             absl::StrFormat("rules[%d]", i).c_str(),
                             &permission_json, error_list)) {
          continue;
        }
        std::vector<grpc_error_handle> permission_error_list;
        permissions.emplace_back(absl::make_unique<Rbac::Permission>(
            ParsePermission(*permission_json, &permission_error_list)));
        if (!permission_error_list.empty()) {
          error_list->push_back(GRPC_ERROR_CREATE_FROM_VECTOR_AND_CPP_STRING(
              absl::StrFormat("rules[%d]", i), &permission_error_list));
        }
      }
    }
    return permissions;
  };
  Rbac::Permission permission;
  const Json::Object* inner_json;
  bool any;
  int port;
  if (ParseJsonObjectField(permission_json, "andRules", &inner_json, error_list,
                           /*required=*/false)) {
    std::vector<grpc_error_handle> and_rules_error_list;
    permission = Rbac::Permission::MakeAndPermission(
        parse_permission_set(*inner_json, &and_rules_error_list));
    if (!and_rules_error_list.empty()) {
      error_list->push_back(
          GRPC_ERROR_CREATE_FROM_VECTOR("andRules", &and_rules_error_list));
    }
  } else if (ParseJsonObjectField(permission_json, "orRules", &inner_json,
                                  error_list, /*required=*/false)) {
    std::vector<grpc_error_handle> or_rules_error_list;
    permission = Rbac::Permission::MakeOrPermission(
        parse_permission_set(*inner_json, &or_rules_error_list));
    if (!or_rules_error_list.empty()) {
      error_list->push_back(
          GRPC_ERROR_CREATE_FROM_VECTOR("orRules", &or_rules_error_list));
    }
  } else if (ParseJsonObjectField(permission_json, "any", &any, error_list,
                                  /*required=*/false) &&
             any) {
    permission = Rbac::Permission::MakeAnyPermission();
  } else if (ParseJsonObjectField(permission_json, "header", &inner_json,
                                  error_list,
                                  /*required=*/false)) {
    std::vector<grpc_error_handle> header_error_list;
    auto matcher = ParseHeaderMatcher(*inner_json, &header_error_list);
    if (matcher.ok()) {
      permission = Rbac::Permission::MakeHeaderPermission(*matcher);
    } else {
      header_error_list.push_back(absl_status_to_grpc_error(matcher.status()));
    }
    if (!header_error_list.empty()) {
      error_list->push_back(
          GRPC_ERROR_CREATE_FROM_VECTOR("header", &header_error_list));
    }
  } else if (ParseJsonObjectField(permission_json, "urlPath", &inner_json,
                                  error_list,
                                  /*required=*/false)) {
    std::vector<grpc_error_handle> url_path_error_list;
    auto matcher = ParsePathMatcher(*inner_json, &url_path_error_list);
    if (matcher.ok()) {
      permission = Rbac::Permission::MakePathPermission(*matcher);
    } else {
      url_path_error_list.push_back(
          absl_status_to_grpc_error(matcher.status()));
    }
    if (!url_path_error_list.empty()) {
      error_list->push_back(
          GRPC_ERROR_CREATE_FROM_VECTOR("urlPath", &url_path_error_list));
    }
  } else if (ParseJsonObjectField(permission_json, "destinationIp", &inner_json,
                                  error_list, /*required=*/false)) {
    std::vector<grpc_error_handle> destination_ip_error_list;
    permission = Rbac::Permission::MakeDestIpPermission(
        ParseCidrRange(*inner_json, &destination_ip_error_list));
    if (!destination_ip_error_list.empty()) {
      error_list->push_back(GRPC_ERROR_CREATE_FROM_VECTOR(
          "destinationIp", &destination_ip_error_list));
    }
  } else if (ParseJsonObjectField(permission_json, "destinationPort", &port,
                                  error_list, /*required=*/false)) {
    permission = Rbac::Permission::MakeDestPortPermission(port);
  } else if (ParseJsonObjectField(permission_json, "metadata", &inner_json,
                                  error_list, /*required=*/false)) {
    std::vector<grpc_error_handle> metadata_error_list;
    bool invert = false;
    ParseJsonObjectField(*inner_json, "invert", &invert, &metadata_error_list,
                         /*required=*/false);
    if (metadata_error_list.empty()) {
      permission = Rbac::Permission::MakeMetadataPermission(invert);
    } else {
      error_list->push_back(
          GRPC_ERROR_CREATE_FROM_VECTOR("metadata", &metadata_error_list));
    }
  } else if (ParseJsonObjectField(permission_json, "notRule", &inner_json,
                                  error_list, /*required=*/false)) {
    std::vector<grpc_error_handle> not_rule_error_list;
    permission = Rbac::Permission::MakeNotPermission(
        ParsePermission(*inner_json, &not_rule_error_list));
    if (!not_rule_error_list.empty()) {
      error_list->push_back(
          GRPC_ERROR_CREATE_FROM_VECTOR("notRule", &not_rule_error_list));
    }
  } else if (ParseJsonObjectField(permission_json, "requestedServerName",
                                  &inner_json, error_list,
                                  /*required=*/false)) {
    std::vector<grpc_error_handle> req_server_name_error_list;
    auto matcher = ParseStringMatcher(*inner_json, &req_server_name_error_list);
    if (matcher.ok()) {
      permission = Rbac::Permission::MakeReqServerNamePermission(*matcher);
    } else {
      req_server_name_error_list.push_back(
          absl_status_to_grpc_error(matcher.status()));
    }
    if (!req_server_name_error_list.empty()) {
      error_list->push_back(GRPC_ERROR_CREATE_FROM_VECTOR(
          "requestedServerName", &req_server_name_error_list));
    }
  } else {
    error_list->push_back(
        GRPC_ERROR_CREATE_FROM_STATIC_STRING("No valid rule found"));
  }
  return permission;
}

Rbac::Principal ParsePrincipal(const Json::Object& principal_json,
                               std::vector<grpc_error_handle>* error_list) {
  auto parse_principal_set = [](const Json::Object& principal_set_json,
                                std::vector<grpc_error_handle>* error_list) {
    const Json::Array* rules_json;
    std::vector<std::unique_ptr<Rbac::Principal>> principals;
    if (ParseJsonObjectField(principal_set_json, "ids", &rules_json,
                             error_list)) {
      for (size_t i = 0; i < rules_json->size(); ++i) {
        const Json::Object* principal_json;
        if (!ExtractJsonType((*rules_json)[i],
                             absl::StrFormat("ids[%d]", i).c_str(),
                             &principal_json, error_list)) {
          continue;
        }
        std::vector<grpc_error_handle> principal_error_list;
        principals.emplace_back(absl::make_unique<Rbac::Principal>(
            ParsePrincipal(*principal_json, &principal_error_list)));
        if (!principal_error_list.empty()) {
          error_list->push_back(GRPC_ERROR_CREATE_FROM_VECTOR_AND_CPP_STRING(
              absl::StrFormat("ids[%d]", i), &principal_error_list));
        }
      }
    }
    return principals;
  };
  Rbac::Principal principal;
  const Json::Object* inner_json;
  bool any;
  if (ParseJsonObjectField(principal_json, "andIds", &inner_json, error_list,
                           /*required=*/false)) {
    std::vector<grpc_error_handle> and_rules_error_list;
    principal = Rbac::Principal::MakeAndPrincipal(
        parse_principal_set(*inner_json, &and_rules_error_list));
    if (!and_rules_error_list.empty()) {
      error_list->push_back(
          GRPC_ERROR_CREATE_FROM_VECTOR("andIds", &and_rules_error_list));
    }
  } else if (ParseJsonObjectField(principal_json, "orIds", &inner_json,
                                  error_list, /*required=*/false)) {
    std::vector<grpc_error_handle> or_rules_error_list;
    principal = Rbac::Principal::MakeOrPrincipal(
        parse_principal_set(*inner_json, &or_rules_error_list));
    if (!or_rules_error_list.empty()) {
      error_list->push_back(
          GRPC_ERROR_CREATE_FROM_VECTOR("orIds", &or_rules_error_list));
    }
  } else if (ParseJsonObjectField(principal_json, "any", &any, error_list,
                                  /*required=*/false) &&
             any) {
    principal = Rbac::Principal::MakeAnyPrincipal();
  } else if (ParseJsonObjectField(principal_json, "authenticated", &inner_json,
                                  error_list, /*required=*/false)) {
    std::vector<grpc_error_handle> authenticated_error_list;
    const Json::Object* principal_name_json;
    if (ParseJsonObjectField(*inner_json, "principalName", &principal_name_json,
                             &authenticated_error_list, /*required=*/false)) {
      std::vector<grpc_error_handle> principal_name_error_list;
      auto matcher =
          ParseStringMatcher(*principal_name_json, &principal_name_error_list);
      if (matcher.ok()) {
        principal = Rbac::Principal::MakeAuthenticatedPrincipal(*matcher);
      } else {
        principal_name_error_list.push_back(
            absl_status_to_grpc_error(matcher.status()));
      }
      if (!principal_name_error_list.empty()) {
        authenticated_error_list.push_back(GRPC_ERROR_CREATE_FROM_VECTOR(
            "principalName", &principal_name_error_list));
      }
    } else if (authenticated_error_list.empty()) {
      // No principalName found. Match for all users.
      principal = Rbac::Principal::MakeAnyPrincipal();
    } else {
      error_list->push_back(GRPC_ERROR_CREATE_FROM_VECTOR(
          "authenticated", &authenticated_error_list));
    }
  } else if (ParseJsonObjectField(principal_json, "sourceIp", &inner_json,
                                  error_list, /*required=*/false)) {
    std::vector<grpc_error_handle> source_ip_error_list;
    principal = Rbac::Principal::MakeSourceIpPrincipal(
        ParseCidrRange(*inner_json, &source_ip_error_list));
    if (!source_ip_error_list.empty()) {
      error_list->push_back(
          GRPC_ERROR_CREATE_FROM_VECTOR("sourceIp", &source_ip_error_list));
    }
  } else if (ParseJsonObjectField(principal_json, "directRemoteIp", &inner_json,
                                  error_list, /*required=*/false)) {
    std::vector<grpc_error_handle> direct_remote_ip_error_list;
    principal = Rbac::Principal::MakeDirectRemoteIpPrincipal(
        ParseCidrRange(*inner_json, &direct_remote_ip_error_list));
    if (!direct_remote_ip_error_list.empty()) {
      error_list->push_back(GRPC_ERROR_CREATE_FROM_VECTOR(
          "directRemoteIp", &direct_remote_ip_error_list));
    }
  } else if (ParseJsonObjectField(principal_json, "remoteIp", &inner_json,
                                  error_list, /*required=*/false)) {
    std::vector<grpc_error_handle> remote_ip_error_list;
    principal = Rbac::Principal::MakeRemoteIpPrincipal(
        ParseCidrRange(*inner_json, &remote_ip_error_list));
    if (!remote_ip_error_list.empty()) {
      error_list->push_back(
          GRPC_ERROR_CREATE_FROM_VECTOR("remoteIp", &remote_ip_error_list));
    }
  } else if (ParseJsonObjectField(principal_json, "header", &inner_json,
                                  error_list,
                                  /*required=*/false)) {
    std::vector<grpc_error_handle> header_error_list;
    auto matcher = ParseHeaderMatcher(*inner_json, &header_error_list);
    if (matcher.ok()) {
      principal = Rbac::Principal::MakeHeaderPrincipal(*matcher);
    } else {
      header_error_list.push_back(absl_status_to_grpc_error(matcher.status()));
    }
    if (!header_error_list.empty()) {
      error_list->push_back(
          GRPC_ERROR_CREATE_FROM_VECTOR("header", &header_error_list));
    }
  } else if (ParseJsonObjectField(principal_json, "urlPath", &inner_json,
                                  error_list,
                                  /*required=*/false)) {
    std::vector<grpc_error_handle> url_path_error_list;
    auto matcher = ParsePathMatcher(*inner_json, &url_path_error_list);
    if (matcher.ok()) {
      principal = Rbac::Principal::MakePathPrincipal(*matcher);
    } else {
      url_path_error_list.push_back(
          absl_status_to_grpc_error(matcher.status()));
    }
    if (!url_path_error_list.empty()) {
      error_list->push_back(
          GRPC_ERROR_CREATE_FROM_VECTOR("urlPath", &url_path_error_list));
    }
  } else if (ParseJsonObjectField(principal_json, "metadata", &inner_json,
                                  error_list, /*required=*/false)) {
    std::vector<grpc_error_handle> metadata_error_list;
    bool invert = false;
    ParseJsonObjectField(*inner_json, "invert", &invert, &metadata_error_list,
                         /*required=*/false);
    if (metadata_error_list.empty()) {
      principal = Rbac::Principal::MakeMetadataPrincipal(invert);
    } else {
      error_list->push_back(
          GRPC_ERROR_CREATE_FROM_VECTOR("metadata", &metadata_error_list));
    }
  } else if (ParseJsonObjectField(principal_json, "notId", &inner_json,
                                  error_list, /*required=*/false)) {
    std::vector<grpc_error_handle> not_rule_error_list;
    principal = Rbac::Principal::MakeNotPrincipal(
        ParsePrincipal(*inner_json, &not_rule_error_list));
    if (!not_rule_error_list.empty()) {
      error_list->push_back(
          GRPC_ERROR_CREATE_FROM_VECTOR("notId", &not_rule_error_list));
    }
  } else {
    error_list->push_back(
        GRPC_ERROR_CREATE_FROM_STATIC_STRING("No valid id found"));
  }
  return principal;
}

Rbac::Policy ParsePolicy(const Json::Object& policy_json,
                         std::vector<grpc_error_handle>* error_list) {
  Rbac::Policy policy;
  const Json::Array* permissions_json_array;
  std::vector<std::unique_ptr<Rbac::Permission>> permissions;
  if (ParseJsonObjectField(policy_json, "permissions", &permissions_json_array,
                           error_list)) {
    for (size_t i = 0; i < permissions_json_array->size(); ++i) {
      const Json::Object* permission_json;
      if (!ExtractJsonType((*permissions_json_array)[i],
                           absl::StrFormat("permissions[%d]", i),
                           &permission_json, error_list)) {
        continue;
      }
      std::vector<grpc_error_handle> permission_error_list;
      permissions.emplace_back(absl::make_unique<Rbac::Permission>(
          ParsePermission(*permission_json, &permission_error_list)));
      if (!permission_error_list.empty()) {
        error_list->push_back(GRPC_ERROR_CREATE_FROM_VECTOR_AND_CPP_STRING(
            absl::StrFormat("permissions[%d]", i), &permission_error_list));
      }
    }
  }
  const Json::Array* principals_json_array;
  std::vector<std::unique_ptr<Rbac::Principal>> principals;
  if (ParseJsonObjectField(policy_json, "principals", &principals_json_array,
                           error_list)) {
    for (size_t i = 0; i < principals_json_array->size(); ++i) {
      const Json::Object* principal_json;
      if (!ExtractJsonType((*principals_json_array)[i],
                           absl::StrFormat("principals[%d]", i),
                           &principal_json, error_list)) {
        continue;
      }
      std::vector<grpc_error_handle> principal_error_list;
      principals.emplace_back(absl::make_unique<Rbac::Principal>(
          ParsePrincipal(*principal_json, &principal_error_list)));
      if (!principal_error_list.empty()) {
        error_list->push_back(GRPC_ERROR_CREATE_FROM_VECTOR_AND_CPP_STRING(
            absl::StrFormat("principals[%d]", i), &principal_error_list));
      }
    }
  }
  policy.permissions =
      Rbac::Permission::MakeOrPermission(std::move(permissions));
  policy.principals = Rbac::Principal::MakeOrPrincipal(std::move(principals));
  return policy;
}

Rbac ParseRbac(const Json::Object& rbac_json,
               std::vector<grpc_error_handle>* error_list) {
  Rbac rbac;
  const Json::Object* rules_json;
  if (!ParseJsonObjectField(rbac_json, "rules", &rules_json, error_list,
                            /*required=*/false)) {
    // No enforcing to be applied. An empty deny policy with an empty map is
    // equivalent to no enforcing.
    return Rbac(Rbac::Action::kDeny, {});
  }
  int action;
  if (ParseJsonObjectField(*rules_json, "action", &action, error_list)) {
    if (action > 1) {
      error_list->push_back(
          GRPC_ERROR_CREATE_FROM_STATIC_STRING("Unknown action"));
    }
  }
  rbac.action = static_cast<Rbac::Action>(action);
  const Json::Object* policies_json;
  if (ParseJsonObjectField(*rules_json, "policies", &policies_json, error_list,
                           /*required=*/false)) {
    for (const auto& entry : *policies_json) {
      std::vector<grpc_error_handle> policy_error_list;
      rbac.policies.emplace(
          entry.first,
          ParsePolicy(entry.second.object_value(), &policy_error_list));
      if (!policy_error_list.empty()) {
        error_list->push_back(GRPC_ERROR_CREATE_FROM_VECTOR_AND_CPP_STRING(
            absl::StrFormat("policies key:'%s'", entry.first.c_str()),
            &policy_error_list));
      }
    }
  }
  return rbac;
}

std::vector<Rbac> ParseRbacArray(const Json::Array& policies_json_array,
                                 std::vector<grpc_error_handle>* error_list) {
  std::vector<Rbac> policies;
  for (size_t i = 0; i < policies_json_array.size(); ++i) {
    const Json::Object* rbac_json;
    if (!ExtractJsonType(policies_json_array[i],
                         absl::StrFormat("rbacPolicy[%d]", i), &rbac_json,
                         error_list)) {
      continue;
    }
    std::vector<grpc_error_handle> rbac_policy_error_list;
    policies.emplace_back(ParseRbac(*rbac_json, &rbac_policy_error_list));
    if (!rbac_policy_error_list.empty()) {
      error_list->push_back(GRPC_ERROR_CREATE_FROM_VECTOR_AND_CPP_STRING(
          absl::StrFormat("rbacPolicy[%d]", i), &rbac_policy_error_list));
    }
  }
  return policies;
}

}  // namespace

absl::StatusOr<std::unique_ptr<ServiceConfigParser::ParsedConfig>>
RbacServiceConfigParser::ParsePerMethodParams(const ChannelArgs& args,
                                              const Json& json) {
  // Only parse rbac policy if the channel arg is present
  if (!args.GetBool(GRPC_ARG_PARSE_RBAC_METHOD_CONFIG).value_or(false)) {
    return nullptr;
  }
  std::vector<Rbac> rbac_policies;
  std::vector<grpc_error_handle> error_list;
  const Json::Array* policies_json_array;
  if (ParseJsonObjectField(json.object_value(), "rbacPolicy",
                           &policies_json_array, &error_list)) {
    rbac_policies = ParseRbacArray(*policies_json_array, &error_list);
  }
  grpc_error_handle error =
      GRPC_ERROR_CREATE_FROM_VECTOR("Rbac parser", &error_list);
  if (!GRPC_ERROR_IS_NONE(error)) {
    absl::Status status = absl::InvalidArgumentError(
        absl::StrCat("error parsing RBAC method parameters: ",
                     grpc_error_std_string(error)));
    GRPC_ERROR_UNREF(error);
    return status;
  }
  if (rbac_policies.empty()) return nullptr;
  return absl::make_unique<RbacMethodParsedConfig>(std::move(rbac_policies));
}

void RbacServiceConfigParser::Register(CoreConfiguration::Builder* builder) {
  builder->service_config_parser()->RegisterParser(
      absl::make_unique<RbacServiceConfigParser>());
}

size_t RbacServiceConfigParser::ParserIndex() {
  return CoreConfiguration::Get().service_config_parser().GetParserIndex(
      parser_name());
}

}  // namespace grpc_core
