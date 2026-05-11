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

#include "src/core/lib/security/authorization/matchers.h"

#include <grpc/grpc_security_constants.h>
#include <grpc/support/port_platform.h>
#include <string.h>

#include <string>

#include "src/core/lib/address_utils/parse_address.h"
#include "src/core/lib/address_utils/sockaddr_utils.h"
#include "src/core/util/uri.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"

namespace grpc_core {

namespace {

// Canonicalize a request :path value before comparing it against an
// authorization policy rule.
//
// Background. The HTTP/2 :path pseudo-header MUST take the "path-absolute"
// form (RFC 3986 §3.3) for http/https URIs (RFC 9113 §8.3.1). xDS RBAC
// rules are validated at config-ingest time (xds_route_config_parser.cc)
// against the same canonical shape. At request time the gRPC C++ matcher
// previously compared raw byte sequences, which let a client defeat a deny
// rule by sending any of the following non-canonical variants:
//
//   * "package.Service/Method"      (missing leading slash)
//   * "//package.Service/Method"    (consecutive slashes)
//   * "/x/../package.Service/Method" (dot-segment traversal)
//   * "/%70ackage.Service/Method"   (percent-encoded prefix character)
//
// The grpc-Go fix for the same defect (CVE-2026-33186, fixed in v1.79.3)
// rejects non-canonical :path at the HPACK pseudo-header layer. Here we
// apply the equivalent normalization at the RBAC matcher so the matcher
// sees a canonical path regardless of the client's framing.
//
// Returns false when the input cannot be reduced to a path-absolute form
// (empty after decode, or dot-segment traversal escapes the root).
bool CanonicalizePathForAuthorization(absl::string_view raw,
                                      std::string* out) {
  if (raw.empty()) {
    return false;
  }
  std::string decoded = URI::PercentDecode(raw);
  if (decoded.empty()) {
    return false;
  }
  if (decoded.front() != '/') {
    decoded.insert(decoded.begin(), '/');
  }
  // Walk segments, applying RFC 3986 §5.2.4 dot-segment removal and
  // collapsing consecutive slashes in one pass.
  std::vector<absl::string_view> segments;
  size_t i = 1;  // skip the leading slash
  while (i <= decoded.size()) {
    size_t j = decoded.find('/', i);
    if (j == std::string::npos) j = decoded.size();
    absl::string_view seg(decoded.data() + i, j - i);
    if (seg.empty() || seg == ".") {
      // empty segment (collapse //) or current-directory marker — skip
    } else if (seg == "..") {
      if (segments.empty()) {
        // Traversal above root is not representable as a path-absolute.
        return false;
      }
      segments.pop_back();
    } else {
      segments.push_back(seg);
    }
    i = j + 1;
  }
  std::string result;
  result.reserve(decoded.size());
  for (absl::string_view seg : segments) {
    result.push_back('/');
    result.append(seg.data(), seg.size());
  }
  if (result.empty()) result.push_back('/');
  *out = std::move(result);
  return true;
}

}  // namespace

std::unique_ptr<AuthorizationMatcher> AuthorizationMatcher::Create(
    Rbac::Permission permission) {
  switch (permission.type) {
    case Rbac::Permission::RuleType::kAnd: {
      std::vector<std::unique_ptr<AuthorizationMatcher>> matchers;
      matchers.reserve(permission.permissions.size());
      for (const auto& rule : permission.permissions) {
        matchers.push_back(AuthorizationMatcher::Create(std::move(*rule)));
      }
      return std::make_unique<AndAuthorizationMatcher>(std::move(matchers));
    }
    case Rbac::Permission::RuleType::kOr: {
      std::vector<std::unique_ptr<AuthorizationMatcher>> matchers;
      matchers.reserve(permission.permissions.size());
      for (const auto& rule : permission.permissions) {
        matchers.push_back(AuthorizationMatcher::Create(std::move(*rule)));
      }
      return std::make_unique<OrAuthorizationMatcher>(std::move(matchers));
    }
    case Rbac::Permission::RuleType::kNot:
      return std::make_unique<NotAuthorizationMatcher>(
          AuthorizationMatcher::Create(std::move(*permission.permissions[0])));
    case Rbac::Permission::RuleType::kAny:
      return std::make_unique<AlwaysAuthorizationMatcher>();
    case Rbac::Permission::RuleType::kHeader:
      return std::make_unique<HeaderAuthorizationMatcher>(
          std::move(permission.header_matcher));
    case Rbac::Permission::RuleType::kPath:
      return std::make_unique<PathAuthorizationMatcher>(
          std::move(permission.string_matcher));
    case Rbac::Permission::RuleType::kDestIp:
      return std::make_unique<IpAuthorizationMatcher>(
          IpAuthorizationMatcher::Type::kDestIp, std::move(permission.ip));
    case Rbac::Permission::RuleType::kDestPort:
      return std::make_unique<PortAuthorizationMatcher>(permission.port);
    case Rbac::Permission::RuleType::kMetadata:
      return std::make_unique<MetadataAuthorizationMatcher>(permission.invert);
    case Rbac::Permission::RuleType::kReqServerName:
      return std::make_unique<ReqServerNameAuthorizationMatcher>(
          std::move(permission.string_matcher));
  }
  return nullptr;
}

std::unique_ptr<AuthorizationMatcher> AuthorizationMatcher::Create(
    Rbac::Principal principal) {
  switch (principal.type) {
    case Rbac::Principal::RuleType::kAnd: {
      std::vector<std::unique_ptr<AuthorizationMatcher>> matchers;
      matchers.reserve(principal.principals.size());
      for (const auto& id : principal.principals) {
        matchers.push_back(AuthorizationMatcher::Create(std::move(*id)));
      }
      return std::make_unique<AndAuthorizationMatcher>(std::move(matchers));
    }
    case Rbac::Principal::RuleType::kOr: {
      std::vector<std::unique_ptr<AuthorizationMatcher>> matchers;
      matchers.reserve(principal.principals.size());
      for (const auto& id : principal.principals) {
        matchers.push_back(AuthorizationMatcher::Create(std::move(*id)));
      }
      return std::make_unique<OrAuthorizationMatcher>(std::move(matchers));
    }
    case Rbac::Principal::RuleType::kNot:
      return std::make_unique<NotAuthorizationMatcher>(
          AuthorizationMatcher::Create(std::move(*principal.principals[0])));
    case Rbac::Principal::RuleType::kAny:
      return std::make_unique<AlwaysAuthorizationMatcher>();
    case Rbac::Principal::RuleType::kPrincipalName:
      return std::make_unique<AuthenticatedAuthorizationMatcher>(
          std::move(principal.string_matcher));
    case Rbac::Principal::RuleType::kSourceIp:
      return std::make_unique<IpAuthorizationMatcher>(
          IpAuthorizationMatcher::Type::kSourceIp, std::move(principal.ip));
    case Rbac::Principal::RuleType::kDirectRemoteIp:
      return std::make_unique<IpAuthorizationMatcher>(
          IpAuthorizationMatcher::Type::kDirectRemoteIp,
          std::move(principal.ip));
    case Rbac::Principal::RuleType::kRemoteIp:
      return std::make_unique<IpAuthorizationMatcher>(
          IpAuthorizationMatcher::Type::kRemoteIp, std::move(principal.ip));
    case Rbac::Principal::RuleType::kHeader:
      return std::make_unique<HeaderAuthorizationMatcher>(
          std::move(principal.header_matcher));
    case Rbac::Principal::RuleType::kPath:
      return std::make_unique<PathAuthorizationMatcher>(
          std::move(principal.string_matcher.value()));
    case Rbac::Principal::RuleType::kMetadata:
      return std::make_unique<MetadataAuthorizationMatcher>(principal.invert);
  }
  return nullptr;
}

bool AndAuthorizationMatcher::Matches(const EvaluateArgs& args) const {
  for (const auto& matcher : matchers_) {
    if (!matcher->Matches(args)) {
      return false;
    }
  }
  return true;
}

bool OrAuthorizationMatcher::Matches(const EvaluateArgs& args) const {
  for (const auto& matcher : matchers_) {
    if (matcher->Matches(args)) {
      return true;
    }
  }
  return false;
}

bool NotAuthorizationMatcher::Matches(const EvaluateArgs& args) const {
  return !matcher_->Matches(args);
}

bool HeaderAuthorizationMatcher::Matches(const EvaluateArgs& args) const {
  std::string concatenated_value;
  return matcher_.Match(
      args.GetHeaderValue(matcher_.name(), &concatenated_value));
}

IpAuthorizationMatcher::IpAuthorizationMatcher(Type type, Rbac::CidrRange range)
    : type_(type), prefix_len_(range.prefix_len) {
  auto address =
      StringToSockaddr(range.address_prefix, 0);  // Port does not matter here.
  if (!address.ok()) {
    VLOG(2) << "CidrRange address \"" << range.address_prefix
            << "\" is not IPv4/IPv6. Error: " << address.status();
    memset(&subnet_address_, 0, sizeof(subnet_address_));
    return;
  }
  subnet_address_ = *address;
  grpc_sockaddr_mask_bits(&subnet_address_, prefix_len_);
}

bool IpAuthorizationMatcher::Matches(const EvaluateArgs& args) const {
  grpc_resolved_address address;
  switch (type_) {
    case Type::kDestIp: {
      address = args.GetLocalAddress();
      break;
    }
    case Type::kSourceIp:
    case Type::kDirectRemoteIp:
    case Type::kRemoteIp: {
      address = args.GetPeerAddress();
      break;
    }
    default:
      return false;
  }
  return grpc_sockaddr_match_subnet(&address, &subnet_address_, prefix_len_);
}

bool PortAuthorizationMatcher::Matches(const EvaluateArgs& args) const {
  return port_ == args.GetLocalPort();
}

bool AuthenticatedAuthorizationMatcher::Matches(
    const EvaluateArgs& args) const {
  if (args.GetTransportSecurityType() != GRPC_SSL_TRANSPORT_SECURITY_TYPE &&
      args.GetTransportSecurityType() != GRPC_TLS_TRANSPORT_SECURITY_TYPE) {
    // Connection is not authenticated.
    return false;
  }
  if (!matcher_.has_value()) {
    // Allows any authenticated user.
    return true;
  }
  std::vector<absl::string_view> uri_sans = args.GetUriSans();
  if (!uri_sans.empty()) {
    for (const auto& uri : uri_sans) {
      if (matcher_->Match(uri)) {
        return true;
      }
    }
  }
  std::vector<absl::string_view> dns_sans = args.GetDnsSans();
  if (!dns_sans.empty()) {
    for (const auto& dns : dns_sans) {
      if (matcher_->Match(dns)) {
        return true;
      }
    }
  }
  return matcher_->Match(args.GetSubject());
}

bool ReqServerNameAuthorizationMatcher::Matches(const EvaluateArgs&) const {
  // Currently we only support matching against an empty string.
  return matcher_.Match("");
}

bool PathAuthorizationMatcher::Matches(const EvaluateArgs& args) const {
  absl::string_view path = args.GetPath();
  if (path.empty()) {
    return false;
  }
  std::string canonical;
  if (!CanonicalizePathForAuthorization(path, &canonical)) {
    return false;
  }
  return matcher_.Match(canonical);
}

bool PolicyAuthorizationMatcher::Matches(const EvaluateArgs& args) const {
  return permissions_->Matches(args) && principals_->Matches(args);
}

}  // namespace grpc_core
