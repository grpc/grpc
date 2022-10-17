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

#include <stddef.h>

#include <algorithm>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/strings/strip.h"

#include "src/core/lib/gpr/useful.h"
#include "src/core/lib/json/json.h"
#include "src/core/lib/matchers/matchers.h"

namespace grpc_core {

namespace {

absl::string_view GetMatcherType(absl::string_view value,
                                 StringMatcher::Type* type) {
  if (value == "*") {
    *type = StringMatcher::Type::kSafeRegex;
    // Presence match checks for non empty strings.
    return ".+";
  } else if (absl::StartsWith(value, "*")) {
    *type = StringMatcher::Type::kSuffix;
    return absl::StripPrefix(value, "*");
  } else if (absl::EndsWith(value, "*")) {
    *type = StringMatcher::Type::kPrefix;
    return absl::StripSuffix(value, "*");
  }
  *type = StringMatcher::Type::kExact;
  return value;
}

absl::StatusOr<StringMatcher> GetStringMatcher(absl::string_view value) {
  StringMatcher::Type type;
  absl::string_view matcher = GetMatcherType(value, &type);
  return StringMatcher::Create(type, matcher);
}

absl::StatusOr<HeaderMatcher> GetHeaderMatcher(absl::string_view name,
                                               absl::string_view value) {
  StringMatcher::Type type;
  absl::string_view matcher = GetMatcherType(value, &type);
  return HeaderMatcher::Create(name, static_cast<HeaderMatcher::Type>(type),
                               matcher);
}

bool IsUnsupportedHeader(absl::string_view header_name) {
  static const char* const kUnsupportedHeaders[] = {"host",
                                                    "connection",
                                                    "keep-alive",
                                                    "proxy-authenticate",
                                                    "proxy-authorization",
                                                    "te",
                                                    "trailer",
                                                    "transfer-encoding",
                                                    "upgrade"};
  for (size_t i = 0; i < GPR_ARRAY_SIZE(kUnsupportedHeaders); ++i) {
    if (absl::EqualsIgnoreCase(header_name, kUnsupportedHeaders[i])) {
      return true;
    }
  }
  return false;
}

absl::StatusOr<Rbac::Principal> ParsePrincipalsArray(const Json& json) {
  std::vector<std::unique_ptr<Rbac::Principal>> principal_names;
  for (size_t i = 0; i < json.array_value().size(); ++i) {
    const Json& child = json.array_value().at(i);
    if (child.type() != Json::Type::STRING) {
      return absl::InvalidArgumentError(
          absl::StrCat("\"principals\" ", i, ": is not a string."));
    }
    auto matcher_or = GetStringMatcher(child.string_value());
    if (!matcher_or.ok()) {
      return absl::Status(matcher_or.status().code(),
                          absl::StrCat("\"principals\" ", i, ": ",
                                       matcher_or.status().message()));
    }
    principal_names.push_back(std::make_unique<Rbac::Principal>(
        Rbac::Principal::MakeAuthenticatedPrincipal(
            std::move(matcher_or.value()))));
  }
  return Rbac::Principal::MakeOrPrincipal(std::move(principal_names));
}

absl::StatusOr<Rbac::Principal> ParsePeer(const Json& json) {
  std::vector<std::unique_ptr<Rbac::Principal>> peer;
  auto it = json.object_value().find("principals");
  if (it != json.object_value().end()) {
    if (it->second.type() != Json::Type::ARRAY) {
      return absl::InvalidArgumentError("\"principals\" is not an array.");
    }
    auto principal_names_or = ParsePrincipalsArray(it->second);
    if (!principal_names_or.ok()) return principal_names_or.status();
    if (!principal_names_or.value().principals.empty()) {
      peer.push_back(std::make_unique<Rbac::Principal>(
          std::move(principal_names_or.value())));
    }
  }
  if (peer.empty()) {
    return Rbac::Principal::MakeAnyPrincipal();
  }
  return Rbac::Principal::MakeAndPrincipal(std::move(peer));
}

absl::StatusOr<Rbac::Permission> ParseHeaderValues(
    const Json& json, absl::string_view header_name) {
  if (json.array_value().empty()) {
    return absl::InvalidArgumentError("\"values\" list is empty.");
  }
  std::vector<std::unique_ptr<Rbac::Permission>> values;
  for (size_t i = 0; i < json.array_value().size(); ++i) {
    const Json& child = json.array_value().at(i);
    if (child.type() != Json::Type::STRING) {
      return absl::InvalidArgumentError(
          absl::StrCat("\"values\" ", i, ": is not a string."));
    }
    auto matcher_or = GetHeaderMatcher(header_name, child.string_value());
    if (!matcher_or.ok()) {
      return absl::Status(
          matcher_or.status().code(),
          absl::StrCat("\"values\" ", i, ": ", matcher_or.status().message()));
    }
    values.push_back(std::make_unique<Rbac::Permission>(
        Rbac::Permission::MakeHeaderPermission(std::move(matcher_or.value()))));
  }
  return Rbac::Permission::MakeOrPermission(std::move(values));
}

absl::StatusOr<Rbac::Permission> ParseHeaders(const Json& json) {
  auto it = json.object_value().find("key");
  if (it == json.object_value().end()) {
    return absl::InvalidArgumentError("\"key\" is not present.");
  }
  if (it->second.type() != Json::Type::STRING) {
    return absl::InvalidArgumentError("\"key\" is not a string.");
  }
  absl::string_view header_name = it->second.string_value();
  if (absl::StartsWith(header_name, ":") ||
      absl::StartsWith(header_name, "grpc-") ||
      IsUnsupportedHeader(header_name)) {
    return absl::InvalidArgumentError(
        absl::StrFormat("Unsupported \"key\" %s.", header_name));
  }
  it = json.object_value().find("values");
  if (it == json.object_value().end()) {
    return absl::InvalidArgumentError("\"values\" is not present.");
  }
  if (it->second.type() != Json::Type::ARRAY) {
    return absl::InvalidArgumentError("\"values\" is not an array.");
  }
  return ParseHeaderValues(it->second, header_name);
}

absl::StatusOr<Rbac::Permission> ParseHeadersArray(const Json& json) {
  std::vector<std::unique_ptr<Rbac::Permission>> headers;
  for (size_t i = 0; i < json.array_value().size(); ++i) {
    const Json& child = json.array_value().at(i);
    if (child.type() != Json::Type::OBJECT) {
      return absl::InvalidArgumentError(
          absl::StrCat("\"headers\" ", i, ": is not an object."));
    }
    auto headers_or = ParseHeaders(child);
    if (!headers_or.ok()) {
      return absl::Status(
          headers_or.status().code(),
          absl::StrCat("\"headers\" ", i, ": ", headers_or.status().message()));
    }
    headers.push_back(
        std::make_unique<Rbac::Permission>(std::move(headers_or.value())));
  }
  return Rbac::Permission::MakeAndPermission(std::move(headers));
}

absl::StatusOr<Rbac::Permission> ParsePathsArray(const Json& json) {
  std::vector<std::unique_ptr<Rbac::Permission>> paths;
  for (size_t i = 0; i < json.array_value().size(); ++i) {
    const Json& child = json.array_value().at(i);
    if (child.type() != Json::Type::STRING) {
      return absl::InvalidArgumentError(
          absl::StrCat("\"paths\" ", i, ": is not a string."));
    }
    auto matcher_or = GetStringMatcher(child.string_value());
    if (!matcher_or.ok()) {
      return absl::Status(
          matcher_or.status().code(),
          absl::StrCat("\"paths\" ", i, ": ", matcher_or.status().message()));
    }
    paths.push_back(std::make_unique<Rbac::Permission>(
        Rbac::Permission::MakePathPermission(std::move(matcher_or.value()))));
  }
  return Rbac::Permission::MakeOrPermission(std::move(paths));
}

absl::StatusOr<Rbac::Permission> ParseRequest(const Json& json) {
  std::vector<std::unique_ptr<Rbac::Permission>> request;
  auto it = json.object_value().find("paths");
  if (it != json.object_value().end()) {
    if (it->second.type() != Json::Type::ARRAY) {
      return absl::InvalidArgumentError("\"paths\" is not an array.");
    }
    auto paths_or = ParsePathsArray(it->second);
    if (!paths_or.ok()) return paths_or.status();
    if (!paths_or.value().permissions.empty()) {
      request.push_back(
          std::make_unique<Rbac::Permission>(std::move(paths_or.value())));
    }
  }
  it = json.object_value().find("headers");
  if (it != json.object_value().end()) {
    if (it->second.type() != Json::Type::ARRAY) {
      return absl::InvalidArgumentError("\"headers\" is not an array.");
    }
    auto headers_or = ParseHeadersArray(it->second);
    if (!headers_or.ok()) return headers_or.status();
    if (!headers_or.value().permissions.empty()) {
      request.push_back(
          std::make_unique<Rbac::Permission>(std::move(headers_or.value())));
    }
  }
  if (request.empty()) {
    return Rbac::Permission::MakeAnyPermission();
  }
  return Rbac::Permission::MakeAndPermission(std::move(request));
}

absl::StatusOr<Rbac::Policy> ParseRules(const Json& json) {
  Rbac::Principal principals;
  auto it = json.object_value().find("source");
  if (it != json.object_value().end()) {
    if (it->second.type() != Json::Type::OBJECT) {
      return absl::InvalidArgumentError("\"source\" is not an object.");
    }
    auto peer_or = ParsePeer(it->second);
    if (!peer_or.ok()) return peer_or.status();
    principals = std::move(peer_or.value());
  } else {
    principals = Rbac::Principal::MakeAnyPrincipal();
  }
  Rbac::Permission permissions;
  it = json.object_value().find("request");
  if (it != json.object_value().end()) {
    if (it->second.type() != Json::Type::OBJECT) {
      return absl::InvalidArgumentError("\"request\" is not an object.");
    }
    auto request_or = ParseRequest(it->second);
    if (!request_or.ok()) return request_or.status();
    permissions = std::move(request_or.value());
  } else {
    permissions = Rbac::Permission::MakeAnyPermission();
  }
  return Rbac::Policy(std::move(permissions), std::move(principals));
}

absl::StatusOr<std::map<std::string, Rbac::Policy>> ParseRulesArray(
    const Json& json, absl::string_view name) {
  std::map<std::string, Rbac::Policy> policies;
  for (size_t i = 0; i < json.array_value().size(); ++i) {
    const Json& child = json.array_value().at(i);
    if (child.type() != Json::Type::OBJECT) {
      return absl::InvalidArgumentError(
          absl::StrCat("rules ", i, ": is not an object."));
    }
    auto it = child.object_value().find("name");
    if (it == child.object_value().end()) {
      return absl::InvalidArgumentError(
          absl::StrCat("rules ", i, ": \"name\" is not present."));
    }
    if (it->second.type() != Json::Type::STRING) {
      return absl::InvalidArgumentError(
          absl::StrCat("rules ", i, ": \"name\" is not a string."));
    }
    std::string policy_name =
        std::string(name) + "_" + it->second.string_value();
    auto policy_or = ParseRules(child);
    if (!policy_or.ok()) {
      return absl::Status(
          policy_or.status().code(),
          absl::StrCat("rules ", i, ": ", policy_or.status().message()));
    }
    policies[policy_name] = std::move(policy_or.value());
  }
  return std::move(policies);
}

absl::StatusOr<Rbac> ParseDenyRulesArray(const Json& json,
                                         absl::string_view name) {
  auto policies_or = ParseRulesArray(json, name);
  if (!policies_or.ok()) return policies_or.status();
  return Rbac(Rbac::Action::kDeny, std::move(policies_or.value()));
}

absl::StatusOr<Rbac> ParseAllowRulesArray(const Json& json,
                                          absl::string_view name) {
  auto policies_or = ParseRulesArray(json, name);
  if (!policies_or.ok()) return policies_or.status();
  return Rbac(Rbac::Action::kAllow, std::move(policies_or.value()));
}

}  // namespace

absl::StatusOr<RbacPolicies> GenerateRbacPolicies(
    absl::string_view authz_policy) {
  auto json = Json::Parse(authz_policy);
  if (!json.ok()) {
    return absl::InvalidArgumentError(
        absl::StrCat("Failed to parse gRPC authorization policy. Error: ",
                     json.status().ToString()));
  }
  if (json->type() != Json::Type::OBJECT) {
    return absl::InvalidArgumentError(
        "SDK authorization policy is not an object.");
  }
  auto it = json->mutable_object()->find("name");
  if (it == json->mutable_object()->end()) {
    return absl::InvalidArgumentError("\"name\" field is not present.");
  }
  if (it->second.type() != Json::Type::STRING) {
    return absl::InvalidArgumentError("\"name\" is not a string.");
  }
  absl::string_view name = it->second.string_value();
  RbacPolicies rbac_policies;
  it = json->mutable_object()->find("deny_rules");
  if (it != json->mutable_object()->end()) {
    if (it->second.type() != Json::Type::ARRAY) {
      return absl::InvalidArgumentError("\"deny_rules\" is not an array.");
    }
    auto deny_policy_or = ParseDenyRulesArray(it->second, name);
    if (!deny_policy_or.ok()) {
      return absl::Status(
          deny_policy_or.status().code(),
          absl::StrCat("deny_", deny_policy_or.status().message()));
    }
    rbac_policies.deny_policy = std::move(deny_policy_or.value());
  } else {
    rbac_policies.deny_policy.action = Rbac::Action::kDeny;
  }
  it = json->mutable_object()->find("allow_rules");
  if (it == json->mutable_object()->end()) {
    return absl::InvalidArgumentError("\"allow_rules\" is not present.");
  }
  if (it->second.type() != Json::Type::ARRAY) {
    return absl::InvalidArgumentError("\"allow_rules\" is not an array.");
  }
  auto allow_policy_or = ParseAllowRulesArray(it->second, name);
  if (!allow_policy_or.ok()) {
    return absl::Status(
        allow_policy_or.status().code(),
        absl::StrCat("allow_", allow_policy_or.status().message()));
  }
  rbac_policies.allow_policy = std::move(allow_policy_or.value());
  return std::move(rbac_policies);
}

}  // namespace grpc_core
