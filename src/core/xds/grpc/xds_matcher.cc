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

bool XdsMatcher::OnMatch::Equal(const OnMatch& other) const {
  if (keep_matching != other.keep_matching) return false;
  if (action.index() != other.action.index()) return false;
  return Match(
      action,
      [&](const std::shared_ptr<Action>& action) {
        const auto& other_action =
            *std::get_if<std::shared_ptr<Action>>(&other.action);
        if (action == nullptr) return other_action == nullptr;
        if (other_action == nullptr) return false;
        return action->Equal(*other_action);
      },
      [&](const std::shared_ptr<XdsMatcher>& matcher) {
        const auto& other_matcher =
            *std::get_if<std::shared_ptr<XdsMatcher>>(&other.action);
        if (matcher == nullptr) return other_matcher == nullptr;
        if (other_matcher == nullptr) return false;
        return matcher->Equal(*other_matcher);
      });
}

std::string XdsMatcher::OnMatch::ToString() const {
  std::vector<std::string> parts;
  parts.push_back(Match(
      action,
      [](const std::shared_ptr<Action>& action) {
        return absl::StrCat("action_type_url=", action->type_url());
      },
      [](const std::shared_ptr<XdsMatcher>& matcher) {
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
      [&](const std::shared_ptr<Action>& action) {
        result.push_back(action.get());
        return !keep_matching;
      },
      [&](const std::shared_ptr<XdsMatcher>& matcher) {
        return matcher->FindMatches(context, result) && !keep_matching;
      });
}

//
// XdsMatcherList
//

bool XdsMatcherList::Equal(const XdsMatcher& other) const {
  const auto* o = dynamic_cast<const XdsMatcherList*>(&other);
  if (o == nullptr) return false;
  if (matchers_.size() != o->matchers_.size()) return false;
  for (size_t i = 0; i < matchers_.size(); ++i) {
    if (!matchers_[i].predicate->Equal(*o->matchers_[i].predicate)) return false;
    if (!matchers_[i].on_match.Equal(o->matchers_[i].on_match)) return false;
  }
  if (on_no_match_.has_value() != o->on_no_match_.has_value()) return false;
  if (on_no_match_.has_value()) {
    if (!on_no_match_->Equal(*o->on_no_match_)) return false;
  }
  return true;
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

bool XdsMatcherList::AndPredicate::Equal(const Predicate& other) const {
  const auto* o = dynamic_cast<const AndPredicate*>(&other);
  if (o == nullptr) return false;
  if (predicates_.size() != o->predicates_.size()) return false;
  for (size_t i = 0; i < predicates_.size(); ++i) {
    if (!predicates_[i]->Equal(*o->predicates_[i])) return false;
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

bool XdsMatcherList::OrPredicate::Equal(const Predicate& other) const {
  const auto* o = dynamic_cast<const OrPredicate*>(&other);
  if (o == nullptr) return false;
  if (predicates_.size() != o->predicates_.size()) return false;
  for (size_t i = 0; i < predicates_.size(); ++i) {
    if (!predicates_[i]->Equal(*o->predicates_[i])) return false;
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

bool XdsMatcherExactMap::Equal(const XdsMatcher& other) const {
  const auto* o = dynamic_cast<const XdsMatcherExactMap*>(&other);
  if (o == nullptr) return false;
  if (!input_->Equal(*o->input_)) return false;
  if (map_.size() != o->map_.size()) return false;
  for (const auto& pair : map_) {
    auto it = o->map_.find(pair.first);
    if (it == o->map_.end()) return false;
    if (!pair.second.Equal(it->second)) return false;
  }
  if (on_no_match_.has_value() != o->on_no_match_.has_value()) return false;
  if (on_no_match_.has_value()) {
    if (!on_no_match_->Equal(*o->on_no_match_)) return false;
  }
  return true;
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

bool XdsMatcherPrefixMap::Equal(const XdsMatcher& other) const {
  const auto* o = dynamic_cast<const XdsMatcherPrefixMap*>(&other);
  if (o == nullptr) return false;
  if (!input_->Equal(*o->input_)) return false;
  if (map_.size() != o->map_.size()) return false;
  for (const auto& pair : map_) {
    auto it = o->map_.find(pair.first);
    if (it == o->map_.end()) return false;
    if (!pair.second.Equal(it->second)) return false;
  }
  if (on_no_match_.has_value() != o->on_no_match_.has_value()) return false;
  if (on_no_match_.has_value()) {
    if (!on_no_match_->Equal(*o->on_no_match_)) return false;
  }
  return true;
}

std::string XdsMatcherPrefixMap::ToString() const {
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
  return absl::StrCat("XdsMatcherPrefixMap{", absl::StrJoin(parts, ", "), "}");
}

void XdsMatcherPrefixMap::PopulateTrie(
    absl::flat_hash_map<std::string, XdsMatcher::OnMatch> map) {
  for (auto& [key, value] : map) {
    root_.AddNode(key, std::move(value));
  }
}

bool XdsMatcherPrefixMap::FindMatches(const MatchContext& context,
                                      Result& result) const {
  auto input = input_->GetValue(context);
  auto value = root_.GetAllPrefixMatches(input.value_or(""));
  if (!value.empty()) {
    // Reverse iterate the matches from more specific to less.
    for (auto it = value.rbegin(); it != value.rend(); ++it) {
      // if keep_matching is set FindMatches will return false but Action would
      // be added to result. In this case loop would continue.
      if ((*it)->FindMatches(context, result)) return true;      
    }
  }
  if (on_no_match_.has_value()) {
    if (on_no_match_->FindMatches(context, result)) return true;
  }
  return false;
}

}  // namespace grpc_core
