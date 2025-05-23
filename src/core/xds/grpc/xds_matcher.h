
#include <functional>
#include <memory>
#include <optional>
#include <unordered_map>
#include <variant>
#include <vector>

#include "xds/type/matcher/v3/matcher.upb.h"

namespace matcher {

int parseFunc(const xds_type_matcher_v3_Matcher* match);

using MatchDataType = std::string;

// Forward declaration
template <class DataType>
class MatcherList;

class Action {
 public:
  ~Action() = default;
};

using ActionPtr = std::unique_ptr<Action>;
using ActionCb = std::function<ActionPtr()>;
template <class DataType>
class OnMatch {
  ActionCb actionCb_;
  MatcherList<DataType> matcher_;
  bool keepMatching;
};

struct FieldMatchResult {
  std::optional<bool> result;
};

template <class DataType>
class FieldMatcher {
 public:
  virtual ~FieldMatcher() = default;
  virtual FieldMatchResult match(const DataType& data) = 0;
};

template <class DataType>
using FieldMatchPtr = std::unique_ptr<FieldMatcher<DataType>>;

template <class DataType>
class AndMatcher : public FieldMatcher<DataType> {
 public:
  explicit AndMatcher(std::vector<FieldMatchPtr<DataType>>&& matcherList)
      : matcherList_(std::move(matcherList)) {}
  FieldMatchResult match(const DataType& data) {
    for (const auto& matcher : matcherList_) {
      auto result = matcher->match(data);
      if (!result.result.has_value() || !result.result.value()) {
        return result;
      }
    }
    return {true};
  }

 private:
  std::vector<FieldMatchPtr<DataType>> matcherList_;
};

template <class DataType>
class AnyMatcher : public FieldMatcher<DataType> {
 public:
  explicit AnyMatcher(std::vector<FieldMatchPtr<DataType>>&& matcherList)
      : matcherList_(std::move(matcherList)) {}
  FieldMatchResult match(const DataType& data) {
    for (const auto& matcher : matcherList_) {
      auto result = matcher->match(data);
      if (result.result.has_value() && result.result.value()) {
        return result;
      }
    }
    return {false};
  }

 private:
  std::vector<FieldMatchPtr<DataType>> matcherList_;
};

class InputMatcher {
 public:
  virtual ~InputMatcher() = 0;
  virtual bool match(const MatchDataType& input) = 0;
};
using InputMatcherPtr = std::unique_ptr<InputMatcher>;
using InputMatcherFactoryCb = std::function<InputMatcherPtr()>;

template <class DataType>
class DataInput {
 public:
  virtual ~DataInput() = default;
  // Interface to return string for match
  //  return false if not available
  bool get(const DataType& data) = 0;
};
template <class DataType> using DataInputPtr = std::unique_ptr<DataInput<DataType>>;


template <class DataType>
class SinglePredicate : public FieldMatcher<DataType> {
 public:
  explicit SinglePredicate(DataInputPtr<DataType> && data, InputMatcherPtr && input) : data_(std::move(data)),input_(std::move(input)) { }
  bool match(const DataType& data) {}
 private:
  DataInputPtr<DataType> data_;
  InputMatcherPtr input_;
};

template <class DataType>
class MatcherList {
  std::vector<FieldMatcher<DataType>> fieldMatchers;
};

}  // namespace matcher