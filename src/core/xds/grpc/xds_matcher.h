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

#include "src/core/util/down_cast.h"
#include "src/core/util/match.h"
#include "src/core/util/matchers.h"
#include "src/core/util/unique_type_name.h"
#include "absl/container/flat_hash_map.h"
#include "absl/container/inlined_vector.h"

namespace grpc_core {

//
// Inputs
//

// Base class for input types.
// Types are identified by a UniqueTypeName, which will indicate what
// subclass type the object may be down-casted to.
// Subclasses may expose whatever methods are appropriate to provide
// data for their type.
class InputBase {
 public:
  virtual ~InputBase() = default;

  // A unique type name for this type of input.
  virtual UniqueTypeName type() const = 0;
};

// An input type for strings.
class StringInput : public InputBase {
 public:
  explicit StringInput(std::string value) : value_(std::move(value)) {}

  static UniqueTypeName Type() {
    return GRPC_UNIQUE_TYPE_NAME_HERE("string");
  }

  UniqueTypeName type() const override { return Type(); }

  absl::string_view value() const { return value_; }

 private:
  std::string value_;
};

//
// InputMatchers
//

// Interface for matching against an input.
class InputMatcher {
 public:
  virtual ~InputMatcher() = default;

  // Indicates what input type is expected by this matcher.
  // When validating an xDS resource, if a matcher is specified with an
  // input type it does not support, the resource should be NACKed.
  virtual UniqueTypeName input_type() const = 0;

  // Returns true if the matcher matches the input.
  virtual bool Match(const InputBase& input) const = 0;
};

// Matches against a string.
class StringInputMatcher : public InputMatcher {
 public:
  explicit StringInputMatcher(StringMatcher matcher)
      : matcher_(std::move(matcher)) {}

  UniqueTypeName input_type() const override { return StringInput::Type(); }

  bool Match(const InputBase& input) const override {
    return matcher_.Match(DownCast<const StringInput&>(input).value());
  }

 private:
  StringMatcher matcher_;
};

//
// Predicates
//

// Base class for predicates.
class Predicate {
 public:
  virtual ~Predicate() = default;

  // Returns true if the predicate is true.
  virtual bool Match() const = 0;
};

// A predicate that evaluates a single matcher with a specified input.
class SinglePredicate : public Predicate {
 public:
  SinglePredicate(std::unique_ptr<InputBase> input,
                  std::unique_ptr<InputMatcher> input_matcher)
      : input_(std::move(input)), input_matcher_(std::move(input_matcher)) {
    CHECK_EQ(input_->type(), input_matcher_->input_type());
  }

  bool Match() const override { return input_matcher_->Match(*input_); }

 private:
  std::unique_ptr<InputBase> input_;
  std::unique_ptr<InputMatcher> input_matcher_;
};

// A predicate that evaluates a list of predicates, returning true if
// all predicates are true.
class AndPredicate : public Predicate {
 public:
  explicit AndPredicate(
      std::vector<std::unique_ptr<Predicate>> predicates)
      : predicates_(std::move(predicates)) {}

  bool Match() const override;

 private:
  std::vector<std::unique_ptr<Predicate>> predicates_;
};

// A predicate that evaluates a list of predicates, returning true if
// any one predicate is true.
class OrPredicate : public Predicate {
 public:
  explicit OrPredicate(
      std::vector<std::unique_ptr<Predicate>> predicates)
      : predicates_(std::move(predicates)) {}

  bool Match() const override;

 private:
  std::vector<std::unique_ptr<Predicate>> predicates_;
};

// A predicate that inverts another predicate.
class NotPredicate : public Predicate {
 public:
  explicit NotPredicate(std::unique_ptr<Predicate> predicate)
      : predicate_(std::move(predicate)) {}

  bool Match() const override { return !predicate_->Match(); }

 private:
  std::unique_ptr<Predicate> predicate_;
};

//
// XdsMatcher
//

// Base class for xDS matchers.
class XdsMatcher {
 public:
  // An action to be returned if the conditions match.
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

    bool FindMatches(Result& result) const;
  };

  virtual ~XdsMatcher() = default;

  // Finds matching actions, which are added to result.
  // Returns true if the match is successful, in which case result will
  // contain at least one action.
  // Note that if a match is found but has keep_matching=true, the
  // action will be added to result, but the match will not be
  // considered successful.
// FIXME: need to specify some sort of input here, like the attributes
// of a data plane RPC.  and we need a way for the Input objects to
// extract data from that input.
  virtual bool FindMatches(Result& result) const = 0;
};

// Evaluates a list of predicates and corresponding actions.
// The first matching predicate wins.
class MatcherList : public XdsMatcher {
 public:
  struct FieldMatcher {
    FieldMatcher(std::unique_ptr<Predicate> predicate, OnMatch on_match)
        : predicate(std::move(predicate)), on_match(std::move(on_match)) {}

    std::unique_ptr<Predicate> predicate;
    OnMatch on_match;
  };

  MatcherList(std::vector<FieldMatcher> matchers,
              std::optional<OnMatch> on_no_match)
      : matchers_(std::move(matchers)), on_no_match_(std::move(on_no_match)) {}

  bool FindMatches(Result& result) const override;

 private:
  std::vector<FieldMatcher> matchers_;
  std::optional<OnMatch> on_no_match_;
};

// Exact map matcher.
class MatcherExactMap : public XdsMatcher {
 public:
  MatcherExactMap(StringInput input,
                  absl::flat_hash_map<std::string, OnMatch> map,
                  std::optional<OnMatch> on_no_match)
      : input_(std::move(input)),
        map_(std::move(map)),
        on_no_match_(std::move(on_no_match)) {}

  bool FindMatches(Result& result) const override;

 private:
  StringInput input_;
  absl::flat_hash_map<std::string, OnMatch> map_;
  std::optional<OnMatch> on_no_match_;
};

}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_XDS_GRPC_XDS_MATCHER_H
