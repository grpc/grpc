//
// Copyright 2018 gRPC authors.
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

#include "src/core/util/matchers.h"

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
    return UNIQUE_TYPE_NAME_HERE("string");
  }

  UniqueTypeName type() const override { return Type(); }

  absl::string_view value() const { return value_; }

 private:
  std::string value_;
};

//
// Matchers
//

// Interface for matching against an input.
class InputMatcherBase {
 public:
  virtual ~InputMatcherBase() = default;

  // Indicates what input type is expected by this matcher.
  // When validating an xDS resource, if a matcher is specified with an
  // input type it does not support, the resource should be NACKed.
  virtual UniqueTypeName input_type() const = 0;

  // Returns true if the matcher matches the input.
  bool Match(const InputBase& input) const = 0;
}

// Matches against a string.
class StrinInputgMatcher : public InputMatcherBase {
 public:
  explicit StringInputMatcher(StringMatcher matcher)
      : matcher_(std::move(matcher)) {}

  UniqueTypeName input_type() const override { return StringInput::Type(); }

  bool Match(const InputBase& input) const override {
    return matcher_.Match(DownCast<StringInput&>(input).value());
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
                  std::unique_ptr<InputMatcherBase> matcher)
      : input_(std::move(input)), matcher_(std::move(matcher)) {
    CHECK_EQ(input_->type(), matcher_->input_type());
  }

  bool Match() const override { return matcher_->Match(*input_); }

 private:
  std::unique_ptr<InputBase> input_;
  std::unique_ptr<InputMatcherBase> matcher_;
};

// A predicate that evaluates a list of predicates, returning true if
// all predicates are true.
class AndPredicate : public Predicate {
 public:
  explicit AndPredicate(
      std::vector<std::unique_ptr<Predicate>> predicates)
      : predicates_(std::move(predicates)) {}

  bool Match() const override {
    for (const auto& predicate : predicates_) {
      if (!predicate.Match()) return false;
    }
    return true;
  }

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

  bool Match() const override {
    for (const auto& predicate : predicates_) {
      if (predicate.Match()) return true;
    }
    return false;
  }

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
// Action
//

class Action {
 public:
  virtual ~Action() = default;

  virtual absl::string_view type_url() const = 0;
};

//
// MatcherList
//

// Evaluates a list of predicates and corresponding actions.
// The first matching predicate wins.
class MatcherList {
 public:
  struct FieldMatcher {
    FieldMatcher(std::unique_ptr<Predicate> predicate,
                 std::unique_ptr<Action> action)
        : predicate_(std::move(predicate)), action_(std::move(action)) {}

    std::unique_ptr<Predicate> predicate;
    std::unique_ptr<Action> action;
  };

  explicit MatcherList(std::vector<FieldMatcher> matchers)
      : matchers_(std::move(matchers)) {}

  bool Match(absl::FunctionRef<void(const Action&, bool)> on_action) {
    for (const auto& [predicate, action] : matchers_) {
      if (predicate_->Match()) {
        on_action(
        return true;
      }
    }
  }

 private:
  std::vector<FieldMatcher> matchers_;
};

}  // namespace grpc_core

#endif  // GRPC_SRC_CORE_XDS_GRPC_XDS_MATCHER_H
