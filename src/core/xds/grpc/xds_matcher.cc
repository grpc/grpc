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
#include "src/core/util/string.h"

namespace grpc_core {

//
// XdsMatcher::OnMatch
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
  std::string result = "{";
  Match(
      action,
      [&](const std::unique_ptr<Action>& action) {
        StrAppend(result, "action=");
        StrAppend(result, action->ToString());
      },
      [&](const std::unique_ptr<XdsMatcher>& matcher) {
        StrAppend(result, "matcher=");
        StrAppend(result, matcher->ToString());
      });
  StrAppend(result, ", keep_matching=");
  StrAppend(result, keep_matching ? "true" : "false");
  StrAppend(result, "}");
  return result;
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

void XdsMatcherList::OnMatch::ForEachAction(
    absl::FunctionRef<void(const Action&)> func) const {
  Match(
      action, [&](const std::unique_ptr<Action>& action) { func(*action); },
      [&](const std::unique_ptr<XdsMatcher>& matcher) {
        matcher->ForEachAction(func);
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
  std::string result = "XdsMatcherList{";
  bool is_first = true;
  for (const auto& matcher : matchers_) {
    if (!is_first) StrAppend(result, ", ");
    StrAppend(result, matcher.ToString());
    is_first = false;
  }
  if (on_no_match_.has_value()) {
    StrAppend(result, ", on_no_match=");
    StrAppend(result, on_no_match_->ToString());
  }
  StrAppend(result, "}");
  return result;
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

void XdsMatcherList::ForEachAction(
    absl::FunctionRef<void(const Action&)> func) const {
  for (const auto& [_, on_match] : matchers_) {
    on_match.ForEachAction(func);
  }
  if (on_no_match_.has_value()) {
    on_no_match_->ForEachAction(func);
  }
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

namespace {

// Code shared between AndPredicate::ToString() and OrPredicate::ToString().
std::string PredicateListToString(
    absl::string_view type,
    const std::vector<std::unique_ptr<XdsMatcherList::Predicate>>& predicates) {
  std::string result;
  StrAppend(result, type);
  StrAppend(result, "{");
  bool is_first = true;
  for (const auto& predicate : predicates) {
    if (!is_first) StrAppend(result, ", ");
    StrAppend(result, predicate->ToString());
    is_first = false;
  }
  StrAppend(result, "}");
  return result;
}

}  // namespace

std::string XdsMatcherList::AndPredicate::ToString() const {
  return PredicateListToString("And", predicates_);
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
  return PredicateListToString("Or", predicates_);
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

namespace {

// Code shared between XdsMatcherExactMap::ToString() and
// XdsMatcherPrefixMap::ToString().
std::string MatcherMapToString(
    absl::string_view type,
    const XdsMatcher::InputValue<absl::string_view>& input,
    std::vector<std::pair<std::string, std::string>> map_entries,
    const std::optional<XdsMatcher::OnMatch>& on_no_match) {
  std::string result;
  StrAppend(result, type);
  StrAppend(result, "{input=");
  StrAppend(result, input.ToString());
  StrAppend(result, ", map={");
  std::sort(map_entries.begin(), map_entries.end());
  bool is_first = true;
  for (const auto& [k, v] : map_entries) {
    StrAppend(result, is_first ? "{\"" : ", {\"");
    StrAppend(result, k);
    StrAppend(result, "\": ");
    StrAppend(result, v);
    StrAppend(result, "}");
    is_first = false;
  }
  StrAppend(result, "}");
  if (on_no_match.has_value()) {
    StrAppend(result, ", on_no_match=");
    StrAppend(result, on_no_match->ToString());
  }
  StrAppend(result, "}");
  return result;
}

}  // namespace

std::string XdsMatcherExactMap::ToString() const {
  // Should be able to use absl::string_view for the key here, but then
  // we'd need to add bloat by templatizing MatcherMapToString().
  std::vector<std::pair<std::string, std::string>> map_entries;
  for (const auto& [k, v] : map_) {
    map_entries.emplace_back(k, v.ToString());
  }
  return MatcherMapToString("XdsMatcherExactMap", *input_,
                            std::move(map_entries), on_no_match_);
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

void XdsMatcherExactMap::ForEachAction(
    absl::FunctionRef<void(const Action&)> func) const {
  for (const auto& [_, on_match] : map_) {
    on_match.ForEachAction(func);
  }
  if (on_no_match_.has_value()) {
    on_no_match_->ForEachAction(func);
  }
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
  std::vector<std::pair<std::string, std::string>> map_entries;
  root_.ForEach([&](absl::string_view key, const OnMatch& value) {
    map_entries.emplace_back(std::string(key), value.ToString());
  });
  return MatcherMapToString("XdsMatcherPrefixMap", *input_,
                            std::move(map_entries), on_no_match_);
}

bool XdsMatcherPrefixMap::FindMatches(const MatchContext& context,
                                      Result& result) const {
  auto input = input_->GetValue(context);
  std::vector<const OnMatch*> on_match_results;
  root_.ForEachPrefixMatch(input.value_or(""), [&](const OnMatch& on_match) {
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

void XdsMatcherPrefixMap::ForEachAction(
    absl::FunctionRef<void(const Action&)> func) const {
  root_.ForEach([&](absl::string_view /*key*/, const OnMatch& on_match) {
    on_match.ForEachAction(func);
  });
  if (on_no_match_.has_value()) {
    on_no_match_->ForEachAction(func);
  }
}

}  // namespace grpc_core
