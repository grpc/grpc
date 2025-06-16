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

#ifndef GRPC_SRC_CORE_XDS_GRPC_XDS_MATCHER_H
#define GRPC_SRC_CORE_XDS_GRPC_XDS_MATCHER_H

#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/container/inlined_vector.h"
#include "src/core/util/matchers.h"
#include "src/core/util/trie_lookup.h"
#include "src/core/util/unique_type_name.h"

namespace grpc_core {

//
// XdsMatcher
//

// Base class for xDS matchers.
class XdsMatcher {

 public:

   enum MatcherType {
    MatcherList,
    MatcherExactMap,
    MatcherPrefixMap,
  };

  virtual MatcherType getType() = 0;
  // An interface implemented by the caller to provide the context from
  // which the inputs will extract data.  There can be different context
  // implementations for different use cases -- for example, there will
  // be an implementation that provides data about a data plane RPC for
  // use in per-RPC matching decisions, but there could also be an
  // implementation that provides data about incoming TCP connections
  // for L4 routing decisions.
  class MatchContext {
   public:
    virtual ~MatchContext() = default;

    // Returns the type of context.  The caller will use this to
    // determine which type to down-cast to.  Subclasses may add
    // whatever fields are appropriate.
    virtual UniqueTypeName type() const = 0;
  };

  // Produces match input from MatchContext.
  // There will be one subclass for each proto type that we support in
  // the input fields.
  template <typename T>
  class InputValue {
   public:
    using ProducedType = T;

    virtual ~InputValue() = default;

    // The supported MatchContext type.
    // When validating an xDS resource, if an input is specified in a
    // context that it doesn't support, the resource should be NACKed.
    virtual UniqueTypeName context_type() const = 0;

    // Gets the value to be matched from context.
    virtual std::optional<T> GetValue(const MatchContext& context) const = 0;
  };

  // An action to be returned if the conditions match.
  // There will be one subclass for each proto type that we support in
  // the action field.
  class Action {
   public:
    virtual ~Action() = default;

    // The protobuf type of the action.  Implementations will down-cast
    // appropriately based on this type, and subclasses can add whatever
    // additional methods they want.
    virtual absl::string_view type_url() const = 0;
  };

  // Actions found while executing the match.
  using Result = absl::InlinedVector<Action*, 1>;

  // What to do if a match is successful.
  // If this contains an action, the action will be added to the set of
  // actions to return.  If keep_matching is false, matching will return
  // true without evaluating any further matches; otherwise, matching
  // will continue to find a final match.
  struct OnMatch {
    std::variant<std::unique_ptr<Action>, std::unique_ptr<XdsMatcher>> action;
    bool keep_matching = false;

    // Constructor for Action variant
    OnMatch(std::unique_ptr<Action> act_ptr, bool km)
        : action(std::move(act_ptr)), keep_matching(km) {}

    // Constructor for XdsMatcher variant
    OnMatch(std::unique_ptr<XdsMatcher> matcher_ptr, bool km)
        : action(std::move(matcher_ptr)), keep_matching(km) {}

    bool FindMatches(const MatchContext& context, Result& result) const;
  };

  virtual ~XdsMatcher() = default;

  // Finds matching actions, which are added to result.
  // Returns true if the match is successful, in which case result will
  // contain at least one action.
  // Note that if a match is found but has keep_matching=true, the
  // action will be added to result, but the match will not be
  // considered successful.
  virtual bool FindMatches(const MatchContext& context,
                           Result& result) const = 0;
};

//
// XdsMatcherList
//

// Evaluates a list of predicates and corresponding actions.
// The first matching predicate wins.
class XdsMatcherList : public XdsMatcher {
 public:

  XdsMatcher::MatcherType getType() override {return  XdsMatcher::MatcherType::MatcherList; }
  
  // Base class for predicates.
  class Predicate {
   public:
    virtual ~Predicate() = default;

    // Returns true if the predicate is true.
    virtual bool Match(const XdsMatcher::MatchContext& context) const = 0;
  };

  // Interface for matching against an input value.
  template <typename T>
  class InputMatcher {
   public:
    using ConsumedType = T;

    virtual ~InputMatcher() = default;

    // Returns true if the matcher matches the input.
    virtual bool Match(const std::optional<T>& input) const = 0;
  };

  class StringInputMatcher;

  // Factory method for creating a SinglePredicate.
  template <typename InputType, typename MatcherType>
  static absl::enable_if_t<
      std::is_same<typename InputType::ProducedType,
                   typename MatcherType::ConsumedType>::value,
      std::unique_ptr<Predicate>>
  CreateSinglePredicate(std::unique_ptr<InputType> input,
                        std::unique_ptr<MatcherType> matcher) {
    return std::make_unique<
        XdsMatcherList::SinglePredicate<typename InputType::ProducedType>>(
        std::move(input), std::move(matcher));
  }

  // Alternative template specialization to return null in the case where
  // the input produces a different type than the matcher consumes.
  template <typename InputType, typename MatcherType>
  static absl::enable_if_t<
      !std::is_same<typename InputType::ProducedType,
                    typename MatcherType::ConsumedType>::value,
      std::unique_ptr<Predicate>>
  CreateSinglePredicate(std::unique_ptr<InputType> /*input*/,
                        std::unique_ptr<MatcherType> /*matcher*/) {
    return nullptr;
  }

  // Predicate implementations -- see below.
  class AndPredicate;
  class OrPredicate;
  class NotPredicate;

  struct FieldMatcher {
    FieldMatcher(std::unique_ptr<Predicate> predicate, std::unique_ptr<OnMatch> on_match)
        : predicate(std::move(predicate)), on_match(std::move(on_match)) {}

    std::unique_ptr<Predicate> predicate;
    std::unique_ptr<OnMatch> on_match;
  };

  XdsMatcherList(std::vector<FieldMatcher> matchers,
                 std::unique_ptr<OnMatch> on_no_match)
      : matchers_(std::move(matchers)), on_no_match_(std::move(on_no_match)) {}

  bool FindMatches(const MatchContext& context, Result& result) const override;

 private:
  template <typename T>
  class SinglePredicate;

  std::vector<FieldMatcher> matchers_;
  std::unique_ptr<OnMatch> on_no_match_;
};

//
// Predicates
//

// A predicate that evaluates a single input with a specified matcher.
template <typename T>
class XdsMatcherList::SinglePredicate : public XdsMatcherList::Predicate {
 public:
  SinglePredicate(std::unique_ptr<InputValue<T>> input,
                  std::unique_ptr<InputMatcher<T>> input_matcher)
      : input_(std::move(input)), input_matcher_(std::move(input_matcher)) {}

  bool Match(const XdsMatcher::MatchContext& context) const override {
    auto input = input_->GetValue(context);
    return input_matcher_->Match(input);
  }

 private:
  std::unique_ptr<InputValue<T>> input_;
  std::unique_ptr<InputMatcher<T>> input_matcher_;
};

// Matches against a string.
class XdsMatcherList::StringInputMatcher
    : public XdsMatcherList::InputMatcher<absl::string_view> {
 public:
  explicit StringInputMatcher(StringMatcher matcher)
      : matcher_(std::move(matcher)) {}

  bool Match(const std::optional<absl::string_view>& input) const override {
    return matcher_.Match(input.value_or(""));
  }

 private:
  StringMatcher matcher_;
};

// A predicate that evaluates a list of predicates, returning true if
// all predicates are true.
class XdsMatcherList::AndPredicate : public XdsMatcherList::Predicate {
 public:
  explicit AndPredicate(std::vector<std::unique_ptr<Predicate>> predicates)
      : predicates_(std::move(predicates)) {}

  bool Match(const XdsMatcher::MatchContext& context) const override;

 private:
  std::vector<std::unique_ptr<Predicate>> predicates_;
};

// A predicate that evaluates a list of predicates, returning true if
// any one predicate is true.
class XdsMatcherList::OrPredicate : public XdsMatcherList::Predicate {
 public:
  explicit OrPredicate(std::vector<std::unique_ptr<Predicate>> predicates)
      : predicates_(std::move(predicates)) {}

  bool Match(const XdsMatcher::MatchContext& context) const override;

 private:
  std::vector<std::unique_ptr<Predicate>> predicates_;
};

// A predicate that inverts another predicate.
class XdsMatcherList::NotPredicate : public XdsMatcherList::Predicate {
 public:
  explicit NotPredicate(std::unique_ptr<Predicate> predicate)
      : predicate_(std::move(predicate)) {}

  bool Match(const XdsMatcher::MatchContext& context) const override {
    return !predicate_->Match(context);
  }

 private:
  std::unique_ptr<Predicate> predicate_;
};

//
// XdsMatcherExactMap
//

class XdsMatcherExactMap : public XdsMatcher {
 public:
  XdsMatcher::MatcherType getType() override {return  XdsMatcher::MatcherType::MatcherExactMap; }
  XdsMatcherExactMap(std::unique_ptr<InputValue<absl::string_view>> input,
                     absl::flat_hash_map<std::string, std::unique_ptr<OnMatch>> map,
                     std::unique_ptr<OnMatch> on_no_match)
      : input_(std::move(input)),
        map_(std::move(map)),
        on_no_match_(std::move(on_no_match)) {}

  bool FindMatches(const MatchContext& context, Result& result) const override;

 private:
  std::unique_ptr<InputValue<absl::string_view>> input_;
  absl::flat_hash_map<std::string, std::unique_ptr<OnMatch>> map_;
  std::unique_ptr<OnMatch> on_no_match_;
};

class XdsMatcherPrefixMap : public XdsMatcher {
 public:
  XdsMatcher::MatcherType getType() override {return  XdsMatcher::MatcherType::MatcherPrefixMap; }
  XdsMatcherPrefixMap(
      std::unique_ptr<InputValue<absl::string_view>> input,
      absl::flat_hash_map<std::string, std::unique_ptr<XdsMatcher::OnMatch>>
          map,
      std::unique_ptr<OnMatch> on_no_match)
      : input_(std::move(input)), on_no_match_(std::move(on_no_match)) {
    PopulateTrie(std::move(map));
  }

  bool FindMatches(const MatchContext& context, Result& result) const override;

 private:
  void PopulateTrie(
      absl::flat_hash_map<std::string, std::unique_ptr<XdsMatcher::OnMatch>>
          map);

  TrieLookupTree<XdsMatcher::OnMatch> root_;
  std::unique_ptr<InputValue<absl::string_view>> input_;
  std::unique_ptr<OnMatch> on_no_match_;
};

}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_XDS_GRPC_XDS_MATCHER_H
