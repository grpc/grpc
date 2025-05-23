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

int parseFunc(const xds_type_matcher_v3_Matcher* match);

using MatchDataType = std::string;

// Forward declaration
template <class DataType>
class MatcherList;

template<class DataType> using MatcherListPtr = std::unique_ptr<MatcherList<DataType>>;


class Action {
 public:
  virtual ~Action() = default;
};

using ActionPtr = std::unique_ptr<Action>;
using ActionCb = std::function<ActionPtr()>;

template <class DataType>
struct OnMatch {
  ActionCb actionCb_;
  MatcherListPtr<DataType> matcher_;
  bool keepMatching;
};

using MatchResult = bool;
/*
template <class DataType>
struct MatchResult {
  std::optional<bool> result;
  std::optional<OnMatch<DataType>> onMatch;
};
*/

template <class DataType>
class Predicate {
 public:
  virtual ~Predicate() = default;
  virtual bool match(const DataType& data) = 0;
};

template <class DataType>
using PredicatePtr = std::unique_ptr<Predicate<DataType>>;

template <class DataType>
class AndMatcher : public Predicate<DataType> {
 public:
  explicit AndMatcher(std::vector<PredicatePtr<DataType>>&& matcherList)
      : matcherList_(std::move(matcherList)) {}
  bool match(const DataType& data) {
    for (const auto& matcher : matcherList_) {
      auto result = matcher->match(data);
      if (!result.result.has_value() || !result.result.value()) {
        return result;
      }
    }
    return true;
  }

 private:
  std::vector<PredicatePtr<DataType>> matcherList_;
};

template <class DataType>
class AnyMatcher : public Predicate<DataType> {
 public:
  explicit AnyMatcher(std::vector<PredicatePtr<DataType>>&& matcherList)
      : matcherList_(std::move(matcherList)) {}
  bool match(const DataType& data) {
    for (const auto& matcher : matcherList_) {
      auto result = matcher->match(data);
      if (result.result.has_value() && result.result.value()) {
        return result;
      }
    }
    return false;
  }

 private:
  std::vector<PredicatePtr<DataType>> matcherList_;
};

class InputMatcher {
 public:
  virtual ~InputMatcher() = default;
  virtual bool match(const MatchDataType& input) = 0;
};
using InputMatcherPtr = std::unique_ptr<InputMatcher>;
using InputMatcherFactoryCb = std::function<InputMatcherPtr()>;

template <class DataType>
class DataInput {
 public:
  virtual ~DataInput() = default;
  // Interface to extract a string from DataType for matching.
  // Returns std::nullopt if the string is not available or not applicable.
  virtual std::optional<MatchDataType> GetInput(const DataType& data) = 0;
};
template <class DataType> using DataInputPtr = std::unique_ptr<DataInput<DataType>>;


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
    std::optional<MatchDataType> input_str = data_input_->GetInput(data);
    if (!input_str.has_value()) {
      return false; // Or false, if not finding input means no match
    }
    return input_matcher_->match(input_str.value());
  }

 private:
  DataInputPtr<DataType> data_input_;
  InputMatcherPtr input_matcher_;
};

template <class DataType>
class MatcherList {
 public:
  void addMatcher(PredicatePtr<DataType> &&predicate, OnMatch<DataType> on_match) {
    fieldMatchers_.emplace_back(std::move(predicate), std::move(on_match));
  }

  MatchResult match(const DataType& data) {
    for (auto matcher : fieldMatchers_) {
      const auto m_result = matcher.first->match(data);
      if (m_result.result.has_value() && !m_result.result.value()) {
        matcher.second.actionCb();
        if (!matcher.second.keepMatching) {
          return true;
        }
      }
    }
    if (onNoMatch_.has_value()) {
      onNoMatch_->actionCb();
    }
    return false;
  }
  private:
   std::vector<std::pair<PredicatePtr<DataType>, OnMatch<DataType>>> fieldMatchers_;
   std::optional<OnMatch<DataType>> onNoMatch_;
};

}  // namespace matcher

#endif  // GRPC_SRC_CORE_XDS_GRPC_XDS_MATCHER_H