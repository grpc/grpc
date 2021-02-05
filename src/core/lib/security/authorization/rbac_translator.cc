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

#include <grpc/support/port_platform.h>

#include "src/core/lib/security/authorization/rbac_translator.h"

#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/strings/strip.h"

#include "src/core/lib/security/authorization/matchers.h"

namespace grpc_core {

namespace {

std::string GetMatcherType(const std::string& value,
                           StringMatcher::Type* type) {
  if (value == "*") {
    *type = StringMatcher::Type::SAFE_REGEX;
    return ".*";
  } else if (absl::StartsWith(value, "*")) {
    *type = StringMatcher::Type::SUFFIX;
    return std::string(absl::StripPrefix(value, "*"));
  } else if (absl::EndsWith(value, "*")) {
    *type = StringMatcher::Type::PREFIX;
    return std::string(absl::StripSuffix(value, "*"));
  }
  *type = StringMatcher::Type::EXACT;
  return value;
}

absl::StatusOr<StringMatcher> GetStringMatcher(const std::string value) {
  StringMatcher::Type type;
  std::string matcher = GetMatcherType(value, &type);
  return StringMatcher::Create(type, matcher);
}

absl::StatusOr<HeaderMatcher> GetHeaderMatcher(const std::string name,
                                               const std::string value) {
  StringMatcher::Type type;
  std::string matcher = GetMatcherType(value, &type);
  return HeaderMatcher::Create(name, static_cast<HeaderMatcher::Type>(type),
                               matcher);
}

absl::Status ParsePrincipalsArray(Json* json, Rbac::Principal* principals) {
  std::unique_ptr<Rbac::Principal> sub_principal =
      absl::make_unique<Rbac::Principal>();
  sub_principal->type = Rbac::Principal::RuleType::OR;
  for (size_t i = 0; i < json->mutable_array()->size(); ++i) {
    Json& child = json->mutable_array()->at(i);
    if (child.type() != Json::Type::STRING) {
      return absl::InvalidArgumentError(
          absl::StrCat("\"principals\" ", i, " is not a string."));
    }
    auto matcher_or = GetStringMatcher(child.string_value());
    if (!matcher_or.ok()) {
      return matcher_or.status();
    }
    sub_principal->principals.push_back(absl::make_unique<Rbac::Principal>());
    sub_principal->principals.back()->type =
        Rbac::Principal::RuleType::PRINCIPAL_NAME;
    sub_principal->principals.back()->string_matcher =
        std::move(matcher_or.value());
  }
  if (!sub_principal->principals.empty()) {
    principals->principals.push_back(std::move(sub_principal));
  }
  return absl::OkStatus();
}

absl::Status ParsePeer(Json* json, Rbac::Policy* policy) {
  Rbac::Principal principals;
  principals.type = Rbac::Principal::RuleType::AND;
  auto it = json->mutable_object()->find("principals");
  if (it != json->mutable_object()->end()) {
    if (it->second.type() != Json::Type::ARRAY) {
      return absl::InvalidArgumentError("\"principals\" is not an array.");
    }
    absl::Status parse_status = ParsePrincipalsArray(&it->second, &principals);
    if (!parse_status.ok()) return parse_status;
  }
  if (!principals.principals.empty()) {
    policy->principals = std::move(principals);
  } else {
    policy->principals.type = Rbac::Principal::RuleType::ANY;
  }
  return absl::OkStatus();
}

absl::StatusOr<std::unique_ptr<Rbac::Permission>> ParseHeaderValues(
    Json* json, const std::string& header_name) {
  if (json->mutable_array()->empty()) {
    return absl::InvalidArgumentError("header \"values\" list is empty.");
  }
  std::unique_ptr<Rbac::Permission> values =
      absl::make_unique<Rbac::Permission>();
  values->type = Rbac::Permission::RuleType::OR;
  for (size_t i = 0; i < json->mutable_array()->size(); ++i) {
    Json& child = json->mutable_array()->at(i);
    if (child.type() != Json::Type::STRING) {
      return absl::InvalidArgumentError(
          absl::StrCat("header \"values\" ", i, " is not a string."));
    }
    auto header_matcher_or =
        GetHeaderMatcher(header_name, child.string_value());
    if (!header_matcher_or.ok()) {
      return header_matcher_or.status();
    }
    values->permissions.push_back(absl::make_unique<Rbac::Permission>());
    values->permissions.back()->type = Rbac::Permission::RuleType::HEADER;
    values->permissions.back()->header_matcher =
        std::move(header_matcher_or.value());
  }
  return std::move(values);
}

absl::StatusOr<std::unique_ptr<Rbac::Permission>> ParseHeaders(Json* json) {
  std::string header_name;
  auto it = json->mutable_object()->find("key");
  if (it == json->mutable_object()->end()) {
    return absl::InvalidArgumentError("\"key\" field is not present.");
  }
  if (it->second.type() != Json::Type::STRING) {
    return absl::InvalidArgumentError("\"key\" is not a string.");
  }
  header_name = it->second.string_value();
  // TODO(ashithasantosh): Add connection headers below.
  if (absl::StartsWith(header_name, ":") ||
      absl::StartsWith(header_name, "grpc-") || header_name == "host") {
    return absl::InvalidArgumentError(
        absl::StrFormat("Unsupported header \"key\" %s.", header_name));
  }

  it = json->mutable_object()->find("values");
  if (it == json->mutable_object()->end()) {
    return absl::InvalidArgumentError(
        "header \"values\" field is not present.");
  }
  if (it->second.type() != Json::Type::ARRAY) {
    return absl::InvalidArgumentError("header \"values\" is not an array.");
  }
  return ParseHeaderValues(&it->second, header_name);
}

absl::Status ParseHeadersArray(Json* json, Rbac::Permission* permissions) {
  std::unique_ptr<Rbac::Permission> headers =
      absl::make_unique<Rbac::Permission>();
  headers->type = Rbac::Permission::RuleType::AND;
  for (size_t i = 0; i < json->mutable_array()->size(); ++i) {
    Json& child = json->mutable_array()->at(i);
    if (child.type() != Json::Type::OBJECT) {
      return absl::InvalidArgumentError(
          absl::StrCat("\"headers\" ", i, " is not an object."));
    }
    auto headers_or = ParseHeaders(&child);
    if (!headers_or.ok()) return headers_or.status();
    headers->permissions.push_back(std::move(headers_or.value()));
  }
  if (!headers->permissions.empty()) {
    permissions->permissions.push_back(std::move(headers));
  }
  return absl::OkStatus();
}

absl::Status ParsePathsArray(Json* json, Rbac::Permission* permissions) {
  std::unique_ptr<Rbac::Permission> paths =
      absl::make_unique<Rbac::Permission>();
  paths->type = Rbac::Permission::RuleType::OR;
  for (size_t i = 0; i < json->mutable_array()->size(); ++i) {
    Json& child = json->mutable_array()->at(i);
    if (child.type() != Json::Type::STRING) {
      return absl::InvalidArgumentError(
          absl::StrCat("\"paths\" ", i, " is not a string."));
    }
    auto string_matcher_or = GetStringMatcher(child.string_value());
    if (!string_matcher_or.ok()) {
      return string_matcher_or.status();
    }
    paths->permissions.push_back(absl::make_unique<Rbac::Permission>());
    paths->permissions.back()->type = Rbac::Permission::RuleType::PATH;
    paths->permissions.back()->string_matcher =
        std::move(string_matcher_or.value());
  }
  if (!paths->permissions.empty()) {
    permissions->permissions.push_back(std::move(paths));
  }
  return absl::OkStatus();
}

absl::Status ParseRequest(Json* json, Rbac::Policy* policy) {
  Rbac::Permission permissions;
  permissions.type = Rbac::Permission::RuleType::AND;
  auto it = json->mutable_object()->find("paths");
  if (it != json->mutable_object()->end()) {
    if (it->second.type() != Json::Type::ARRAY) {
      return absl::InvalidArgumentError("\"paths\" is not an array.");
    }
    absl::Status parse_status = ParsePathsArray(&it->second, &permissions);
    if (!parse_status.ok()) return parse_status;
  }

  it = json->mutable_object()->find("headers");
  if (it != json->mutable_object()->end()) {
    if (it->second.type() != Json::Type::ARRAY) {
      return absl::InvalidArgumentError("\"headers\" is not an array.");
    }
    absl::Status status = ParseHeadersArray(&it->second, &permissions);
    if (!status.ok()) return status;
  }

  if (!permissions.permissions.empty()) {
    policy->permissions = std::move(permissions);
  } else {
    policy->permissions.type = Rbac::Permission::RuleType::ANY;
  }
  return absl::OkStatus();
}

absl::Status ParseRules(Json* json, std::string* policy_name,
                        Rbac::Policy* policy) {
  auto it = json->mutable_object()->find("name");
  if (it == json->mutable_object()->end()) {
    return absl::InvalidArgumentError("policy \"name\" field is not present.");
  }
  if (it->second.type() != Json::Type::STRING) {
    return absl::InvalidArgumentError("policy \"name\" is not a string.");
  }
  *policy_name += it->second.string_value();

  it = json->mutable_object()->find("source");
  if (it != json->mutable_object()->end()) {
    if (it->second.type() != Json::Type::OBJECT) {
      return absl::InvalidArgumentError("\"source\" is not an object.");
    }
    auto parse_status = ParsePeer(&it->second, policy);
    if (!parse_status.ok()) return parse_status;
  } else {
    policy->principals.type = Rbac::Principal::RuleType::ANY;
  }

  it = json->mutable_object()->find("request");
  if (it != json->mutable_object()->end()) {
    if (it->second.type() != Json::Type::OBJECT) {
      return absl::InvalidArgumentError("\"request\" is not an object.");
    }
    auto parse_status = ParseRequest(&it->second, policy);
    if (!parse_status.ok()) return parse_status;
  } else {
    policy->permissions.type = Rbac::Permission::RuleType::ANY;
  }
  return absl::OkStatus();
}

absl::Status ParseRulesArray(Json* json, const std::string& name, Rbac* rbac) {
  std::map<std::string, Rbac::Policy> policies;
  for (size_t i = 0; i < json->mutable_array()->size(); ++i) {
    Json& child = json->mutable_array()->at(i);
    if (child.type() != Json::Type::OBJECT) {
      return absl::InvalidArgumentError(
          absl::StrCat("rules ", i, " is not an object."));
    }
    Rbac::Policy policy;
    std::string policy_name = name + "_";
    absl::Status parse_status = ParseRules(&child, &policy_name, &policy);
    if (!parse_status.ok()) return parse_status;
    policies[policy_name] = std::move(policy);
  }
  rbac->policies = std::move(policies);
  return absl::OkStatus();
}

absl::Status ParseDenyRulesArray(Json* json, const std::string& name,
                                 Rbac* rbac) {
  rbac->action = Rbac::Action::DENY;
  absl::Status parse_status = ParseRulesArray(json, name, rbac);
  if (!parse_status.ok()) return parse_status;
  return absl::OkStatus();
}

absl::Status ParseAllowRulesArray(Json* json, const std::string& name,
                                  Rbac* rbac) {
  rbac->action = Rbac::Action::ALLOW;
  absl::Status parse_status = ParseRulesArray(json, name, rbac);
  if (!parse_status.ok()) return parse_status;
  return absl::OkStatus();
}

}  // namespace

absl::StatusOr<std::unique_ptr<RbacPolicies>> GenerateRbacPolicies(
    absl::string_view authz_policy) {
  grpc_error* error = GRPC_ERROR_NONE;
  auto json = Json::Parse(authz_policy, &error);
  if (error != GRPC_ERROR_NONE) {
    absl::Status status = absl::InvalidArgumentError(
        absl::StrCat("Failed to parse SDK authorization policy. Error: ",
                     grpc_error_string(error)));
    GRPC_ERROR_UNREF(error);
    return status;
  }
  if (json.type() != Json::Type::OBJECT) {
    return absl::InvalidArgumentError(
        "SDK authorization policy is not an object.");
  }

  std::unique_ptr<RbacPolicies> rbac_policies =
      absl::make_unique<RbacPolicies>();
  std::string name;
  auto it = json.mutable_object()->find("name");
  if (it == json.mutable_object()->end()) {
    return absl::InvalidArgumentError("\"name\" field is not present.");
  } else if (it->second.type() != Json::Type::STRING) {
    return absl::InvalidArgumentError("\"name\" is not a string.");
  } else {
    name = it->second.string_value();
  }

  it = json.mutable_object()->find("deny_rules");
  if (it != json.mutable_object()->end()) {
    if (it->second.type() != Json::Type::ARRAY) {
      return absl::InvalidArgumentError("\"deny_rules\" is not an array.");
    }
    Rbac deny_policy;
    absl::Status parse_status =
        ParseDenyRulesArray(&it->second, name, &deny_policy);
    if (!parse_status.ok()) return parse_status;
    rbac_policies->deny_policy = std::move(deny_policy);
  }

  it = json.mutable_object()->find("allow_rules");
  if (it == json.mutable_object()->end()) {
    return absl::InvalidArgumentError("\"allow_rules\" field is not present.");
  } else if (it->second.type() != Json::Type::ARRAY) {
    return absl::InvalidArgumentError("\"allow_rules\" is not an array.");
  } else {
    Rbac allow_policy;
    absl::Status parse_status =
        ParseAllowRulesArray(&it->second, name, &allow_policy);
    if (!parse_status.ok()) return parse_status;
    rbac_policies->allow_policy = std::move(allow_policy);
  }
  return std::move(rbac_policies);
}

}  // namespace grpc_core
