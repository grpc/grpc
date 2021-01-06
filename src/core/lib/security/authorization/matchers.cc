// Copyright 2020 gRPC authors.
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

#include "absl/memory/memory.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/strings/str_join.h"
#include "absl/strings/str_split.h"

namespace grpc_core {

namespace {

StringMatcher::StringMatcherType GetStringMatcherType(
    HeaderMatcher::HeaderMatcherType type) {
  switch (type) {
    case HeaderMatcher::HeaderMatcherType::EXACT:
      return StringMatcher::StringMatcherType::EXACT;
    case HeaderMatcher::HeaderMatcherType::PREFIX:
      return StringMatcher::StringMatcherType::PREFIX;
    case HeaderMatcher::HeaderMatcherType::SUFFIX:
      return StringMatcher::StringMatcherType::SUFFIX;
    case HeaderMatcher::HeaderMatcherType::CONTAINS:
      return StringMatcher::StringMatcherType::CONTAINS;
    case HeaderMatcher::HeaderMatcherType::SAFE_REGEX:
      return StringMatcher::StringMatcherType::SAFE_REGEX;
    default:
      return StringMatcher::StringMatcherType::UNKNOWN;
  }
}

}  // namespace

//
// StringMatcher
//

StringMatcher::StringMatcher(StringMatcherType type, const std::string& matcher,
                             bool ignore_case)
    : type_(type), ignore_case_(ignore_case) {
  if (type_ == StringMatcherType::SAFE_REGEX) {
    regex_matcher_ = absl::make_unique<RE2>(matcher);
  } else {
    string_matcher_ = matcher;
  }
}

StringMatcher::StringMatcher(const StringMatcher& other)
    : type_(other.type_), ignore_case_(other.ignore_case_) {
  switch (type_) {
    case StringMatcherType::SAFE_REGEX:
      regex_matcher_ = absl::make_unique<RE2>(other.regex_matcher_->pattern());
      break;
    default:
      string_matcher_ = other.string_matcher_;
  }
}

StringMatcher& StringMatcher::operator=(const StringMatcher& other) {
  type_ = other.type_;
  switch (type_) {
    case StringMatcherType::SAFE_REGEX:
      regex_matcher_ = absl::make_unique<RE2>(other.regex_matcher_->pattern());
      break;
    default:
      string_matcher_ = other.string_matcher_;
  }
  ignore_case_ = other.ignore_case_;
  return *this;
}

bool StringMatcher::operator==(const StringMatcher& other) const {
  if (type_ != other.type_ || ignore_case_ != other.ignore_case_) return false;
  switch (type_) {
    case StringMatcherType::SAFE_REGEX:
      return regex_matcher_->pattern() == other.regex_matcher_->pattern();
    default:
      return string_matcher_ == other.string_matcher_;
  }
}

bool StringMatcher::Match(absl::string_view value) const {
  switch (type_) {
    case StringMatcherType::EXACT:
      return ignore_case_ ? absl::EqualsIgnoreCase(value, string_matcher_)
                          : value == string_matcher_;
    case StringMatcher::StringMatcherType::PREFIX:
      return ignore_case_ ? absl::StartsWithIgnoreCase(value, string_matcher_)
                          : absl::StartsWith(value, string_matcher_);
    case StringMatcher::StringMatcherType::SUFFIX:
      return ignore_case_ ? absl::EndsWithIgnoreCase(value, string_matcher_)
                          : absl::EndsWith(value, string_matcher_);
    case StringMatcher::StringMatcherType::CONTAINS:
      return ignore_case_
                 ? absl::StrContains(absl::AsciiStrToLower(value),
                                     absl::AsciiStrToLower(string_matcher_))
                 : absl::StrContains(value, string_matcher_);
    case StringMatcher::StringMatcherType::SAFE_REGEX:
      // ignore_case_ is ignored for SAFE_REGEX
      return RE2::FullMatch(std::string(value), *regex_matcher_);
    default:
      return false;
  }
}

std::string StringMatcher::ToString() const {
  switch (type_) {
    case StringMatcherType::EXACT:
      return absl::StrFormat("StringMatcher{exact=%s%s}", string_matcher_,
                             ignore_case_ ? ", ignore_case" : "");
    case StringMatcherType::PREFIX:
      return absl::StrFormat("StringMatcher{prefix=%s%s}", string_matcher_,
                             ignore_case_ ? ", ignore_case" : "");
    case StringMatcherType::SUFFIX:
      return absl::StrFormat("StringMatcher{suffix=%s%s}", string_matcher_,
                             ignore_case_ ? ", ignore_case" : "");
    case StringMatcherType::CONTAINS:
      return absl::StrFormat("StringMatcher{contains=%s%s}", string_matcher_,
                             ignore_case_ ? ", ignore_case" : "");
    case StringMatcherType::SAFE_REGEX:
      return absl::StrFormat("StringMatcher{safe_regex=%s}",
                             regex_matcher_->pattern());
    default:
      return "";
  }
}

//
// HeaderMatcher
//

HeaderMatcher::HeaderMatcher(HeaderMatcherType type, const std::string& name,
                             const std::string& matcher)
    : type_(type), name_(name) {
  matcher_ = absl::make_unique<StringMatcher>(GetStringMatcherType(type),
                                              matcher, /*ignore_case=*/false);
}

HeaderMatcher::HeaderMatcher(const HeaderMatcher& other)
    : name_(other.name_),
      type_(other.type_),
      invert_match_(other.invert_match_) {
  switch (type_) {
    case HeaderMatcherType::RANGE:
      range_start_ = other.range_start_;
      range_end_ = other.range_end_;
      break;
    default:
      matcher_ = absl::make_unique<StringMatcher>(*other.matcher_);
  }
}

HeaderMatcher& HeaderMatcher::operator=(const HeaderMatcher& other) {
  name_ = other.name_;
  type_ = other.type_;
  invert_match_ = other.invert_match_;
  switch (type_) {
    case HeaderMatcherType::RANGE:
      range_start_ = other.range_start_;
      range_end_ = other.range_end_;
      break;
    default:
      matcher_ = absl::make_unique<StringMatcher>(*other.matcher_);
  }
  return *this;
}

bool HeaderMatcher::operator==(const HeaderMatcher& other) const {
  if (name_ != other.name_) return false;
  if (type_ != other.type_) return false;
  if (invert_match_ != other.invert_match_) return false;
  switch (type_) {
    case HeaderMatcherType::RANGE:
      return range_start_ == other.range_start_ &&
             range_end_ == other.range_end_;
    default:
      return matcher_ == other.matcher_;
  }
}

void HeaderMatcher::SetName(const std::string& name) { name_ = name; }

void HeaderMatcher::SetStringMatch(HeaderMatcherType type,
                                   const std::string& matcher,
                                   bool ignore_case) {
  type_ = type;
  matcher_ = absl::make_unique<StringMatcher>(GetStringMatcherType(type),
                                              matcher, ignore_case);
}

void HeaderMatcher::SetRangeMatch(int range_start, int range_end) {
  type_ = HeaderMatcherType::RANGE;
  range_start_ = range_start;
  range_end_ = range_end;
}

void HeaderMatcher::SetPresentMatch() { type_ = HeaderMatcherType::PRESENT; }

void HeaderMatcher::SetInvertMatch(bool invert_match) {
  invert_match_ = invert_match;
}

bool HeaderMatcher::Match(
    const absl::optional<absl::string_view>& value) const {
  if (!value.has_value()) {
    return invert_match_ && type_ == HeaderMatcherType::PRESENT;
  }
  bool match;
  switch (type_) {
    case HeaderMatcherType::RANGE:
      int64_t int_value;
      match = absl::SimpleAtoi(value.value(), &int_value) &&
              int_value >= range_start_ && int_value < range_end_;
      break;
    case HeaderMatcherType::PRESENT:
      match = true;
      break;
    case HeaderMatcherType::EXACT:
    case HeaderMatcherType::PREFIX:
    case HeaderMatcherType::SUFFIX:
    case HeaderMatcherType::CONTAINS:
    case HeaderMatcherType::SAFE_REGEX:
      match = matcher_->Match(value.value());
      break;
    default:
      // LOG;
      match = false;
  }
  return match != invert_match_;
}

std::string HeaderMatcher::ToString() const {
  switch (type_) {
    case HeaderMatcherType::EXACT:
      return absl::StrFormat("HeaderMatcher{exact=%s %s:%s}",
                             invert_match_ ? " not" : "", name_,
                             matcher_->string_matcher());
    case HeaderMatcherType::SAFE_REGEX:
      return absl::StrFormat("HeaderMatcher{safe_regex=%s %s:%s}",
                             invert_match_ ? " not" : "", name_,
                             matcher_->regex_matcher()->pattern());
    case HeaderMatcherType::RANGE:
      return absl::StrFormat("HeaderMatcher{range=%s %s:[%d, %d]}",
                             invert_match_ ? " not" : "", name_, range_start_,
                             range_end_);
    case HeaderMatcherType::PRESENT:
      return absl::StrFormat("HeaderMatcher{present=%s %s}",
                             invert_match_ ? " not" : "", name_);
    case HeaderMatcherType::PREFIX:
      return absl::StrFormat("HeaderMatcher{suffix=%s %s:%s}",
                             invert_match_ ? " not" : "", name_,
                             matcher_->string_matcher());
    case HeaderMatcherType::SUFFIX:
      return absl::StrFormat("HeaderMatcher{suffix=%s %s:%s}",
                             invert_match_ ? " not" : "", name_,
                             matcher_->string_matcher());
    case HeaderMatcherType::CONTAINS:
      return absl::StrFormat("HeaderMatcher{contains=%s %s:%s}",
                             invert_match_ ? " not" : "", name_,
                             matcher_->string_matcher());
    default:
      return "";
  }
}

}  // namespace grpc_core
