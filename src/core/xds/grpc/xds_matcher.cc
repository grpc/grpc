//
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
//

#include "src/core/xds/grpc/xds_matcher.h"

namespace grpc_core {

//
// Predicates
//

bool AndPredicate::Match() const {
  for (const auto& predicate : predicates_) {
    if (!predicate->Match()) return false;
  }
  return true;
}

bool OrPredicate::Match() const {
  for (const auto& predicate : predicates_) {
    if (predicate->Match()) return true;
  }
  return false;
}

//
// XdsMatcher
//

bool XdsMatcher::OnMatch::FindMatches(Result& result) const {
  // Confusingly, this Match() call has nothing to do with the
  // matching we're doing here -- it's the name of our utility
  // method for accessing fields of std::variant<>.
  return Match(
      action,
      [&](const std::unique_ptr<Action>& action) {
        result.push_back(action.get());
        return !keep_matching;
      },
      [&](const std::unique_ptr<XdsMatcher>& matcher) {
        return matcher->FindMatches(result) && !keep_matching;
      });
}

bool MatcherList::FindMatches(Result& result) const {
  for (const auto& [predicate, on_match] : matchers_) {
    if (predicate->Match()) {
      if (on_match.FindMatches(result)) return true;
    }
  }
  if (on_no_match_.has_value()) {
    if (on_no_match_->FindMatches(result)) return true;
  }
  return false;
}

bool MatcherExactMap::FindMatches(Result& result) const {
  auto it = map_.find(input_.value());
  if (it != map_.end()) {
    if (it->second.FindMatches(result)) return true;
  }
  if (on_no_match_.has_value()) {
    if (on_no_match_->FindMatches(result)) return true;
  }
  return false;
}

}  // namespace grpc_core
