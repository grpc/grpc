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

#include "src/core/util/match.h"

namespace grpc_core {

//
// XdsMatcher
//

bool XdsMatcher::OnMatch::FindMatches(const MatchContext& context,
                                      Result& result) const {
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
        return matcher->FindMatches(context, result) && !keep_matching;
      });
}

//
// XdsMatcherList
//

bool XdsMatcherList::FindMatches(const MatchContext& context,
                                 Result& result) const {
  for (const auto& [predicate, on_match] : matchers_) {
    if (predicate->Match(context)) {
      if (on_match.FindMatches(context, result)) return true;
    }
  }
  if (on_no_match_.has_value()) {
    if (on_no_match_->FindMatches(context, result)) return true;
  }
  return false;
}

bool XdsMatcherList::AndPredicate::Match(
    const XdsMatcher::MatchContext& context) const {
  for (const auto& predicate : predicates_) {
    if (!predicate->Match(context)) return false;
  }
  return true;
}

bool XdsMatcherList::OrPredicate::Match(
    const XdsMatcher::MatchContext& context) const {
  for (const auto& predicate : predicates_) {
    if (predicate->Match(context)) return true;
  }
  return false;
}

//
// XdsMatcherExactMap
//

bool XdsMatcherExactMap::FindMatches(const MatchContext& context,
                                     Result& result) const {
  auto input = input_->GetValue(context);
  auto it = map_.find(input.value_or(""));
  if (it != map_.end()) {
    if (it->second.FindMatches(context, result)) return true;
  }
  if (on_no_match_.has_value()) {
    if (on_no_match_->FindMatches(context, result)) return true;
  }
  return false;
}

//
// XdsMatcherPrefixMap
//

void XdsMatcherPrefixMap::PopulateTrie(
    absl::flat_hash_map<std::string, std::shared_ptr<XdsMatcher::OnMatch>>
        map) {
  for (auto& [key, value] : map) {
    root_.addNode(key, std::move(value));
  }
}

bool XdsMatcherPrefixMap::FindMatches(const MatchContext& context,
                                      Result& result) const {
  auto input = input_->GetValue(context);
  auto value = root_.lookupLongestPrefix(input.value_or(""));
  if (value != nullptr) {
    if (value->FindMatches(context, result)) return true;
  }
  if (on_no_match_.has_value()) {
    if (on_no_match_->FindMatches(context, result)) return true;
  }
  return false;
}

}  // namespace grpc_core
