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

#ifndef GRPC_CORE_LIB_SECURITY_AUTHORIZATION_MATCHERS_H
#define GRPC_CORE_LIB_SECURITY_AUTHORIZATION_MATCHERS_H

#include <grpc/support/port_platform.h>

#include <memory>

#include "src/core/lib/matchers/matchers.h"
#include "src/core/lib/security/authorization/evaluate_args.h"
#include "src/core/lib/security/authorization/rbac_policy.h"

namespace grpc_core {

// Matchers describe the rules for matching permission or principal.
class Matcher {
 public:
  virtual ~Matcher() = default;

  // Returns whether or not the permission/principal matches the rules of the
  // matcher.
  virtual bool Matches(const EvaluateArgs& args) const = 0;

  // Creates an instance of a matcher based off the rules defined in Permission
  // config.
  static std::unique_ptr<Matcher> Create(const Rbac::Permission& permission);

  // Creates an instance of a matcher based off the rules defined in Principal
  // config.
  static std::unique_ptr<Matcher> Create(const Rbac::Principal& principal);
};

class AlwaysMatcher : public Matcher {
 public:
  explicit AlwaysMatcher(bool not_rule = false) : not_rule_(not_rule) {}

  bool Matches(const EvaluateArgs&) const override { return !not_rule_; }

 private:
  // Negates matching the provided permission/principal.
  const bool not_rule_;
};

class AndMatcher : public Matcher {
 public:
  explicit AndMatcher(
      const std::vector<std::unique_ptr<Rbac::Permission>>& rules,
      bool not_rule = false);
  explicit AndMatcher(const std::vector<std::unique_ptr<Rbac::Principal>>& ids,
                      bool not_rule = false);

  bool Matches(const EvaluateArgs& args) const override;

 private:
  std::vector<std::unique_ptr<Matcher>> matchers_;
  // Negates matching the provided permission/principal.
  const bool not_rule_;
};

class OrMatcher : public Matcher {
 public:
  explicit OrMatcher(
      const std::vector<std::unique_ptr<Rbac::Permission>>& rules,
      bool not_rule = false);
  explicit OrMatcher(const std::vector<std::unique_ptr<Rbac::Principal>>& ids,
                     bool not_rule = false);

  bool Matches(const EvaluateArgs& args) const override;

 private:
  std::vector<std::unique_ptr<Matcher>> matchers_;
  // Negates matching the provided permission/principal.
  const bool not_rule_;
};

// Perform a match against HTTP headers.
class HttpHeaderMatcher : public Matcher {
 public:
  explicit HttpHeaderMatcher(const HeaderMatcher& matcher,
                             bool not_rule = false)
      : matcher_(matcher), not_rule_(not_rule) {}

  bool Matches(const EvaluateArgs& args) const override;

 private:
  const HeaderMatcher matcher_;
  // Negates matching the provided permission/principal.
  const bool not_rule_;
};

// Perform a match against IP Cidr Range.
// TODO(ashithasantosh): Handle type of Ip or use seperate matchers for each
// type. Implement Match functionality, this would require updating EvaluateArgs
// getters, to return type of IP as well.
class IpMatcher : public Matcher {
 public:
  explicit IpMatcher(const Rbac::CidrRange& range, bool not_rule = false)
      : range_(range), not_rule_(not_rule) {}

  bool Matches(const EvaluateArgs&) const override;

 private:
  const Rbac::CidrRange range_;
  // Negates matching the provided permission/principal.
  const bool not_rule_;
};

// Matches the port number of the destination (local) address.
class PortMatcher : public Matcher {
 public:
  explicit PortMatcher(const int port, bool not_rule = false)
      : port_(port), not_rule_(not_rule) {}

  bool Matches(const EvaluateArgs& args) const override;

 private:
  const int port_;
  // Negates matching the provided permission/principal.
  const bool not_rule_;
};

// Matches the principal name as described in the peer certificate. Uses URI SAN
// or DNS SAN in that order, otherwise uses subject field.
class AuthenticatedMatcher : public Matcher {
 public:
  explicit AuthenticatedMatcher(const StringMatcher& auth,
                                bool not_rule = false)
      : matcher_(auth), not_rule_(not_rule) {}

  bool Matches(const EvaluateArgs& args) const override;

 private:
  const StringMatcher matcher_;
  // Negates matching the provided permission/principal.
  const bool not_rule_;
};

// Perform a match against the request server from the client's connection
// request. This is typically TLS SNI. Currently unsupported.
class RequestedServerNameMatcher : public Matcher {
 public:
  explicit RequestedServerNameMatcher(
      const StringMatcher& requested_server_name, bool not_rule = false)
      : matcher_(requested_server_name), not_rule_(not_rule) {}

  bool Matches(const EvaluateArgs&) const override;

 private:
  const StringMatcher matcher_;
  // Negates matching the provided permission/principal.
  const bool not_rule_;
};

// Perform a match against the path header of HTTP request.
class PathMatcher : public Matcher {
 public:
  explicit PathMatcher(const StringMatcher& path, bool not_rule = false)
      : matcher_(path), not_rule_(not_rule) {}

  bool Matches(const EvaluateArgs& args) const override;

 private:
  const StringMatcher matcher_;
  // Negates matching the provided permission/principal.
  const bool not_rule_;
};

// Performs a match for policy field in RBAC, which is a collection of
// permission and principal matchers. Policy matches iff, we find a match in one
// of its permissions and a match in one of its principals.
class PolicyMatcher : public Matcher {
 public:
  explicit PolicyMatcher(const Rbac::Policy& policy)
      : permissions_(Matcher::Create(policy.permissions)),
        principals_(Matcher::Create(policy.principals)) {}

  bool Matches(const EvaluateArgs& args) const override;

 private:
  std::unique_ptr<Matcher> permissions_;
  std::unique_ptr<Matcher> principals_;
};

}  // namespace grpc_core

#endif  // GRPC_CORE_LIB_SECURITY_AUTHORIZATION_MATCHERS_H
