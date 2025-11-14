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

#include <algorithm>
#include <string>
#include <vector>

#include "src/core/util/match.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"

namespace grpc_core {

//
// XdsMatcher
//

bool XdsMatcher::OnMatch::operator==(const OnMatch& other) const {
  if (keep_matching != other.keep_matching) return false;
  if (action.index() != other.action.index()) return false;
  return Match(
      action,
      [&](const std::unique_ptr<Action>& action) {
        const auto& other_action =
            std::get<std::unique_ptr<Action>>(other.action);
        if (action == nullptr) return other_action == nullptr;
        if (other_action == nullptr) return false;
        return action->Equals(*other_action);
      },
      [&](const std::unique_ptr<XdsMatcher>& matcher) {
        const auto& other_matcher =
            std::get<std::unique_ptr<XdsMatcher>>(other.action);
        if (matcher == nullptr) return other_matcher == nullptr;
        if (other_matcher == nullptr) return false;
        return matcher->Equals(*other_matcher);
      });
}

std::string XdsMatcher::OnMatch::ToString() const {
  std::vector<std::string> parts;
  parts.push_back(Match(
      action,
      [](const std::unique_ptr<Action>& action) {
        return absl::StrCat("action=", action->ToString());
      },
      [](const std::unique_ptr<XdsMatcher>& matcher) {
        return absl::StrCat("matcher=", matcher->ToString());
      }));
  parts.push_back(
      absl::StrCat("keep_matching=", keep_matching ? "true" : "false"));
  return absl::StrCat("{", absl::StrJoin(parts, ", "), "}");
}

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

bool XdsMatcherList::Equals(const XdsMatcher& other) const {
  if (type() != other.type()) return false;
  const auto& o = DownCast<const XdsMatcherList&>(other);
  if (matchers_.size() != o.matchers_.size()) return false;
  for (size_t i = 0; i < matchers_.size(); ++i) {
    if (matchers_[i] != o.matchers_[i]) return false;
  }
  return on_no_match_ == o.on_no_match_;
}

std::string XdsMatcherList::ToString() const {
  std::vector<std::string> parts;
  for (const auto& matcher : matchers_) {
    parts.push_back(matcher.ToString());
  }
  if (on_no_match_.has_value()) {
    parts.push_back(absl::StrCat("on_no_match=", on_no_match_->ToString()));
  }
  return absl::StrCat("XdsMatcherList{", absl::StrJoin(parts, ", "), "}");
}

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

bool XdsMatcherList::AndPredicate::Equals(const Predicate& other) const {
  if (type() != other.type()) return false;
  const auto& o = DownCast<const AndPredicate&>(other);
  if (predicates_.size() != o.predicates_.size()) return false;
  for (size_t i = 0; i < predicates_.size(); ++i) {
    if (!predicates_[i]->Equals(*o.predicates_[i])) return false;
  }
  return true;
}

std::string XdsMatcherList::AndPredicate::ToString() const {
  std::vector<std::string> parts;
  for (const auto& predicate : predicates_) {
    parts.push_back(predicate->ToString());
  }
  return absl::StrCat("And{", absl::StrJoin(parts, ", "), "}");
}

bool XdsMatcherList::AndPredicate::Match(
    const XdsMatcher::MatchContext& context) const {
  for (const auto& predicate : predicates_) {
    if (!predicate->Match(context)) return false;
  }
  return true;
}

bool XdsMatcherList::OrPredicate::Equals(const Predicate& other) const {
  if (type() != other.type()) return false;
  const auto& o = DownCast<const OrPredicate&>(other);
  if (predicates_.size() != o.predicates_.size()) return false;
  for (size_t i = 0; i < predicates_.size(); ++i) {
    if (!predicates_[i]->Equals(*o.predicates_[i])) return false;
  }
  return true;
}

std::string XdsMatcherList::OrPredicate::ToString() const {
  std::vector<std::string> parts;
  for (const auto& predicate : predicates_) {
    parts.push_back(predicate->ToString());
  }
  return absl::StrCat("Or{", absl::StrJoin(parts, ", "), "}");
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

bool XdsMatcherExactMap::Equals(const XdsMatcher& other) const {
  if (type() != other.type()) return false;
  const auto& o = DownCast<const XdsMatcherExactMap&>(other);
  if (!input_->Equals(*o.input_)) return false;
  if (map_ != o.map_) return false;
  return on_no_match_ == o.on_no_match_;
}

std::string XdsMatcherExactMap::ToString() const {
  std::vector<std::string> map_parts;
  for (const auto& pair : map_) {
    map_parts.push_back(
        absl::StrCat("{\"", pair.first, "\": ", pair.second.ToString(), "}"));
  }
  std::sort(map_parts.begin(), map_parts.end());
  std::vector<std::string> parts;
  parts.push_back(absl::StrCat("input=", input_->ToString()));
  parts.push_back(absl::StrCat("map={", absl::StrJoin(map_parts, ", "), "}"));
  if (on_no_match_.has_value()) {
    parts.push_back(absl::StrCat("on_no_match=", on_no_match_->ToString()));
  }
  return absl::StrCat("XdsMatcherExactMap{", absl::StrJoin(parts, ", "), "}");
}

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

XdsMatcherPrefixMap::XdsMatcherPrefixMap(
    std::unique_ptr<InputValue<absl::string_view>> input,
    absl::flat_hash_map<std::string, XdsMatcher::OnMatch> map,
    std::optional<OnMatch> on_no_match)
    : input_(std::move(input)), on_no_match_(std::move(on_no_match)) {
  for (auto& [key, value] : map) {
    root_.AddNode(key, std::move(value));
  }
}

bool XdsMatcherPrefixMap::Equals(const XdsMatcher& other) const {
  if (type() != other.type()) return false;
  const auto& o = DownCast<const XdsMatcherPrefixMap&>(other);
  if (!input_->Equals(*o.input_)) return false;
  if (root_ != o.root_) return false;
  return on_no_match_ == o.on_no_match_;
}

std::string XdsMatcherPrefixMap::ToString() const {
  std::vector<std::string> map_parts;
  root_.ForEach([&](absl::string_view key, const OnMatch& value) {
    map_parts.push_back(
        absl::StrCat("{\"", key, "\": ", value.ToString(), "}"));
  });
  std::sort(map_parts.begin(), map_parts.end());
  std::vector<std::string> parts;
  parts.push_back(absl::StrCat("input=", input_->ToString()));
  parts.push_back(absl::StrCat("map={", absl::StrJoin(map_parts, ", "), "}"));
  if (on_no_match_.has_value()) {
    parts.push_back(absl::StrCat("on_no_match=", on_no_match_->ToString()));
  }
  return absl::StrCat("XdsMatcherPrefixMap{", absl::StrJoin(parts, ", "), "}");
}

bool XdsMatcherPrefixMap::FindMatches(const MatchContext& context,
                                      Result& result) const {
  auto input = input_->GetValue(context);
  std::vector<const OnMatch*> on_match_results;
  root_.ForEachPrefixMatch(input.value_or(""), [&](const OnMatch& on_match) {
    if (!on_match.keep_matching) {
      // Don't need previous entries if we can use this one.
      on_match_results.clear();
    }
    on_match_results.push_back(&on_match);
  });
  for (auto it = on_match_results.rbegin(); it != on_match_results.rend();
       ++it) {
    if ((*it)->FindMatches(context, result)) {
      return true;
    }
  }
  if (on_no_match_.has_value()) {
    if (on_no_match_->FindMatches(context, result)) return true;
  }
  return false;
}

}  // namespace grpc_core
