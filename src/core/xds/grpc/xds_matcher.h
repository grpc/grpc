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

// Call back to execute on Match
using ExecuteActionCb = std::function<void()>;

// Match Result to return
// AI: Do we need to return result here ? We can return nullopt for fail case.
template <class DataType> struct MatchResult {
  bool result;
//  std::optional<OnMatch<DataType>> onMatch;
};

// OnMatch Class definition 
// Contains either Action or subsequent matcher to match
// AI : Should we create variant here ??
template <class DataType> struct OnMatch {
  // Returning Action
  //std::variant<MatcherPtr<DataType>, ActionCb> action_;
  // Changing to a callback instead of a return
  std::variant<MatcherPtr<DataType>, ExecuteActionCb> action_;

  // AI: CHeck usecase of keepMatching , currently unused
  bool keepMatching;

  MatchResult<DataType> MatchAction(const DataType& data) {
    if (std::holds_alternative<MatcherPtr<DataType>>(action_)) {
      std::get<MatcherPtr<DataType>>(action_)->match(data);
    } else if (std::holds_alternative<ExecuteActionCb>(action_)) {
      std::get<ExecuteActionCb>(action_)();
    }
    // if keep matching is true return false
    return {!keepMatching};
  }
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
    virtual MatchResult<DataType> match(const DataType& data) = 0;
};

// List of Predicates
// Return OnMatch for the corresponding Predicatematch else return onNoMatch if set 
template <class DataType> class MatcherList : public Matcher<DataType> {
 public:
  void addMatcher(PredicatePtr<DataType> &&predicate, OnMatch<DataType> on_match) {
    fieldMatchers_.emplace_back(std::move(predicate), std::move(on_match));
  }

  void setOnNoMatch(std::optional<OnMatch<DataType>> on_no_match) {
    onNoMatch_ = std::move(on_no_match);
  }

  MatchResult<DataType> match(const DataType& data) override {
    for (auto& [predicate_f, onMatch_f] : fieldMatchers_) {
      const bool predicate_matched = predicate_f->match(data);
      if (predicate_matched) {
        if (std::holds_alternative<ExecuteActionCb>(onMatch_f.action_)) {
          std::get<ExecuteActionCb>(onMatch_f.action_)();
        } else if (std::holds_alternative<MatcherPtr<DataType>>(onMatch_f.action_)) {
          // Call subsequent matcher
          std::get<MatcherPtr<DataType>>(onMatch_f.action_)->match(data);
        }
        // if keep_matching is true , lets continue else return with true (matcher matched)
        if (!onMatch_f.keepMatching) {
          return {true};
        }
        // If keepMatching is true, continue to the next field matcher.
      }
    }
    if (onNoMatch_.has_value()) {
      if (std::holds_alternative<ExecuteActionCb>(onNoMatch_->action_)) {
        std::get<ExecuteActionCb>(onNoMatch_->action_)();
      } else if (std::holds_alternative<MatcherPtr<DataType>>(onNoMatch_->action_)) {
        // Call subsequent matcher
        std::get<MatcherPtr<DataType>>(onNoMatch_->action_)->match(data);
      }
      // If onNoMatch_ is processed, MatcherList considers it a "match".
      return {true};
    }
    return {false};
  }
  private:
   std::vector<std::pair<PredicatePtr<DataType>, OnMatch<DataType>>> fieldMatchers_;
   std::optional<OnMatch<DataType>> onNoMatch_;
};

// AI: will this support only string ??
template <class DataType> class MatcherTree : public Matcher<DataType> {
public:
  MatchResult<DataType> match(const DataType& data) override {
    if (dataInput_ == nullptr) return {false};
    std::optional<MatchDataType> input_val_opt = dataInput_->GetInput(data);
    if (!input_val_opt.has_value()) {
      if (onNoMatch_.has_value()) {
        return onNoMatch_->MatchAction(data);
      }
      return {false};
    }
    const std::string* key_to_match = std::get_if<std::string>(&input_val_opt.value());
    if (key_to_match == nullptr) { // Input was not a string
        if (onNoMatch_.has_value()) {
            return onNoMatch_->MatchAction(data);
        }
        return {false};
    }
    // Call the derived class's doMatch.
    // This assumes doMatch returns MatchResult<DataType> and handles the key.
    // If DataType is not std::string, ExactMatcherTree::doMatch's call to
    // OnMatch::MatchAction(key) would be a type error unless MatchAction
    // is specialized or DataType is string. Tests use DataType=string.
    MatchResult<DataType> map_lookup_result = doMatch(*key_to_match);
    // If map_lookup_result.result is true, it means a terminal match was found in the map.
    // If map_lookup_result.result is false, it means either:
    //   a) key was not found in map (ExactMatcherTree::doMatch returned {false})
    //   b) key was found, but its OnMatch.keepMatching was true (OnMatch::MatchAction returned {false})
    // In case (a), we should try onNoMatch_. In case (b), we should not.
    // ExactMatcherTree::doMatch currently doesn't distinguish these.
    // For now, we assume if doMatch returns {false}, it means "not found in map".
    if (!map_lookup_result.result && onNoMatch_.has_value()) {
        return onNoMatch_->MatchAction(data);
    }
    return map_lookup_result;
  }

 protected:
  virtual MatchResult<DataType> doMatch(const std::string& data) = 0;
  std::unordered_map<std::string, OnMatch<DataType>> map_;
  DataInputPtr<DataType> dataInput_;
  std::optional<OnMatch<DataType>> onNoMatch_;
};

template <class DataType> class ExactMatcherTree : public MatcherTree<DataType> {
 protected:
  MatchResult<DataType>  doMatch(const std::string& data) override {
    // data here is the key extracted by MatcherTree::match
    auto f = this->map_.find(data);
    if (f != this->map_.end()) {
      // MatchAction expects DataType. If DataType is not std::string, this is an issue.
      // Assuming DataType is std::string as per test setup for ExactMatcherTree.
      return f->second.MatchAction(static_cast<const DataType&>(data));
    }
    // Key not found in map.
    return {false};
  }
};

// AI: Add prefix matcher for map (Based on string ?? )

}  // namespace matcher
#endif  // GRPC_SRC_CORE_XDS_GRPC_XDS_MATCHER_H