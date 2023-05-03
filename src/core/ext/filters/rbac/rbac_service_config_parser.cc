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

#include <cstdint>
#include <map>
#include <string>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/types/optional.h"

#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/json/json_args.h"
#include "src/core/lib/json/json_object_loader.h"
#include "src/core/lib/matchers/matchers.h"

namespace grpc_core {

namespace {

// RbacConfig: one or more RbacPolicy structs
struct RbacConfig {
  // RbacPolicy: optional Rules
  struct RbacPolicy {
    // Rules: an action, plus a map of policy names to Policy structs
    struct Rules {
      // Policy: a list of Permissions and a list of Principals
      struct Policy {
        // CidrRange: represents an IP range
        struct CidrRange {
          Rbac::CidrRange cidr_range;

          static const JsonLoaderInterface* JsonLoader(const JsonArgs&);
          void JsonPostLoad(const Json& json, const JsonArgs& args,
                            ValidationErrors* errors);
        };

        // SafeRegexMatch: a regex matcher
        struct SafeRegexMatch {
          std::string regex;

          static const JsonLoaderInterface* JsonLoader(const JsonArgs&);
        };

        // HeaderMatch: a matcher for HTTP headers
        struct HeaderMatch {
          // RangeMatch: matches a range of numerical values
          struct RangeMatch {
            int64_t start;
            int64_t end;

            static const JsonLoaderInterface* JsonLoader(const JsonArgs&);
          };

          HeaderMatcher matcher;

          static const JsonLoaderInterface* JsonLoader(const JsonArgs&);
          void JsonPostLoad(const Json& json, const JsonArgs& args,
                            ValidationErrors* errors);
        };

        // StringMatch: a matcher for strings
        struct StringMatch {
          StringMatcher matcher;

          static const JsonLoaderInterface* JsonLoader(const JsonArgs&);
          void JsonPostLoad(const Json& json, const JsonArgs& args,
                            ValidationErrors* errors);
        };

        // PathMatch: a matcher for paths
        struct PathMatch {
          StringMatch path;

          static const JsonLoaderInterface* JsonLoader(const JsonArgs&);
        };

        // Metadata: a matcher for Envoy metadata (not really applicable
        // to gRPC; we use only the invert field for proper match semantics)
        struct Metadata {
          bool invert = false;

          static const JsonLoaderInterface* JsonLoader(const JsonArgs&);
        };

        // Permission: a matcher for request attributes
        struct Permission {
          // PermissionList: a list used for "and" and "or" matchers
          struct PermissionList {
            std::vector<Permission> rules;

            PermissionList() = default;
            PermissionList(const PermissionList&) = delete;
            PermissionList& operator=(const PermissionList&) = delete;
            PermissionList(PermissionList&&) = default;
            PermissionList& operator=(PermissionList&&) = default;

            static const JsonLoaderInterface* JsonLoader(const JsonArgs&);
          };

          std::unique_ptr<Rbac::Permission> permission;

          Permission() = default;
          Permission(const Permission&) = delete;
          Permission& operator=(const Permission&) = delete;
          Permission(Permission&&) = default;
          Permission& operator=(Permission&&) = default;

          static std::vector<std::unique_ptr<Rbac::Permission>>
          MakeRbacPermissionList(std::vector<Permission> permission_list);
          static const JsonLoaderInterface* JsonLoader(const JsonArgs&);
          void JsonPostLoad(const Json& json, const JsonArgs& args,
                            ValidationErrors* errors);
        };

        // Principal: a matcher for client identity
        struct Principal {
          // PrincipalList: a list used for "and" and "or" matchers
          struct PrincipalList {
            std::vector<Principal> ids;

            PrincipalList() = default;
            PrincipalList(const PrincipalList&) = delete;
            PrincipalList& operator=(const PrincipalList&) = delete;
            PrincipalList(PrincipalList&&) = default;
            PrincipalList& operator=(PrincipalList&&) = default;

            static const JsonLoaderInterface* JsonLoader(const JsonArgs&);
          };

          struct Authenticated {
            absl::optional<StringMatch> principal_name;

            static const JsonLoaderInterface* JsonLoader(const JsonArgs&);
          };

          std::unique_ptr<Rbac::Principal> principal;

          Principal() = default;
          Principal(const Principal&) = delete;
          Principal& operator=(const Principal&) = delete;
          Principal(Principal&&) = default;
          Principal& operator=(Principal&&) = default;

          static std::vector<std::unique_ptr<Rbac::Principal>>
          MakeRbacPrincipalList(std::vector<Principal> principal_list);
          static const JsonLoaderInterface* JsonLoader(const JsonArgs&);
          void JsonPostLoad(const Json& json, const JsonArgs& args,
                            ValidationErrors* errors);
        };

        std::vector<Permission> permissions;
        std::vector<Principal> principals;

        Policy() = default;
        Policy(const Policy&) = delete;
        Policy& operator=(const Policy&) = delete;
        Policy(Policy&&) = default;
        Policy& operator=(Policy&&) = default;

        Rbac::Policy TakeAsRbacPolicy();
        static const JsonLoaderInterface* JsonLoader(const JsonArgs&);
      };

      int action;
      std::map<std::string, Policy> policies;

      Rules() = default;
      Rules(const Rules&) = delete;
      Rules& operator=(const Rules&) = delete;
      Rules(Rules&&) = default;
      Rules& operator=(Rules&&) = default;

      Rbac TakeAsRbac();
      static const JsonLoaderInterface* JsonLoader(const JsonArgs&);
      void JsonPostLoad(const Json&, const JsonArgs&, ValidationErrors* errors);
    };

    std::string name;
    absl::optional<Rules> rules;

    Rbac TakeAsRbac();
    static const JsonLoaderInterface* JsonLoader(const JsonArgs&);
  };

  std::vector<RbacPolicy> rbac_policies;

  std::vector<Rbac> TakeAsRbacList();
  static const JsonLoaderInterface* JsonLoader(const JsonArgs&);
};

//
// RbacConfig::RbacPolicy::Rules::Policy::CidrRange
//

const JsonLoaderInterface*
RbacConfig::RbacPolicy::Rules::Policy::CidrRange::JsonLoader(const JsonArgs&) {
  // All fields handled in JsonPostLoad().
  static const auto* loader = JsonObjectLoader<CidrRange>().Finish();
  return loader;
}

void RbacConfig::RbacPolicy::Rules::Policy::CidrRange::JsonPostLoad(
    const Json& json, const JsonArgs& args, ValidationErrors* errors) {
  auto address_prefix = LoadJsonObjectField<std::string>(
      json.object(), args, "addressPrefix", errors);
  auto prefix_len =
      LoadJsonObjectField<uint32_t>(json.object(), args, "prefixLen", errors,
                                    /*required=*/false);
  cidr_range =
      Rbac::CidrRange(address_prefix.value_or(""), prefix_len.value_or(0));
}

//
// RbacConfig::RbacPolicy::Rules::Policy::SafeRegexMatch
//

const JsonLoaderInterface*
RbacConfig::RbacPolicy::Rules::Policy::SafeRegexMatch::JsonLoader(
    const JsonArgs&) {
  static const auto* loader = JsonObjectLoader<SafeRegexMatch>()
                                  .Field("regex", &SafeRegexMatch::regex)
                                  .Finish();
  return loader;
}

//
// RbacConfig::RbacPolicy::Rules::Policy::HeaderMatch::RangeMatch
//

const JsonLoaderInterface*
RbacConfig::RbacPolicy::Rules::Policy::HeaderMatch::RangeMatch::JsonLoader(
    const JsonArgs&) {
  static const auto* loader = JsonObjectLoader<RangeMatch>()
                                  .Field("start", &RangeMatch::start)
                                  .Field("end", &RangeMatch::end)
                                  .Finish();
  return loader;
}

//
// RbacConfig::RbacPolicy::Rules::Policy::HeaderMatch
//

const JsonLoaderInterface*
RbacConfig::RbacPolicy::Rules::Policy::HeaderMatch::JsonLoader(
    const JsonArgs&) {
  // All fields handled in JsonPostLoad().
  static const auto* loader = JsonObjectLoader<HeaderMatch>().Finish();
  return loader;
}

void RbacConfig::RbacPolicy::Rules::Policy::HeaderMatch::JsonPostLoad(
    const Json& json, const JsonArgs& args, ValidationErrors* errors) {
  const size_t original_error_size = errors->size();
  std::string name =
      LoadJsonObjectField<std::string>(json.object(), args, "name", errors)
          .value_or("");
  bool invert_match =
      LoadJsonObjectField<bool>(json.object(), args, "invertMatch", errors,
                                /*required=*/false)
          .value_or(false);
  auto set_header_matcher = [&](absl::StatusOr<HeaderMatcher> header_matcher) {
    if (header_matcher.ok()) {
      matcher = *header_matcher;
    } else {
      errors->AddError(header_matcher.status().message());
    }
  };
  auto check_match = [&](absl::string_view field_name,
                         HeaderMatcher::Type type) {
    auto match = LoadJsonObjectField<std::string>(json.object(), args,
                                                  field_name, errors,
                                                  /*required=*/false);
    if (match.has_value()) {
      set_header_matcher(
          HeaderMatcher::Create(name, type, *match, 0, 0, false, invert_match));
      return true;
    }
    return false;
  };
  if (check_match("exactMatch", HeaderMatcher::Type::kExact) ||
      check_match("prefixMatch", HeaderMatcher::Type::kPrefix) ||
      check_match("suffixMatch", HeaderMatcher::Type::kSuffix) ||
      check_match("containsMatch", HeaderMatcher::Type::kContains)) {
    return;
  }
  auto present_match =
      LoadJsonObjectField<bool>(json.object(), args, "presentMatch", errors,
                                /*required=*/false);
  if (present_match.has_value()) {
    set_header_matcher(
        HeaderMatcher::Create(name, HeaderMatcher::Type::kPresent, "", 0, 0,
                              *present_match, invert_match));
    return;
  }
  auto regex_match = LoadJsonObjectField<SafeRegexMatch>(
      json.object(), args, "safeRegexMatch", errors,
      /*required=*/false);
  if (regex_match.has_value()) {
    set_header_matcher(
        HeaderMatcher::Create(name, HeaderMatcher::Type::kSafeRegex,
                              regex_match->regex, 0, 0, false, invert_match));
    return;
  }
  auto range_match =
      LoadJsonObjectField<RangeMatch>(json.object(), args, "rangeMatch", errors,
                                      /*required=*/false);
  if (range_match.has_value()) {
    set_header_matcher(HeaderMatcher::Create(name, HeaderMatcher::Type::kRange,
                                             "", range_match->start,
                                             range_match->end, invert_match));
    return;
  }
  if (errors->size() == original_error_size) {
    errors->AddError("no valid matcher found");
  }
}

//
// RbacConfig::RbacPolicy::Rules::Policy::StringMatch
//

const JsonLoaderInterface*
RbacConfig::RbacPolicy::Rules::Policy::StringMatch::JsonLoader(
    const JsonArgs&) {
  // All fields handled in JsonPostLoad().
  static const auto* loader = JsonObjectLoader<StringMatch>().Finish();
  return loader;
}

void RbacConfig::RbacPolicy::Rules::Policy::StringMatch::JsonPostLoad(
    const Json& json, const JsonArgs& args, ValidationErrors* errors) {
  const size_t original_error_size = errors->size();
  bool ignore_case =
      LoadJsonObjectField<bool>(json.object(), args, "ignoreCase", errors,
                                /*required=*/false)
          .value_or(false);
  auto set_string_matcher = [&](absl::StatusOr<StringMatcher> string_matcher) {
    if (string_matcher.ok()) {
      matcher = *string_matcher;
    } else {
      errors->AddError(string_matcher.status().message());
    }
  };
  auto check_match = [&](absl::string_view field_name,
                         StringMatcher::Type type) {
    auto match = LoadJsonObjectField<std::string>(json.object(), args,
                                                  field_name, errors,
                                                  /*required=*/false);
    if (match.has_value()) {
      set_string_matcher(StringMatcher::Create(type, *match, ignore_case));
      return true;
    }
    return false;
  };
  if (check_match("exact", StringMatcher::Type::kExact) ||
      check_match("prefix", StringMatcher::Type::kPrefix) ||
      check_match("suffix", StringMatcher::Type::kSuffix) ||
      check_match("contains", StringMatcher::Type::kContains)) {
    return;
  }
  auto regex_match = LoadJsonObjectField<SafeRegexMatch>(json.object(), args,
                                                         "safeRegex", errors,
                                                         /*required=*/false);
  if (regex_match.has_value()) {
    set_string_matcher(StringMatcher::Create(StringMatcher::Type::kSafeRegex,
                                             regex_match->regex, ignore_case));
    return;
  }
  if (errors->size() == original_error_size) {
    errors->AddError("no valid matcher found");
  }
}

//
// RbacConfig::RbacPolicy::Rules::Policy::PathMatch
//

const JsonLoaderInterface*
RbacConfig::RbacPolicy::Rules::Policy::PathMatch::JsonLoader(const JsonArgs&) {
  static const auto* loader =
      JsonObjectLoader<PathMatch>().Field("path", &PathMatch::path).Finish();
  return loader;
}

//
// RbacConfig::RbacPolicy::Rules::Policy::Metadata
//

const JsonLoaderInterface*
RbacConfig::RbacPolicy::Rules::Policy::Metadata::JsonLoader(const JsonArgs&) {
  static const auto* loader = JsonObjectLoader<Metadata>()
                                  .OptionalField("invert", &Metadata::invert)
                                  .Finish();
  return loader;
}

//
// RbacConfig::RbacPolicy::Rules::Policy::Permission::PermissionList
//

const JsonLoaderInterface*
RbacConfig::RbacPolicy::Rules::Policy::Permission::PermissionList::JsonLoader(
    const JsonArgs&) {
  static const auto* loader = JsonObjectLoader<PermissionList>()
                                  .Field("rules", &PermissionList::rules)
                                  .Finish();
  return loader;
}

//
// RbacConfig::RbacPolicy::Rules::Policy::Permission
//

std::vector<std::unique_ptr<Rbac::Permission>>
RbacConfig::RbacPolicy::Rules::Policy::Permission::MakeRbacPermissionList(
    std::vector<Permission> permission_list) {
  std::vector<std::unique_ptr<Rbac::Permission>> permissions;
  permissions.reserve(permission_list.size());
  for (auto& rule : permission_list) {
    permissions.emplace_back(std::move(rule.permission));
  }
  return permissions;
}

const JsonLoaderInterface*
RbacConfig::RbacPolicy::Rules::Policy::Permission::JsonLoader(const JsonArgs&) {
  // All fields handled in JsonPostLoad().
  static const auto* loader = JsonObjectLoader<Permission>().Finish();
  return loader;
}

void RbacConfig::RbacPolicy::Rules::Policy::Permission::JsonPostLoad(
    const Json& json, const JsonArgs& args, ValidationErrors* errors) {
  const size_t original_error_size = errors->size();
  auto any = LoadJsonObjectField<bool>(json.object(), args, "any", errors,
                                       /*required=*/false);
  if (any.has_value()) {
    permission = std::make_unique<Rbac::Permission>(
        Rbac::Permission::MakeAnyPermission());
    return;
  }
  auto header =
      LoadJsonObjectField<HeaderMatch>(json.object(), args, "header", errors,
                                       /*required=*/false);
  if (header.has_value()) {
    permission = std::make_unique<Rbac::Permission>(
        Rbac::Permission::MakeHeaderPermission(std::move(header->matcher)));
    return;
  }
  auto url_path =
      LoadJsonObjectField<PathMatch>(json.object(), args, "urlPath", errors,
                                     /*required=*/false);
  if (url_path.has_value()) {
    permission = std::make_unique<Rbac::Permission>(
        Rbac::Permission::MakePathPermission(url_path->path.matcher));
    return;
  }
  auto destination_ip = LoadJsonObjectField<CidrRange>(json.object(), args,
                                                       "destinationIp", errors,
                                                       /*required=*/false);
  if (destination_ip.has_value()) {
    permission = std::make_unique<Rbac::Permission>(
        Rbac::Permission::MakeDestIpPermission(
            std::move(destination_ip->cidr_range)));
    return;
  }
  auto destination_port = LoadJsonObjectField<uint32_t>(
      json.object(), args, "destinationPort", errors,
      /*required=*/false);
  if (destination_port.has_value()) {
    permission = std::make_unique<Rbac::Permission>(
        Rbac::Permission::MakeDestPortPermission(*destination_port));
    return;
  }
  auto metadata =
      LoadJsonObjectField<Metadata>(json.object(), args, "metadata", errors,
                                    /*required=*/false);
  if (metadata.has_value()) {
    permission = std::make_unique<Rbac::Permission>(
        Rbac::Permission::MakeMetadataPermission(metadata->invert));
    return;
  }
  auto requested_server_name = LoadJsonObjectField<StringMatch>(
      json.object(), args, "requestedServerName", errors,
      /*required=*/false);
  if (requested_server_name.has_value()) {
    permission = std::make_unique<Rbac::Permission>(
        Rbac::Permission::MakeReqServerNamePermission(
            std::move(requested_server_name->matcher)));
    return;
  }
  auto rules = LoadJsonObjectField<PermissionList>(json.object(), args,
                                                   "andRules", errors,
                                                   /*required=*/false);
  if (rules.has_value()) {
    permission =
        std::make_unique<Rbac::Permission>(Rbac::Permission::MakeAndPermission(
            MakeRbacPermissionList(std::move(rules->rules))));
    return;
  }
  rules = LoadJsonObjectField<PermissionList>(json.object(), args, "orRules",
                                              errors,
                                              /*required=*/false);
  if (rules.has_value()) {
    permission =
        std::make_unique<Rbac::Permission>(Rbac::Permission::MakeOrPermission(
            MakeRbacPermissionList(std::move(rules->rules))));
    return;
  }
  auto not_rule =
      LoadJsonObjectField<Permission>(json.object(), args, "notRule", errors,
                                      /*required=*/false);
  if (not_rule.has_value()) {
    permission = std::make_unique<Rbac::Permission>(
        Rbac::Permission::MakeNotPermission(std::move(*not_rule->permission)));
    return;
  }
  if (errors->size() == original_error_size) {
    errors->AddError("no valid rule found");
  }
}

//
// RbacConfig::RbacPolicy::Rules::Policy::Principal::PrincipalList
//

const JsonLoaderInterface*
RbacConfig::RbacPolicy::Rules::Policy::Principal::PrincipalList::JsonLoader(
    const JsonArgs&) {
  static const auto* loader = JsonObjectLoader<PrincipalList>()
                                  .Field("ids", &PrincipalList::ids)
                                  .Finish();
  return loader;
}

//
// RbacConfig::RbacPolicy::Rules::Policy::Principal::Authenticated
//

const JsonLoaderInterface*
RbacConfig::RbacPolicy::Rules::Policy::Principal::Authenticated::JsonLoader(
    const JsonArgs&) {
  static const auto* loader =
      JsonObjectLoader<Authenticated>()
          .OptionalField("principalName", &Authenticated::principal_name)
          .Finish();
  return loader;
}

//
// RbacConfig::RbacPolicy::Rules::Policy::Principal
//

std::vector<std::unique_ptr<Rbac::Principal>>
RbacConfig::RbacPolicy::Rules::Policy::Principal::MakeRbacPrincipalList(
    std::vector<Principal> principal_list) {
  std::vector<std::unique_ptr<Rbac::Principal>> principals;
  principals.reserve(principal_list.size());
  for (auto& id : principal_list) {
    principals.emplace_back(std::move(id.principal));
  }
  return principals;
}

const JsonLoaderInterface*
RbacConfig::RbacPolicy::Rules::Policy::Principal::JsonLoader(const JsonArgs&) {
  // All fields handled in JsonPostLoad().
  static const auto* loader = JsonObjectLoader<Principal>().Finish();
  return loader;
}

void RbacConfig::RbacPolicy::Rules::Policy::Principal::JsonPostLoad(
    const Json& json, const JsonArgs& args, ValidationErrors* errors) {
  const size_t original_error_size = errors->size();
  auto any = LoadJsonObjectField<bool>(json.object(), args, "any", errors,
                                       /*required=*/false);
  if (any.has_value()) {
    principal =
        std::make_unique<Rbac::Principal>(Rbac::Principal::MakeAnyPrincipal());
    return;
  }
  auto authenticated = LoadJsonObjectField<Authenticated>(
      json.object(), args, "authenticated", errors,
      /*required=*/false);
  if (authenticated.has_value()) {
    if (authenticated->principal_name.has_value()) {
      principal = std::make_unique<Rbac::Principal>(
          Rbac::Principal::MakeAuthenticatedPrincipal(
              std::move(authenticated->principal_name->matcher)));
    } else {
      // No principalName found. Match for all users.
      principal = std::make_unique<Rbac::Principal>(
          Rbac::Principal::MakeAnyPrincipal());
    }
    return;
  }
  auto cidr_range =
      LoadJsonObjectField<CidrRange>(json.object(), args, "sourceIp", errors,
                                     /*required=*/false);
  if (cidr_range.has_value()) {
    principal = std::make_unique<Rbac::Principal>(
        Rbac::Principal::MakeSourceIpPrincipal(
            std::move(cidr_range->cidr_range)));
    return;
  }
  cidr_range = LoadJsonObjectField<CidrRange>(json.object(), args,
                                              "directRemoteIp", errors,
                                              /*required=*/false);
  if (cidr_range.has_value()) {
    principal = std::make_unique<Rbac::Principal>(
        Rbac::Principal::MakeDirectRemoteIpPrincipal(
            std::move(cidr_range->cidr_range)));
    return;
  }
  cidr_range =
      LoadJsonObjectField<CidrRange>(json.object(), args, "remoteIp", errors,
                                     /*required=*/false);
  if (cidr_range.has_value()) {
    principal = std::make_unique<Rbac::Principal>(
        Rbac::Principal::MakeRemoteIpPrincipal(
            std::move(cidr_range->cidr_range)));
    return;
  }
  auto header =
      LoadJsonObjectField<HeaderMatch>(json.object(), args, "header", errors,
                                       /*required=*/false);
  if (header.has_value()) {
    principal = std::make_unique<Rbac::Principal>(
        Rbac::Principal::MakeHeaderPrincipal(std::move(header->matcher)));
    return;
  }
  auto url_path =
      LoadJsonObjectField<PathMatch>(json.object(), args, "urlPath", errors,
                                     /*required=*/false);
  if (url_path.has_value()) {
    principal = std::make_unique<Rbac::Principal>(
        Rbac::Principal::MakePathPrincipal(std::move(url_path->path.matcher)));
    return;
  }
  auto metadata =
      LoadJsonObjectField<Metadata>(json.object(), args, "metadata", errors,
                                    /*required=*/false);
  if (metadata.has_value()) {
    principal = std::make_unique<Rbac::Principal>(
        Rbac::Principal::MakeMetadataPrincipal(metadata->invert));
    return;
  }
  auto ids =
      LoadJsonObjectField<PrincipalList>(json.object(), args, "andIds", errors,
                                         /*required=*/false);
  if (ids.has_value()) {
    principal =
        std::make_unique<Rbac::Principal>(Rbac::Principal::MakeAndPrincipal(
            MakeRbacPrincipalList(std::move(ids->ids))));
    return;
  }
  ids = LoadJsonObjectField<PrincipalList>(json.object(), args, "orIds", errors,
                                           /*required=*/false);
  if (ids.has_value()) {
    principal =
        std::make_unique<Rbac::Principal>(Rbac::Principal::MakeOrPrincipal(
            MakeRbacPrincipalList(std::move(ids->ids))));
    return;
  }
  auto not_rule =
      LoadJsonObjectField<Principal>(json.object(), args, "notId", errors,
                                     /*required=*/false);
  if (not_rule.has_value()) {
    principal = std::make_unique<Rbac::Principal>(
        Rbac::Principal::MakeNotPrincipal(std::move(*not_rule->principal)));
    return;
  }
  if (errors->size() == original_error_size) {
    errors->AddError("no valid id found");
  }
}

//
// RbacConfig::RbacPolicy::Rules::Policy
//

Rbac::Policy RbacConfig::RbacPolicy::Rules::Policy::TakeAsRbacPolicy() {
  Rbac::Policy policy;
  policy.permissions = Rbac::Permission::MakeOrPermission(
      Permission::MakeRbacPermissionList(std::move(permissions)));
  policy.principals = Rbac::Principal::MakeOrPrincipal(
      Principal::MakeRbacPrincipalList(std::move(principals)));
  return policy;
}

const JsonLoaderInterface* RbacConfig::RbacPolicy::Rules::Policy::JsonLoader(
    const JsonArgs&) {
  static const auto* loader = JsonObjectLoader<Policy>()
                                  .Field("permissions", &Policy::permissions)
                                  .Field("principals", &Policy::principals)
                                  .Finish();
  return loader;
}

//
// RbacConfig::RbacPolicy::Rules
//

Rbac RbacConfig::RbacPolicy::Rules::TakeAsRbac() {
  Rbac rbac;
  rbac.action = static_cast<Rbac::Action>(action);
  for (auto& p : policies) {
    rbac.policies.emplace(p.first, p.second.TakeAsRbacPolicy());
  }
  return rbac;
}

const JsonLoaderInterface* RbacConfig::RbacPolicy::Rules::JsonLoader(
    const JsonArgs&) {
  static const auto* loader = JsonObjectLoader<Rules>()
                                  .Field("action", &Rules::action)
                                  .OptionalField("policies", &Rules::policies)
                                  .Finish();
  return loader;
}

void RbacConfig::RbacPolicy::Rules::JsonPostLoad(const Json&, const JsonArgs&,
                                                 ValidationErrors* errors) {
  // Validate action field.
  auto rbac_action = static_cast<Rbac::Action>(action);
  if (rbac_action != Rbac::Action::kAllow &&
      rbac_action != Rbac::Action::kDeny) {
    ValidationErrors::ScopedField field(errors, ".action");
    errors->AddError("unknown action");
  }
}

//
// RbacConfig::RbacPolicy
//

Rbac RbacConfig::RbacPolicy::TakeAsRbac() {
  if (!rules.has_value()) {
    // No enforcing to be applied. An empty deny policy with an empty map
    // is equivalent to no enforcing.
    return Rbac(std::move(name), Rbac::Action::kDeny, {});
  }
  // TODO(lwge): This also needs to take the name.
  return rules->TakeAsRbac();
}

const JsonLoaderInterface* RbacConfig::RbacPolicy::JsonLoader(const JsonArgs&) {
  static const auto* loader = JsonObjectLoader<RbacPolicy>()
                                  .OptionalField("rules", &RbacPolicy::rules)
                                  .Field("filter_name", &RbacPolicy::name)
                                  .Finish();
  return loader;
}

//
// RbacConfig
//

std::vector<Rbac> RbacConfig::TakeAsRbacList() {
  std::vector<Rbac> rbac_list;
  rbac_list.reserve(rbac_policies.size());
  for (auto& rbac_policy : rbac_policies) {
    rbac_list.emplace_back(rbac_policy.TakeAsRbac());
  }
  return rbac_list;
}

const JsonLoaderInterface* RbacConfig::JsonLoader(const JsonArgs&) {
  static const auto* loader =
      JsonObjectLoader<RbacConfig>()
          .Field("rbacPolicy", &RbacConfig::rbac_policies)
          .Finish();
  return loader;
}

}  // namespace

std::unique_ptr<ServiceConfigParser::ParsedConfig>
RbacServiceConfigParser::ParsePerMethodParams(const ChannelArgs& args,
                                              const Json& json,
                                              ValidationErrors* errors) {
  // Only parse rbac policy if the channel arg is present
  if (!args.GetBool(GRPC_ARG_PARSE_RBAC_METHOD_CONFIG).value_or(false)) {
    return nullptr;
  }
  auto rbac_config = LoadFromJson<RbacConfig>(json, JsonArgs(), errors);
  std::vector<Rbac> rbac_policies = rbac_config.TakeAsRbacList();
  if (rbac_policies.empty()) return nullptr;
  return std::make_unique<RbacMethodParsedConfig>(std::move(rbac_policies));
}

void RbacServiceConfigParser::Register(CoreConfiguration::Builder* builder) {
  builder->service_config_parser()->RegisterParser(
      std::make_unique<RbacServiceConfigParser>());
}

size_t RbacServiceConfigParser::ParserIndex() {
  return CoreConfiguration::Get().service_config_parser().GetParserIndex(
      parser_name());
}

}  // namespace grpc_core
