
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

#ifndef GRPC_CORE_LIB_SECURITY_AUTHORIZATION_MATCHERS_H
#define GRPC_CORE_LIB_SECURITY_AUTHORIZATION_MATCHERS_H

#include <memory>
#include <string>

#include "absl/strings/string_view.h"
#include "absl/types/optional.h"

#include "re2/re2.h"

namespace grpc_core {

class StringMatcher {
 public:
  enum class StringMatcherType {
    UNKNOWN,
    EXACT,       // value stored in string_matcher_ field
    PREFIX,      // value stored in string_matcher_ field
    SUFFIX,      // value stored in string_matcher_ field
    SAFE_REGEX,  // pattern stored in regex_matcher_ field
    CONTAINS,    // value stored in string_matcher_ field
  };

  StringMatcher() = default;
  StringMatcher(const StringMatcher& other);
  StringMatcher(StringMatcherType type, const std::string& matcher,
                bool ignore_case = false);
  StringMatcher& operator=(const StringMatcher& other);
  bool operator==(const StringMatcher& other) const;

  bool Match(absl::string_view value) const;

  std::string ToString() const;

  StringMatcherType type() const { return type_; }

  // Valid for EXACT, PREFIX, SUFFIX and CONTAINS
  const std::string& string_matcher() const { return string_matcher_; }

  // Valid for SAFE_REGEX
  RE2* regex_matcher() const { return regex_matcher_.get(); }

 private:
  StringMatcherType type_ = StringMatcherType::EXACT;
  std::string string_matcher_;
  std::unique_ptr<RE2> regex_matcher_;
  bool ignore_case_ = false;
};

class HeaderMatcher {
 public:
  enum class HeaderMatcherType {
    EXACT,       // value stored in StringMatcher field
    SAFE_REGEX,  // uses regex_match field
    RANGE,       // uses range_start and range_end fields
    PRESENT,     // uses present_match field
    PREFIX,      // prefix stored in StringMatcher field
    SUFFIX,      // suffix stored in StringMatcher field
    CONTAINS,    // contains stored in StringMatcher field
  };

  HeaderMatcher() = default;
  HeaderMatcher(const HeaderMatcher& other);
  HeaderMatcher(HeaderMatcherType type, const std::string& name,
                const std::string& matcher);
  /*
  HeaderMatcher(const std::string& name, int range_start, int range_end,
                bool invert_match);
  HeaderMatcher(const std::string& name, bool present_match, bool
  invert_match);*/

  HeaderMatcher& operator=(const HeaderMatcher& other);
  bool operator==(const HeaderMatcher& other) const;

  bool Match(const absl::optional<absl::string_view>& value) const;

  std::string ToString() const;

  HeaderMatcherType type() const { return type_; }

  const std::string& name() const { return name_; }

  // Valid for EXACT, PREFIX, SUFFIX and CONTAINS.
  const std::string& string_matcher() const {
    return matcher_->string_matcher();
  }

  // Valid for SAFE_REGEX.
  RE2* regex_matcher() const { return matcher_->regex_matcher(); }

  // Setters for HeaderMatcher fields.
  void SetName(const std::string& name);

  void SetStringMatch(HeaderMatcherType type, const std::string& matcher,
                      bool ignore_case);

  void SetRangeMatch(int range_start, int range_end);

  void SetPresentMatch();

  void SetInvertMatch(bool invert_match);

 private:
  std::string name_;
  HeaderMatcherType type_ = HeaderMatcherType::EXACT;
  std::unique_ptr<StringMatcher> matcher_;
  int64_t range_start_;
  int64_t range_end_;
  // invert_match field may or may not exist, so initialize it to false.
  bool invert_match_ = false;
};

}  // namespace grpc_core

#endif /* GRPC_CORE_LIB_SECURITY_AUTHORIZATION_MATCHERS_H */