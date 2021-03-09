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

#include "src/core/lib/security/authorization/matchers.h"

namespace grpc_core {

namespace {

bool AuthenticatedMatchesHelper(const EvaluateArgs& args,
                                const StringMatcher& matcher) {
  if (args.GetTransportSecurityType() != "ssl") {
    // Connection is not ssl authenticated.
    return false;
  }
  if (matcher.string_matcher().empty()) {
    // Allows any authenticated user.
    return true;
  }
  absl::string_view spiffe_id = args.GetSpiffeId();
  if (!spiffe_id.empty()) {
    return matcher.Match(spiffe_id);
  }
  // TODO(ashithasantosh): Check principal matches DNS SAN, followed by Subject
  // field from certificate. This requires updating tsi_peer to expose these
  // fields.
  return false;
}

}  // namespace

std::unique_ptr<Matcher> Matcher::Create(const Rbac::Permission& permission) {
  switch (permission.type) {
    case Rbac::Permission::RuleType::AND:
      return absl::make_unique<AndMatcher>(permission.permissions,
                                           permission.not_rule);
    case Rbac::Permission::RuleType::OR:
      return absl::make_unique<OrMatcher>(permission.permissions,
                                          permission.not_rule);
    case Rbac::Permission::RuleType::ANY:
      return absl::make_unique<AlwaysMatcher>(permission.not_rule);
    case Rbac::Permission::RuleType::HEADER:
      return absl::make_unique<HttpHeaderMatcher>(permission.header_matcher,
                                                  permission.not_rule);
    case Rbac::Permission::RuleType::PATH:
      return absl::make_unique<PathMatcher>(permission.string_matcher,
                                            permission.not_rule);
    case Rbac::Permission::RuleType::DEST_IP:
      return absl::make_unique<IpMatcher>(permission.ip, permission.not_rule);
    case Rbac::Permission::RuleType::DEST_PORT:
      return absl::make_unique<PortMatcher>(permission.port,
                                            permission.not_rule);
    case Rbac::Permission::RuleType::REQ_SERVER_NAME:
      return absl::make_unique<RequestedServerNameMatcher>(
          permission.string_matcher, permission.not_rule);
  }
  gpr_log(GPR_ERROR, "Unexpected Permission rule type.");
}

std::unique_ptr<Matcher> Matcher::Create(const Rbac::Principal& principal) {
  switch (principal.type) {
    case Rbac::Principal::RuleType::AND:
      return absl::make_unique<AndMatcher>(principal.principals,
                                           principal.not_rule);
    case Rbac::Principal::RuleType::OR:
      return absl::make_unique<OrMatcher>(principal.principals,
                                          principal.not_rule);
    case Rbac::Principal::RuleType::ANY:
      return absl::make_unique<AlwaysMatcher>(principal.not_rule);
    case Rbac::Principal::RuleType::PRINCIPAL_NAME:
      return absl::make_unique<AuthenticatedMatcher>(principal.string_matcher,
                                                     principal.not_rule);
    case Rbac::Principal::RuleType::SOURCE_IP:
    case Rbac::Principal::RuleType::DIRECT_REMOTE_IP:
    case Rbac::Principal::RuleType::REMOTE_IP:
      return absl::make_unique<IpMatcher>(principal.ip, principal.not_rule);
    case Rbac::Principal::RuleType::HEADER:
      return absl::make_unique<HttpHeaderMatcher>(principal.header_matcher,
                                                  principal.not_rule);
    case Rbac::Principal::RuleType::PATH:
      return absl::make_unique<PathMatcher>(principal.string_matcher,
                                            principal.not_rule);
  }
  gpr_log(GPR_ERROR, "Unexpected Principal id type.");
}

AndMatcher::AndMatcher(
    const std::vector<std::unique_ptr<Rbac::Permission>>& rules, bool not_rule)
    : not_rule_(not_rule) {
  for (const auto& rule : rules) {
    matchers_.push_back(Matcher::Create(*rule));
  }
}

AndMatcher::AndMatcher(const std::vector<std::unique_ptr<Rbac::Principal>>& ids,
                       bool not_rule)
    : not_rule_(not_rule) {
  for (const auto& id : ids) {
    matchers_.push_back(Matcher::Create(*id));
  }
}

bool AndMatcher::Matches(const EvaluateArgs& args) const {
  bool matches = true;
  for (const auto& matcher : matchers_) {
    if (!matcher->Matches(args)) {
      matches = false;
      break;
    }
  }
  return matches != not_rule_;
}

OrMatcher::OrMatcher(
    const std::vector<std::unique_ptr<Rbac::Permission>>& rules, bool not_rule)
    : not_rule_(not_rule) {
  for (const auto& rule : rules) {
    matchers_.push_back(Matcher::Create(*rule));
  }
}

OrMatcher::OrMatcher(const std::vector<std::unique_ptr<Rbac::Principal>>& ids,
                     bool not_rule)
    : not_rule_(not_rule) {
  for (const auto& id : ids) {
    matchers_.push_back(Matcher::Create(*id));
  }
}

bool OrMatcher::Matches(const EvaluateArgs& args) const {
  bool matches = false;
  for (const auto& matcher : matchers_) {
    if (matcher->Matches(args)) {
      matches = true;
      break;
    }
  }
  return matches != not_rule_;
}

bool HttpHeaderMatcher::Matches(const EvaluateArgs& args) const {
  std::string concatenated_value;
  bool matches =
      matcher_.Match(args.GetHeaderValue(matcher_.name(), &concatenated_value));
  return matches != not_rule_;
}

// TODO(ashithasantosh): Implement IpMatcher::Matches.
bool IpMatcher::Matches(const EvaluateArgs&) const {
  bool matches = false;
  return matches != not_rule_;
}

bool PortMatcher::Matches(const EvaluateArgs& args) const {
  bool matches = (port_ == args.GetLocalPort());
  return matches != not_rule_;
}

bool AuthenticatedMatcher::Matches(const EvaluateArgs& args) const {
  bool matches = AuthenticatedMatchesHelper(args, matcher_);
  return matches != not_rule_;
}

bool RequestedServerNameMatcher::Matches(const EvaluateArgs&) const {
  // Currently we do not support matching rules containing
  // "requested_server_name".
  bool matches = false;
  return matches != not_rule_;
}

bool PathMatcher::Matches(const EvaluateArgs& args) const {
  bool matches = false;
  absl::string_view path = args.GetPath();
  if (!path.empty()) {
    matches = matcher_.Match(path);
  }
  return matches != not_rule_;
}

bool PolicyMatcher::Matches(const EvaluateArgs& args) const {
  return permissions_->Matches(args) && principals_->Matches(args);
}

}  // namespace grpc_core
