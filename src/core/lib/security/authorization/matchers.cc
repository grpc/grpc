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

#include <grpc/support/port_platform.h>

#include "src/core/lib/security/authorization/matchers.h"

#include "absl/memory/memory.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/strings/str_join.h"
#include "absl/strings/str_split.h"

namespace grpc_core {

//
// StringMatcher
//

StringMatcher::StringMatcher(StringMatcherType type, const std::string& matcher,
                             bool ignore_case, bool use_ignore_case_in_regex)
    : type_(type), ignore_case_(ignore_case) {
  if (type_ == StringMatcherType::SAFE_REGEX) {
    RE2::Options options;
    if (use_ignore_case_in_regex) {
      // True only for path_specifier.
      options.set_case_sensitive(!ignore_case_);
    }
    regex_matcher_ = absl::make_unique<RE2>(matcher, options);
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
      // ignore_case_ is ignored for SAFE_REGEX except for path_specifier.
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

HeaderMatcher::HeaderMatcher(const std::string& name, HeaderMatcherType type,
                             const std::string& matcher, int64_t range_start,
                             int64_t range_end, bool present_match,
                             bool invert_match)
    : name_(name),
      type_(type),
      range_start_(range_start),
      range_end_(range_end),
      present_match_(present_match),
      invert_match_(invert_match) {
  // Only for EXACT, PREFIX, SUFFIX, SAFE_REGEX and CONTAINS.
  if (static_cast<int>(type_) < 5) {
    matcher_ = absl::make_unique<StringMatcher>(
        static_cast<StringMatcher::StringMatcherType>(type_), matcher,
        /*ignore_case=*/false);
  }
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
    case HeaderMatcherType::PRESENT:
      present_match_ = other.present_match_;
      break;
    default:
      if (other.matcher_ != nullptr) {
        matcher_ = absl::make_unique<StringMatcher>(*other.matcher_);
      }
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
    case HeaderMatcherType::PRESENT:
      present_match_ = other.present_match_;
      break;
    default:
      if (other.matcher_ != nullptr) {
        matcher_ = absl::make_unique<StringMatcher>(*other.matcher_);
      }
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
    case HeaderMatcherType::PRESENT:
      return present_match_ == other.present_match_;
    default:
      return matcher_ == other.matcher_;
  }
}

bool HeaderMatcher::Match(
    const absl::optional<absl::string_view>& value) const {
  bool match;
  if (!value.has_value()) {
    // All types except PRESENT fail to match if field is not present.
    match = (type_ == HeaderMatcherType::PRESENT) ? !present_match_ : false;
  } else if (type_ == HeaderMatcherType::RANGE) {
    int64_t int_value;
    match = absl::SimpleAtoi(value.value(), &int_value) &&
            int_value >= range_start_ && int_value < range_end_;
  } else if (type_ == HeaderMatcherType::PRESENT) {
    match = true;
  } else {
    match = matcher_->Match(value.value());
  }
  return match != invert_match_;
}

std::string HeaderMatcher::ToString() const {
  switch (type_) {
    case HeaderMatcherType::RANGE:
      return absl::StrFormat("HeaderMatcher{%s %srange=[%d, %d]}", name_,
                             invert_match_ ? " not" : "", range_start_,
                             range_end_);
    case HeaderMatcherType::PRESENT:
      return absl::StrFormat("HeaderMatcher{%s %spresent=%s}", name_,
                             invert_match_ ? " not" : "",
                             present_match_ ? "true" : "false");
    case HeaderMatcherType::EXACT:
    case HeaderMatcherType::PREFIX:
    case HeaderMatcherType::SUFFIX:
    case HeaderMatcherType::SAFE_REGEX:
    case HeaderMatcherType::CONTAINS:
      return absl::StrFormat("HeaderMatcher{%s %s%s}", name_,
                             invert_match_ ? " not" : "", matcher_->ToString());
    default:
      return "";
  }
}

}  // namespace grpc_core
