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
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/types/optional.h"

#include <grpc/support/log.h>

#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/json/json_args.h"
#include "src/core/lib/json/json_object_loader.h"
#include "src/core/lib/matchers/matchers.h"

namespace grpc_core {

namespace {

// TODO(roth): When we have time, consider moving this parsing code into
// the Rbac class itself, so that we don't have to do so many copies.
struct RbacConfig {
  struct RbacPolicy {
    struct Rules {
      struct Policy {
        struct CidrRange {
          std::string address_prefix;
          uint32_t prefix_len = 0;

          static const JsonLoaderInterface* JsonLoader(const JsonArgs&) {
            static const auto* loader =
                JsonObjectLoader<CidrRange>()
                    .Field("addressPrefix", &CidrRange::address_prefix)
                    .OptionalField("prefixLen", &CidrRange::prefix_len)
                    .Finish();
            return loader;
          }

          Rbac::CidrRange MakeCidrRange() const {
            return Rbac::CidrRange(address_prefix, prefix_len);
          }
        };

        struct SafeRegexMatch {
          std::string regex;

          static const JsonLoaderInterface* JsonLoader(const JsonArgs&) {
            static const auto* loader =
                JsonObjectLoader<SafeRegexMatch>()
                    .Field("regex", &SafeRegexMatch::regex)
                    .Finish();
            return loader;
          }
        };

        struct HeaderMatch {
          struct RangeMatch {
            int64_t start;
            int64_t end;

            static const JsonLoaderInterface* JsonLoader(const JsonArgs&) {
              static const auto* loader =
                  JsonObjectLoader<RangeMatch>()
                      .Field("start", &RangeMatch::start)
                      .Field("end", &RangeMatch::end)
                      .Finish();
              return loader;
            }
          };

          HeaderMatcher matcher;

          static const JsonLoaderInterface* JsonLoader(const JsonArgs&) {
            // All fields handled in JsonPostLoad().
            static const auto* loader =
                JsonObjectLoader<HeaderMatch>().Finish();
            return loader;
          }

          void JsonPostLoad(const Json& json, const JsonArgs& args,
                            ValidationErrors* errors) {
            const size_t original_error_size = errors->size();
            std::string name = LoadJsonObjectField<std::string>(
                                   json.object_value(), args, "name", errors)
                                   .value_or("");
            bool invert_match =
                LoadJsonObjectField<bool>(json.object_value(), args,
                                          "invertMatch", errors,
                                          /*required=*/false)
                    .value_or(false);
            auto set_header_matcher =
                [&](absl::StatusOr<HeaderMatcher> header_matcher) {
                  if (header_matcher.ok()) {
                    matcher = *header_matcher;
                  } else {
                    errors->AddError(header_matcher.status().message());
                  }
                };
            auto check_match = [&](absl::string_view field_name,
                                   HeaderMatcher::Type type) {
              auto match = LoadJsonObjectField<std::string>(
                  json.object_value(), args, field_name, errors,
                  /*required=*/false);
              if (match.has_value()) {
                set_header_matcher(HeaderMatcher::Create(
                    name, type, *match, 0, 0, false, invert_match));
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
            auto present_match = LoadJsonObjectField<bool>(
                json.object_value(), args, "presentMatch", errors,
                /*required=*/false);
            if (present_match.has_value()) {
              set_header_matcher(
                  HeaderMatcher::Create(name, HeaderMatcher::Type::kPresent, "",
                                        0, 0, *present_match, invert_match));
              return;
            }
            auto regex_match = LoadJsonObjectField<SafeRegexMatch>(
                json.object_value(), args, "safeRegexMatch", errors,
                /*required=*/false);
            if (regex_match.has_value()) {
              set_header_matcher(HeaderMatcher::Create(
                  name, HeaderMatcher::Type::kSafeRegex, regex_match->regex, 0,
                  0, false, invert_match));
              return;
            }
            auto range_match = LoadJsonObjectField<RangeMatch>(
                json.object_value(), args, "rangeMatch", errors,
                /*required=*/false);
            if (range_match.has_value()) {
              set_header_matcher(HeaderMatcher::Create(
                  name, HeaderMatcher::Type::kRange, "", range_match->start,
                  range_match->end, invert_match));
              return;
            }
            if (errors->size() == original_error_size) {
              errors->AddError("no valid matcher found");
            }
          }
        };

        struct StringMatch {
          StringMatcher matcher;

          static const JsonLoaderInterface* JsonLoader(const JsonArgs&) {
            // All fields handled in JsonPostLoad().
            static const auto* loader =
                JsonObjectLoader<StringMatch>().Finish();
            return loader;
          }

          void JsonPostLoad(const Json& json, const JsonArgs& args,
                            ValidationErrors* errors) {
            const size_t original_error_size = errors->size();
            bool ignore_case =
                LoadJsonObjectField<bool>(json.object_value(), args,
                                          "ignoreCase", errors,
                                          /*required=*/false)
                    .value_or(false);
            auto set_string_matcher =
                [&](absl::StatusOr<StringMatcher> string_matcher) {
                  if (string_matcher.ok()) {
                    matcher = *string_matcher;
                  } else {
                    errors->AddError(string_matcher.status().message());
                  }
                };
            auto check_match = [&](absl::string_view field_name,
                                   StringMatcher::Type type) {
              auto match = LoadJsonObjectField<std::string>(
                  json.object_value(), args, field_name, errors,
                  /*required=*/false);
              if (match.has_value()) {
                set_string_matcher(
                    StringMatcher::Create(type, *match, ignore_case));
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
            auto regex_match = LoadJsonObjectField<SafeRegexMatch>(
                json.object_value(), args, "safeRegex", errors,
                /*required=*/false);
            if (regex_match.has_value()) {
              set_string_matcher(
                  StringMatcher::Create(StringMatcher::Type::kSafeRegex,
                                        regex_match->regex, ignore_case));
              return;
            }
            if (errors->size() == original_error_size) {
              errors->AddError("no valid matcher found");
            }
          }
        };

        struct PathMatch {
          StringMatch path;

          static const JsonLoaderInterface* JsonLoader(const JsonArgs&) {
            static const auto* loader = JsonObjectLoader<PathMatch>()
                                            .Field("path", &PathMatch::path)
                                            .Finish();
            return loader;
          }
        };

        struct Metadata {
          bool invert = false;

          static const JsonLoaderInterface* JsonLoader(const JsonArgs&) {
            static const auto* loader =
                JsonObjectLoader<Metadata>()
                    .OptionalField("invert", &Metadata::invert)
                    .Finish();
            return loader;
          }
        };

        struct Permission {
          struct PermissionList {
            std::vector<Permission> rules;

            static const JsonLoaderInterface* JsonLoader(const JsonArgs&) {
              static const auto* loader =
                  JsonObjectLoader<PermissionList>()
                      .Field("rules", &PermissionList::rules)
                      .Finish();
              return loader;
            }

            std::vector<std::unique_ptr<Rbac::Permission>> MakePermissionList()
                const {
              std::vector<std::unique_ptr<Rbac::Permission>> permissions;
              for (const auto& rule : rules) {
                permissions.push_back(
                    absl::make_unique<Rbac::Permission>(rule.MakePermission()));
              }
              return permissions;
            }
          };

          absl::optional<bool> any;
          absl::optional<HeaderMatch> header;
          absl::optional<PathMatch> url_path;
          absl::optional<CidrRange> destination_ip;
          absl::optional<uint32_t> destination_port;
          absl::optional<Metadata> metadata;
          absl::optional<StringMatch> requested_server_name;
          absl::optional<PermissionList> and_rules;
          absl::optional<PermissionList> or_rules;
          std::unique_ptr<Permission> not_rule;

          Permission() = default;
          Permission(const Permission& other)
              : any(other.any),
                header(other.header),
                url_path(other.url_path),
                destination_ip(other.destination_ip),
                destination_port(other.destination_port),
                metadata(other.metadata),
                requested_server_name(other.requested_server_name),
                and_rules(other.and_rules),
                or_rules(other.or_rules),
                not_rule(other.not_rule == nullptr
                             ? nullptr
                             : absl::make_unique<Permission>(*other.not_rule)) {
          }

          static const JsonLoaderInterface* JsonLoader(const JsonArgs&) {
            static const auto* loader =
                JsonObjectLoader<Permission>()
                    .OptionalField("any", &Permission::any)
                    .OptionalField("header", &Permission::header)
                    .OptionalField("urlPath", &Permission::url_path)
                    .OptionalField("destinationIp", &Permission::destination_ip)
                    .OptionalField("destinationPort",
                                   &Permission::destination_port)
                    .OptionalField("metadata", &Permission::metadata)
                    .OptionalField("requestedServerName",
                                   &Permission::requested_server_name)
                    .OptionalField("andRules", &Permission::and_rules)
                    .OptionalField("orRules", &Permission::or_rules)
                    .OptionalField("notRule", &Permission::not_rule)
                    .Finish();
            return loader;
          }

          void JsonPostLoad(const Json&, const JsonArgs&,
                            ValidationErrors* errors) {
            size_t num_matchers =
                any.has_value() + header.has_value() + url_path.has_value() +
                destination_ip.has_value() + destination_port.has_value() +
                metadata.has_value() + requested_server_name.has_value() +
                and_rules.has_value() + or_rules.has_value() +
                (not_rule != nullptr);
            if (num_matchers != 1) {
              errors->AddError(
                  absl::StrCat("expected exactly one permission type, found ",
                               num_matchers));
            }
          }

          Rbac::Permission MakePermission() const {
            if (any.has_value()) {
              return Rbac::Permission::MakeAnyPermission();
            }
            if (header.has_value()) {
              return Rbac::Permission::MakeHeaderPermission(header->matcher);
            }
            if (url_path.has_value()) {
              return Rbac::Permission::MakePathPermission(
                  url_path->path.matcher);
            }
            if (destination_ip.has_value()) {
              return Rbac::Permission::MakeDestIpPermission(
                  destination_ip->MakeCidrRange());
            }
            if (destination_port.has_value()) {
              return Rbac::Permission::MakeDestPortPermission(
                  *destination_port);
            }
            if (metadata.has_value()) {
              return Rbac::Permission::MakeMetadataPermission(metadata->invert);
            }
            if (requested_server_name.has_value()) {
              return Rbac::Permission::MakeReqServerNamePermission(
                  requested_server_name->matcher);
            }
            if (and_rules.has_value()) {
              return Rbac::Permission::MakeAndPermission(
                  and_rules->MakePermissionList());
            }
            if (or_rules.has_value()) {
              return Rbac::Permission::MakeOrPermission(
                  or_rules->MakePermissionList());
            }
            GPR_ASSERT(not_rule != nullptr);
            return Rbac::Permission::MakeNotPermission(
                not_rule->MakePermission());
          }
        };

        struct Principal {
          struct PrincipalList {
            std::vector<Principal> ids;

            static const JsonLoaderInterface* JsonLoader(const JsonArgs&) {
              static const auto* loader = JsonObjectLoader<PrincipalList>()
                                              .Field("ids", &PrincipalList::ids)
                                              .Finish();
              return loader;
            }

            std::vector<std::unique_ptr<Rbac::Principal>> MakePrincipalList()
                const {
              std::vector<std::unique_ptr<Rbac::Principal>> principals;
              for (const auto& id : ids) {
                principals.push_back(
                    absl::make_unique<Rbac::Principal>(id.MakePrincipal()));
              }
              return principals;
            }
          };

          struct Authenticated {
            absl::optional<StringMatch> principal_name;

            static const JsonLoaderInterface* JsonLoader(const JsonArgs&) {
              static const auto* loader =
                  JsonObjectLoader<Authenticated>()
                      .OptionalField("principalName",
                                     &Authenticated::principal_name)
                      .Finish();
              return loader;
            }

            Rbac::Principal MakePrincipal() const {
              if (principal_name.has_value()) {
                return Rbac::Principal::MakeAuthenticatedPrincipal(
                    principal_name->matcher);
              }
              // No principalName found. Match for all users.
              return Rbac::Principal::MakeAnyPrincipal();
            }
          };

          absl::optional<bool> any;
          absl::optional<Authenticated> authenticated;
          absl::optional<CidrRange> source_ip;
          absl::optional<CidrRange> direct_remote_ip;
          absl::optional<CidrRange> remote_ip;
          absl::optional<HeaderMatch> header;
          absl::optional<PathMatch> url_path;
          absl::optional<Metadata> metadata;
          absl::optional<PrincipalList> and_ids;
          absl::optional<PrincipalList> or_ids;
          std::unique_ptr<Principal> not_id;

          Principal() = default;
          Principal(const Principal& other)
              : any(other.any),
                authenticated(other.authenticated),
                source_ip(other.source_ip),
                direct_remote_ip(other.direct_remote_ip),
                remote_ip(other.remote_ip),
                header(other.header),
                url_path(other.url_path),
                metadata(other.metadata),
                and_ids(other.and_ids),
                or_ids(other.or_ids),
                not_id(other.not_id == nullptr
                           ? nullptr
                           : absl::make_unique<Principal>(*other.not_id)) {}

          static const JsonLoaderInterface* JsonLoader(const JsonArgs&) {
            static const auto* loader =
                JsonObjectLoader<Principal>()
                    .OptionalField("any", &Principal::any)
                    .OptionalField("authenticated", &Principal::authenticated)
                    .OptionalField("sourceIp", &Principal::source_ip)
                    .OptionalField("directRemoteIp",
                                   &Principal::direct_remote_ip)
                    .OptionalField("remoteIp", &Principal::remote_ip)
                    .OptionalField("header", &Principal::header)
                    .OptionalField("urlPath", &Principal::url_path)
                    .OptionalField("metadata", &Principal::metadata)
                    .OptionalField("andIds", &Principal::and_ids)
                    .OptionalField("orIds", &Principal::or_ids)
                    .OptionalField("notId", &Principal::not_id)
                    .Finish();
            return loader;
          }

          void JsonPostLoad(const Json&, const JsonArgs&,
                            ValidationErrors* errors) {
            size_t num_matchers =
                any.has_value() + authenticated.has_value() +
                source_ip.has_value() + direct_remote_ip.has_value() +
                remote_ip.has_value() + header.has_value() +
                url_path.has_value() + metadata.has_value() +
                and_ids.has_value() + or_ids.has_value() + (not_id != nullptr);
            if (num_matchers != 1) {
              errors->AddError(absl::StrCat(
                  "expected exactly one principal type, found ", num_matchers));
            }
          }

          Rbac::Principal MakePrincipal() const {
            if (any.has_value()) {
              return Rbac::Principal::MakeAnyPrincipal();
            }
            if (authenticated.has_value()) {
              return authenticated->MakePrincipal();
            }
            if (source_ip.has_value()) {
              return Rbac::Principal::MakeSourceIpPrincipal(
                  source_ip->MakeCidrRange());
            }
            if (direct_remote_ip.has_value()) {
              return Rbac::Principal::MakeDirectRemoteIpPrincipal(
                  direct_remote_ip->MakeCidrRange());
            }
            if (remote_ip.has_value()) {
              return Rbac::Principal::MakeRemoteIpPrincipal(
                  remote_ip->MakeCidrRange());
            }
            if (header.has_value()) {
              return Rbac::Principal::MakeHeaderPrincipal(header->matcher);
            }
            if (url_path.has_value()) {
              return Rbac::Principal::MakePathPrincipal(url_path->path.matcher);
            }
            if (metadata.has_value()) {
              return Rbac::Principal::MakeMetadataPrincipal(metadata->invert);
            }
            if (and_ids.has_value()) {
              return Rbac::Principal::MakeAndPrincipal(
                  and_ids->MakePrincipalList());
            }
            if (or_ids.has_value()) {
              return Rbac::Principal::MakeOrPrincipal(
                  or_ids->MakePrincipalList());
            }
            GPR_ASSERT(not_id != nullptr);
            return Rbac::Principal::MakeNotPrincipal(not_id->MakePrincipal());
          }
        };

        std::vector<Permission> permissions;
        std::vector<Principal> principals;

        static const JsonLoaderInterface* JsonLoader(const JsonArgs&) {
          static const auto* loader =
              JsonObjectLoader<Policy>()
                  .Field("permissions", &Policy::permissions)
                  .Field("principals", &Policy::principals)
                  .Finish();
          return loader;
        }

        Rbac::Policy MakePolicy() const {
          std::vector<std::unique_ptr<Rbac::Permission>> rbac_permissions;
          for (const auto& permission : permissions) {
            rbac_permissions.emplace_back(absl::make_unique<Rbac::Permission>(
                permission.MakePermission()));
          }
          std::vector<std::unique_ptr<Rbac::Principal>> rbac_principals;
          for (const auto& principal : principals) {
            rbac_principals.emplace_back(
                absl::make_unique<Rbac::Principal>(principal.MakePrincipal()));
          }
          Rbac::Policy policy;
          policy.permissions =
              Rbac::Permission::MakeOrPermission(std::move(rbac_permissions));
          policy.principals =
              Rbac::Principal::MakeOrPrincipal(std::move(rbac_principals));
          return policy;
        }
      };

      int action;
      std::map<std::string, Policy> policies;

      static const JsonLoaderInterface* JsonLoader(const JsonArgs&) {
        static const auto* loader =
            JsonObjectLoader<Rules>()
                .Field("action", &Rules::action)
                .OptionalField("policies", &Rules::policies)
                .Finish();
        return loader;
      }

      void JsonPostLoad(const Json&, const JsonArgs&,
                        ValidationErrors* errors) {
        // Validate action field.
        auto rbac_action = static_cast<Rbac::Action>(action);
        if (rbac_action != Rbac::Action::kAllow &&
            rbac_action != Rbac::Action::kDeny) {
          ValidationErrors::ScopedField field(errors, ".action");
          errors->AddError("unknown action");
        }
      }

      Rbac MakeRbac() const {
        Rbac rbac;
        rbac.action = static_cast<Rbac::Action>(action);
        for (const auto& p : policies) {
          rbac.policies.emplace(p.first, p.second.MakePolicy());
        }
        return rbac;
      }
    };

    absl::optional<Rules> rules;

    static const JsonLoaderInterface* JsonLoader(const JsonArgs&) {
      static const auto* loader =
          JsonObjectLoader<RbacPolicy>()
              .OptionalField("rules", &RbacPolicy::rules)
              .Finish();
      return loader;
    }

    Rbac MakeRbac() const {
      if (!rules.has_value()) {
        // No enforcing to be applied. An empty deny policy with an empty map
        // is equivalent to no enforcing.
        return Rbac(Rbac::Action::kDeny, {});
      }
      return rules->MakeRbac();
    }
  };

  std::vector<RbacPolicy> rbac_policies;

  static const JsonLoaderInterface* JsonLoader(const JsonArgs&) {
    static const auto* loader =
        JsonObjectLoader<RbacConfig>()
            .Field("rbacPolicy", &RbacConfig::rbac_policies)
            .Finish();
    return loader;
  }

  std::vector<Rbac> MakeRbacList() const {
    std::vector<Rbac> rbac_list;
    for (const auto& rbac_policy : rbac_policies) {
      rbac_list.emplace_back(rbac_policy.MakeRbac());
    }
    return rbac_list;
  }
};

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
  std::vector<Rbac> rbac_policies = rbac_config.MakeRbacList();
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
