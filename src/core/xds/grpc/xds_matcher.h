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

#ifndef GRPC_SRC_CORE_XDS_GRPC_XDS_MATCHER_H
#define GRPC_SRC_CORE_XDS_GRPC_XDS_MATCHER_H



#include <functional>
#include <memory>
#include <optional>
#include <unordered_map>
#include <variant>
#include <string>
#include <vector>

#include "xds/type/matcher/v3/matcher.upb.h"

namespace matcher {

// Class For custom Match Data (Example CEL)
class CustomMatchData {
  public:
    virtual ~CustomMatchData() = default;
};

// Type of data to match
// std::monostate -> No data (empty)
using MatchDataType = std::variant<std::monostate, std::string, std::shared_ptr<CustomMatchData>>;

// Forward declaration of Matcher Class
template <class DataType> class Matcher;
template <class DataType> using MatcherPtr = std::unique_ptr<Matcher<DataType>>;

// Action Class definition
class Action {
 public:
  virtual ~Action() = default;
  virtual std::string_view typeUrl() = 0;
};
using ActionPtr = std::unique_ptr<Action>;
using ActionCb = std::function<ActionPtr()>;

// OnMatch Class definition 
// Contains either Action or subsequent matcher to match
// AI : Should we create variant here ??
template <class DataType> struct OnMatch {
  ActionCb actionCb_;
  MatcherPtr<DataType> matcher_;
  // AI: CHeck usecase of keepMatching , currently unused
  bool keepMatching;
};

// Match Result to return
// AI: Do we need to return result here ? We can return nullopt for fail case.
template <class DataType> struct MatchResult {
  bool result;
  std::optional<OnMatch<DataType>> onMatch;
};


// Predicate
template <class DataType> class Predicate {
 public:
  virtual ~Predicate() = default;
  virtual bool match(const DataType& data) = 0;
};
template <class DataType> using PredicatePtr = std::unique_ptr<Predicate<DataType>>;

// AndMatcher : Return true if all the predicates return true else false
template <class DataType> class AndMatcher : public Predicate<DataType> {
 public:
  explicit AndMatcher(std::vector<PredicatePtr<DataType>>&& matcherList)
      : matcherList_(std::move(matcherList)) {}
  bool match(const DataType& data) {
    for (const auto& matcher : matcherList_) {
      bool result = matcher->match(data);
      if (!result) {
        return false;
      }
    }
    return true;
  }
 private:
  std::vector<PredicatePtr<DataType>> matcherList_;
};

// OrMatcher : Return true if any of the predicates return true else false
template <class DataType> class OrMatcher : public Predicate<DataType> {
 public:
  explicit OrMatcher(std::vector<PredicatePtr<DataType>>&& matcherList)
      : matcherList_(std::move(matcherList)) {}
  bool match(const DataType& data) {
    for (const auto& matcher : matcherList_) {
      bool result = matcher->match(data);
      if (result) {
        return true;
      }
    }
    return false;
  }
 private:
  std::vector<PredicatePtr<DataType>> matcherList_;
};

// Input matcher class to perform match (eg: StringMatcher ..) 
class InputMatcher {
 public:
  virtual ~InputMatcher() = default;
  virtual bool match(const MatchDataType& input) = 0;
};
using InputMatcherPtr = std::unique_ptr<InputMatcher>;

// Class to extract data from the input
// core.v3.TypedExtensionConfig
template <class DataType> class DataInput {
 public:
  virtual ~DataInput() = default;
  // Interface to extract a string from DataType for matching.
  // Returns std::nullopt if the string is not available or not applicable.
  virtual std::optional<MatchDataType> GetInput(const DataType& data) = 0;
  virtual std::string_view typeUrl() = 0;
};
template <class DataType> using DataInputPtr = std::unique_ptr<DataInput<DataType>>;


// Matching of single predicate
template <class DataType>
class SinglePredicate : public Predicate<DataType> {
 public:
  explicit SinglePredicate(DataInputPtr<DataType>&& data_input, InputMatcherPtr&& input_matcher)
      : data_input_(std::move(data_input)), input_matcher_(std::move(input_matcher)) {}

  bool match(const DataType& data) override {
    if (!data_input_ || !input_matcher_) {
      // Or handle as an error, depending on desired behavior
      return false; 
    }
    std::optional<MatchDataType> input_val = data_input_->GetInput(data);
    if (!input_val.has_value()) {
      return false; // Or false, if not finding input means no match
    }
    return input_matcher_->match(input_val.value());
  }

 private:
 // AI: do we need to have a correlation b/w data_input_ and input_matcher ?
 // As input_matcher may not support the type returned from data_input
  DataInputPtr<DataType> data_input_;
  InputMatcherPtr input_matcher_;
};

// Base Matcher class
template <class DataType> class Matcher {
  public:
    virtual ~Matcher() = default;
    MatchResult<DataType> match(const DataType& data) = 0;
};

// List of Predicates
// Return OnMatch for the corresponding Predicatematch else return onNoMatch if set 
template <class DataType> class MatcherList : public Matcher<DataType> {
 public:
  void addMatcher(PredicatePtr<DataType> &&predicate, OnMatch<DataType> on_match) {
    fieldMatchers_.emplace_back(std::move(predicate), std::move(on_match));
  }

  MatchResult<DataType> match(const DataType& data) override {
    for (auto& matcher_pair : fieldMatchers_) {
      const bool predicate_matched = matcher_pair.first->match(data);
      if (predicate_matched) {
        return {true, matcher_pair.second};
      }
    }
    // AI: check return values same as MatchResult should we just return the OnMatch (optional can indicate as false)
    return {false, onNoMatch_};
  }
  private:
   std::vector<std::pair<PredicatePtr<DataType>, OnMatch<DataType>>> fieldMatchers_;
   std::optional<OnMatch<DataType>> onNoMatch_;
};

// AI: will this support only string ??
template <class DataType> class MatcherTree : public Matcher<DataType> {
public:
  MatchResult<DataType> match(const DataType& data) {
    auto input_str = dataInput_->GetInput(data);
    if (!input_str.has_value()) {
      return {false, onNoMatch_};
    }
    auto result = doMatch(data);
    if (!result.has_value()) {
      return {false, onNoMatch_};
    }
    if (result.value().matcher_ != nullptr) {
      return result.value().matcher_->match(data);
    }
    return {true, result.value().onMatch_};
  }
 protected:
  virtual std::optional<OnMatch<DataType>> doMatch(const std::string& data) = 0;
  std::unordered_map<std::string, OnMatch<DataType>> map_;
  DataInputPtr<DataType> dataInput_;
  std::optional<OnMatch<DataType>> onNoMatch_;
};

template <class DataType> class ExactMatcherTree : public MatcherTree<DataType> {
 protected:
  std::optional<OnMatch<DataType>> doMatch(const std::string& data) override {
    auto f = this->map_.find(data);
    if (f != this->map_.end()) {
      return f->second;
    }
    return std::nullopt;
  }
};

// AI: Add prefix matcher for map (Based on string ?? )

}  // namespace matcher
#endif  // GRPC_SRC_CORE_XDS_GRPC_XDS_MATCHER_H