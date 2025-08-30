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

#include "src/core/xds/grpc/xds_matcher.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "absl/strings/string_view.h"
#include "src/core/util/down_cast.h"
#include "src/core/util/matchers.h"

namespace grpc_core {
namespace testing {
namespace {

// A concrete implementation of MatchContext for testing purposes.
class TestMatchContext : public XdsMatcher::MatchContext {
 public:
  explicit TestMatchContext(absl::string_view path) : path_(path) {}

  UniqueTypeName type() const override {
    return GRPC_UNIQUE_TYPE_NAME_HERE("TestMatchContext");
  }

  absl::string_view path() const { return path_; }

 private:
  absl::string_view path_;
};

// A concrete implementation of InputValue for testing.
class TestPathInput : public XdsMatcher::InputValue<absl::string_view> {
 public:
  std::optional<absl::string_view> GetValue(
      const XdsMatcher::MatchContext& context) const override {
    const auto& test_context = DownCast<const TestMatchContext&>(context);
    return test_context.path();
  }
  static UniqueTypeName Type() {
    return GRPC_UNIQUE_TYPE_NAME_HERE("TestPathInput");
  }
  UniqueTypeName type() const override { return Type(); }
  bool Equals(const XdsMatcher::InputValue<absl::string_view>&) const override {
    return true;
  }
  std::string ToString() const override { return "TestPathInput"; }
};

// A concrete implementation of Action for testing.
class TestAction : public XdsMatcher::Action {
 public:
  explicit TestAction(absl::string_view name) : name_(name) {}
  static UniqueTypeName Type() {
    return GRPC_UNIQUE_TYPE_NAME_HERE("test.TestAction");
  }
  UniqueTypeName type() const override { return Type(); }
  absl::string_view name() const { return name_; }
  bool Equals(const XdsMatcher::Action& other) const override {
    if (other.type() != type()) return false;
    return name_ == DownCast<const TestAction&>(other).name_;
  }
  std::string ToString() const override {
    return absl::StrCat("TestAction{name=", name(), "}");
  }

 private:
  std::string name_;
};

MATCHER_P(IsTestAction, expected, "") {
  return ::testing::ExplainMatchResult(
      ::testing::WhenDynamicCastTo<const TestAction*>(
          ::testing::Property(&TestAction::name, expected)),
      arg, result_listener);
}

// A mock predicate for testing complex predicate structures.
class MockPredicate : public XdsMatcherList::Predicate {
 public:
  MOCK_METHOD(bool, Match, (const XdsMatcher::MatchContext& context),
              (const, override));
  MOCK_METHOD(UniqueTypeName, type, (), (const, override));
  MOCK_METHOD(bool, Equals, (const XdsMatcherList::Predicate& other),
              (const, override));
  MOCK_METHOD(std::string, ToString, (), (const, override));
};

TEST(XdsMatcherTest, OnMatchWithAction) {
  TestMatchContext context("/foo");
  XdsMatcher::Result result;
  auto action = std::make_unique<TestAction>("test_action");
  XdsMatcher::OnMatch on_match(std::move(action), false);
  EXPECT_TRUE(on_match.FindMatches(context, result));
  EXPECT_THAT(result, ::testing::ElementsAre(IsTestAction("test_action")));
}

TEST(XdsMatcherListTest, BasicMatch) {
  TestMatchContext context("/foo/bar");
  XdsMatcher::Result result;
  std::vector<XdsMatcherList::FieldMatcher> matchers;
  matchers.emplace_back(
      XdsMatcherList::CreateSinglePredicate(
          std::make_unique<TestPathInput>(),
          std::make_unique<XdsMatcherList::StringInputMatcher>(
              StringMatcher::Create(StringMatcher::Type::kExact, "/foo/bar")
                  .value())),
      XdsMatcher::OnMatch(std::make_unique<TestAction>("match"), false));
  XdsMatcherList matcher_list(std::move(matchers), std::nullopt);
  EXPECT_TRUE(matcher_list.FindMatches(context, result));
  EXPECT_THAT(result, ::testing::ElementsAre(IsTestAction("match")));
}

TEST(XdsMatcherListTest, BasicMatchWithKeepMatching) {
  TestMatchContext context("/foo/bar");
  XdsMatcher::Result result;
  std::vector<XdsMatcherList::FieldMatcher> matchers;
  matchers.emplace_back(
      XdsMatcherList::CreateSinglePredicate(
          std::make_unique<TestPathInput>(),
          std::make_unique<XdsMatcherList::StringInputMatcher>(
              StringMatcher::Create(StringMatcher::Type::kExact, "/foo/bar")
                  .value())),
      XdsMatcher::OnMatch(std::make_unique<TestAction>("match"), true));
  XdsMatcherList matcher_list(std::move(matchers), std::nullopt);
  EXPECT_FALSE(matcher_list.FindMatches(context, result));
  EXPECT_THAT(result, ::testing::ElementsAre(IsTestAction("match")));
}

TEST(XdsMatcherListTest, BasicMatchNestedMatcher) {
  TestMatchContext context("/foo/bar");
  XdsMatcher::Result result;
  std::vector<XdsMatcherList::FieldMatcher> matchers;
  std::vector<XdsMatcherList::FieldMatcher> nested_matchers;
  nested_matchers.emplace_back(
      XdsMatcherList::CreateSinglePredicate(
          std::make_unique<TestPathInput>(),
          std::make_unique<XdsMatcherList::StringInputMatcher>(
              StringMatcher::Create(StringMatcher::Type::kExact, "/foo/bar")
                  .value())),
      XdsMatcher::OnMatch(std::make_unique<TestAction>("match"), false));
  matchers.emplace_back(
      XdsMatcherList::CreateSinglePredicate(
          std::make_unique<TestPathInput>(),
          std::make_unique<XdsMatcherList::StringInputMatcher>(
              StringMatcher::Create(StringMatcher::Type::kExact, "/foo/bar")
                  .value())),
      XdsMatcher::OnMatch(std::make_unique<XdsMatcherList>(
                              std::move(nested_matchers), std::nullopt),
                          false));
  XdsMatcherList matcher_list(std::move(matchers), std::nullopt);
  EXPECT_TRUE(matcher_list.FindMatches(context, result));
  EXPECT_THAT(result, ::testing::ElementsAre(IsTestAction("match")));
}

TEST(XdsMatcherListTest, NoMatch) {
  TestMatchContext context("/baz");
  XdsMatcher::Result result;
  std::vector<XdsMatcherList::FieldMatcher> matchers;
  matchers.emplace_back(
      XdsMatcherList::CreateSinglePredicate(
          std::make_unique<TestPathInput>(),
          std::make_unique<XdsMatcherList::StringInputMatcher>(
              StringMatcher::Create(StringMatcher::Type::kExact, "/foo/bar")
                  .value())),
      XdsMatcher::OnMatch(std::make_unique<TestAction>("match"), false));
  XdsMatcherList matcher_list(std::move(matchers), std::nullopt);
  EXPECT_FALSE(matcher_list.FindMatches(context, result));
  EXPECT_TRUE(result.empty());
}

TEST(XdsMatcherListTest, OnNoMatch) {
  TestMatchContext context("/baz");
  XdsMatcher::Result result;
  std::vector<XdsMatcherList::FieldMatcher> matchers;
  matchers.emplace_back(
      XdsMatcherList::CreateSinglePredicate(
          std::make_unique<TestPathInput>(),
          std::make_unique<XdsMatcherList::StringInputMatcher>(
              StringMatcher::Create(StringMatcher::Type::kExact, "/foo/bar")
                  .value())),
      XdsMatcher::OnMatch(std::make_unique<TestAction>("match"), false));
  auto on_no_match =
      XdsMatcher::OnMatch(std::make_unique<TestAction>("no_match"), false);
  XdsMatcherList matcher_list(std::move(matchers), std::move(on_no_match));
  EXPECT_TRUE(matcher_list.FindMatches(context, result));
  EXPECT_THAT(result, ::testing::ElementsAre(IsTestAction("no_match")));
}

TEST(XdsMatcherListTest, OnNoMatchWithKeepMatching) {
  TestMatchContext context("/foo/bar");
  XdsMatcher::Result result;
  std::vector<XdsMatcherList::FieldMatcher> matchers;
  matchers.emplace_back(
      XdsMatcherList::CreateSinglePredicate(
          std::make_unique<TestPathInput>(),
          std::make_unique<XdsMatcherList::StringInputMatcher>(
              StringMatcher::Create(StringMatcher::Type::kExact, "/foo/bar")
                  .value())),
      XdsMatcher::OnMatch(std::make_unique<TestAction>("match"), true));
  auto on_no_match =
      XdsMatcher::OnMatch(std::make_unique<TestAction>("no_match"), false);
  XdsMatcherList matcher_list(std::move(matchers), std::move(on_no_match));
  EXPECT_TRUE(matcher_list.FindMatches(context, result));
  EXPECT_THAT(result, ::testing::ElementsAre(IsTestAction("match"),
                                             IsTestAction("no_match")));
}

TEST(XdsMatcherListTest, KeepMatchingFalseStopMatching) {
  // Setup: Create a matcher list where two consecutive predicates would match.
  // The first one has keep_matching=false.
  std::vector<XdsMatcherList::FieldMatcher> matchers;
  // Matcher 1: Will match, keep_matching is false.
  auto predicate1 = std::make_unique<MockPredicate>();
  EXPECT_CALL(*predicate1, Match(::testing::_))
      .WillOnce(::testing::Return(true));
  matchers.emplace_back(
      std::move(predicate1),
      XdsMatcher::OnMatch(std::make_unique<TestAction>("Action1"), false));
  // Matcher 2: Would also match, but should never be evaluated.
  auto predicate2 = std::make_unique<MockPredicate>();
  matchers.emplace_back(
      std::move(predicate2),
      XdsMatcher::OnMatch(std::make_unique<TestAction>("Action2"), false));
  XdsMatcherList matcher_list(std::move(matchers), std::nullopt);
  TestMatchContext context("/qux");
  XdsMatcher::Result result;
  // Execute
  bool match_found = matcher_list.FindMatches(context, result);
  // Checks
  EXPECT_TRUE(match_found);
  EXPECT_THAT(result, ::testing::ElementsAre(IsTestAction("Action1")));
}

TEST(XdsMatcherListTest, KeepMatchingTrueContinueMatching) {
  // Verifies that when keep_matching is true, matching continues and
  // actions are accumulated until a final match (with keep_matching=false) is
  // found.
  std::vector<XdsMatcherList::FieldMatcher> matchers;
  // Matcher 1: Will match, keep_matching = true
  auto predicate1 = std::make_unique<MockPredicate>();
  EXPECT_CALL(*predicate1, Match(::testing::_))
      .WillOnce(::testing::Return(true));
  matchers.emplace_back(
      std::move(predicate1),
      XdsMatcher::OnMatch(std::make_unique<TestAction>("Action1"), true));
  // Matcher 2: Would not match.
  auto predicate2 = std::make_unique<MockPredicate>();
  EXPECT_CALL(*predicate2, Match(::testing::_))
      .WillOnce(::testing::Return(false));
  matchers.emplace_back(
      std::move(predicate2),
      XdsMatcher::OnMatch(std::make_unique<TestAction>("Action2"), false));
  // Matcher 3: Would also match Terminal match, with keep matching as false
  auto predicate3 = std::make_unique<MockPredicate>();
  EXPECT_CALL(*predicate3, Match(::testing::_))
      .WillOnce(::testing::Return(true));
  matchers.emplace_back(
      std::move(predicate3),
      XdsMatcher::OnMatch(std::make_unique<TestAction>("Action3"), false));
  XdsMatcherList matcher_list(std::move(matchers), std::nullopt);
  TestMatchContext context("/qux");
  XdsMatcher::Result result;
  // Execute
  bool match_found = matcher_list.FindMatches(context, result);
  // Assert
  EXPECT_TRUE(match_found);
  EXPECT_THAT(result, ::testing::ElementsAre(IsTestAction("Action1"),
                                             IsTestAction("Action3")));
}

TEST(PredicateTest, AndPredicate) {
  TestMatchContext context("/foo");
  auto predicate1 = std::make_unique<MockPredicate>();
  auto predicate2 = std::make_unique<MockPredicate>();
  EXPECT_CALL(*predicate1, Match(::testing::_))
      .WillOnce(::testing::Return(true));
  EXPECT_CALL(*predicate2, Match(::testing::_))
      .WillOnce(::testing::Return(true));
  std::vector<std::unique_ptr<XdsMatcherList::Predicate>> predicates;
  predicates.push_back(std::move(predicate1));
  predicates.push_back(std::move(predicate2));
  auto and_predicate =
      XdsMatcherList::AndPredicate::Create(std::move(predicates));
  EXPECT_TRUE(and_predicate->Match(context));
}

TEST(PredicateTest, AndPredicateFail) {
  TestMatchContext context("/foo");
  auto predicate1 = std::make_unique<MockPredicate>();
  auto predicate2 = std::make_unique<MockPredicate>();
  EXPECT_CALL(*predicate1, Match(::testing::_))
      .WillOnce(::testing::Return(true));
  EXPECT_CALL(*predicate2, Match(::testing::_))
      .WillOnce(::testing::Return(false));
  std::vector<std::unique_ptr<XdsMatcherList::Predicate>> predicates;
  predicates.push_back(std::move(predicate1));
  predicates.push_back(std::move(predicate2));
  auto and_predicate =
      XdsMatcherList::AndPredicate::Create(std::move(predicates));
  EXPECT_FALSE(and_predicate->Match(context));
}

TEST(PredicateTest, OrPredicate) {
  TestMatchContext context("/foo");
  auto predicate1 = std::make_unique<MockPredicate>();
  auto predicate2 = std::make_unique<MockPredicate>();
  EXPECT_CALL(*predicate1, Match(::testing::_))
      .WillOnce(::testing::Return(false));
  EXPECT_CALL(*predicate2, Match(::testing::_))
      .WillOnce(::testing::Return(true));
  std::vector<std::unique_ptr<XdsMatcherList::Predicate>> predicates;
  predicates.push_back(std::move(predicate1));
  predicates.push_back(std::move(predicate2));
  auto or_predicate =
      XdsMatcherList::OrPredicate::Create(std::move(predicates));
  EXPECT_TRUE(or_predicate->Match(context));
}

TEST(PredicateTest, OrPredicateFail) {
  TestMatchContext context("/foo");
  auto predicate1 = std::make_unique<MockPredicate>();
  auto predicate2 = std::make_unique<MockPredicate>();
  EXPECT_CALL(*predicate1, Match(::testing::_))
      .WillOnce(::testing::Return(false));
  EXPECT_CALL(*predicate2, Match(::testing::_))
      .WillOnce(::testing::Return(false));
  std::vector<std::unique_ptr<XdsMatcherList::Predicate>> predicates;
  predicates.push_back(std::move(predicate1));
  predicates.push_back(std::move(predicate2));
  auto or_predicate =
      XdsMatcherList::OrPredicate::Create(std::move(predicates));
  EXPECT_FALSE(or_predicate->Match(context));
}

TEST(PredicateTest, NotPredicate) {
  TestMatchContext context("/foo");
  auto predicate = std::make_unique<MockPredicate>();
  EXPECT_CALL(*predicate, Match(::testing::_))
      .WillOnce(::testing::Return(false));
  auto not_predicate =
      XdsMatcherList::NotPredicate::Create(std::move(predicate));
  EXPECT_TRUE(not_predicate->Match(context));
}

TEST(XdsMatcherExactMapTest, BasicMatch) {
  TestMatchContext context("/foo");
  XdsMatcher::Result result;
  absl::flat_hash_map<std::string, XdsMatcher::OnMatch> map;
  map.emplace("/foo", XdsMatcher::OnMatch(
                          std::make_unique<TestAction>("foo_action"), false));
  XdsMatcherExactMap matcher(std::make_unique<TestPathInput>(), std::move(map),
                             std::nullopt);
  EXPECT_TRUE(matcher.FindMatches(context, result));
  EXPECT_THAT(result, ::testing::ElementsAre(IsTestAction("foo_action")));
}

TEST(XdsMatcherExactMapTest, MatchWithKeepMatching) {
  TestMatchContext context("/foo");
  XdsMatcher::Result result;
  absl::flat_hash_map<std::string, XdsMatcher::OnMatch> map;
  map.emplace("/foo", XdsMatcher::OnMatch(
                          std::make_unique<TestAction>("foo_action"), true));
  XdsMatcherExactMap matcher(std::make_unique<TestPathInput>(), std::move(map),
                             std::nullopt);
  EXPECT_FALSE(matcher.FindMatches(context, result));
  EXPECT_THAT(result, ::testing::ElementsAre(IsTestAction("foo_action")));
}

TEST(XdsMatcherExactMapTest, BasicMatchNestedMatcher) {
  TestMatchContext context("/foo");
  XdsMatcher::Result result;
  absl::flat_hash_map<std::string, XdsMatcher::OnMatch> map;
  std::vector<XdsMatcherList::FieldMatcher> nested_matchers;
  nested_matchers.emplace_back(
      XdsMatcherList::CreateSinglePredicate(
          std::make_unique<TestPathInput>(),
          std::make_unique<XdsMatcherList::StringInputMatcher>(
              StringMatcher::Create(StringMatcher::Type::kExact, "/foo")
                  .value())),
      XdsMatcher::OnMatch(std::make_unique<TestAction>("foo_action"), false));
  map.emplace("/foo",
              XdsMatcher::OnMatch(std::make_unique<XdsMatcherList>(
                                      std::move(nested_matchers), std::nullopt),
                                  false));
  XdsMatcherExactMap matcher(std::make_unique<TestPathInput>(), std::move(map),
                             std::nullopt);
  EXPECT_TRUE(matcher.FindMatches(context, result));
  EXPECT_THAT(result, ::testing::ElementsAre(IsTestAction("foo_action")));
}

TEST(XdsMatcherExactMapTest, NoMatch) {
  TestMatchContext context("/bar");
  XdsMatcher::Result result;
  absl::flat_hash_map<std::string, XdsMatcher::OnMatch> map;
  map.emplace("/foo", XdsMatcher::OnMatch(
                          std::make_unique<TestAction>("foo_action"), false));
  XdsMatcherExactMap matcher(std::make_unique<TestPathInput>(), std::move(map),
                             std::nullopt);
  EXPECT_FALSE(matcher.FindMatches(context, result));
  EXPECT_TRUE(result.empty());
}

TEST(XdsMatcherExactMapTest, OnNoMatch) {
  TestMatchContext context("/bar");
  XdsMatcher::Result result;
  absl::flat_hash_map<std::string, XdsMatcher::OnMatch> map;
  map.emplace("/foo", XdsMatcher::OnMatch(
                          std::make_unique<TestAction>("foo_action"), false));
  auto on_no_match = XdsMatcher::OnMatch(
      std::make_unique<TestAction>("no_match_action"), false);
  XdsMatcherExactMap matcher(std::make_unique<TestPathInput>(), std::move(map),
                             std::move(on_no_match));
  EXPECT_TRUE(matcher.FindMatches(context, result));
  EXPECT_THAT(result, ::testing::ElementsAre(IsTestAction("no_match_action")));
}

TEST(XdsMatcherExactMapTest, OnNoMatchWithKeepMatching) {
  TestMatchContext context("/foo");
  XdsMatcher::Result result;
  absl::flat_hash_map<std::string, XdsMatcher::OnMatch> map;
  map.emplace("/foo", XdsMatcher::OnMatch(
                          std::make_unique<TestAction>("foo_action"), true));
  auto on_no_match = XdsMatcher::OnMatch(
      std::make_unique<TestAction>("no_match_action"), false);
  XdsMatcherExactMap matcher(std::make_unique<TestPathInput>(), std::move(map),
                             std::move(on_no_match));
  EXPECT_TRUE(matcher.FindMatches(context, result));
  EXPECT_THAT(result, ::testing::ElementsAre(IsTestAction("foo_action"),
                                             IsTestAction("no_match_action")));
}

TEST(XdsMatcherPrefixMapTest, ExactMatch) {
  TestMatchContext context("/foo/bar");
  XdsMatcher::Result result;
  absl::flat_hash_map<std::string, XdsMatcher::OnMatch> map;
  map.emplace("/foo/bar",
              XdsMatcher::OnMatch(
                  std::make_unique<TestAction>("exact_match_action"), false));
  XdsMatcherPrefixMap matcher(std::make_unique<TestPathInput>(), std::move(map),
                              std::nullopt);
  EXPECT_TRUE(matcher.FindMatches(context, result));
  EXPECT_THAT(result,
              ::testing::ElementsAre(IsTestAction("exact_match_action")));
}

TEST(XdsMatcherPrefixMapTest, PrefixMatch) {
  TestMatchContext context("/foo/bar/baz");
  XdsMatcher::Result result;
  absl::flat_hash_map<std::string, XdsMatcher::OnMatch> map;
  map.emplace("/foo/",
              XdsMatcher::OnMatch(
                  std::make_unique<TestAction>("prefix_match_action"), false));
  XdsMatcherPrefixMap matcher(std::make_unique<TestPathInput>(), std::move(map),
                              std::nullopt);
  EXPECT_TRUE(matcher.FindMatches(context, result));
  EXPECT_THAT(result,
              ::testing::ElementsAre(IsTestAction("prefix_match_action")));
}

TEST(XdsMatcherPrefixMapTest, BasicMatchNestedMatcher) {
  TestMatchContext context("/foo");
  XdsMatcher::Result result;
  absl::flat_hash_map<std::string, XdsMatcher::OnMatch> map;
  std::vector<XdsMatcherList::FieldMatcher> nested_matchers;
  nested_matchers.emplace_back(
      XdsMatcherList::CreateSinglePredicate(
          std::make_unique<TestPathInput>(),
          std::make_unique<XdsMatcherList::StringInputMatcher>(
              StringMatcher::Create(StringMatcher::Type::kExact, "/foo")
                  .value())),
      XdsMatcher::OnMatch(std::make_unique<TestAction>("foo_action"), false));
  map.emplace("/foo",
              XdsMatcher::OnMatch(std::make_unique<XdsMatcherList>(
                                      std::move(nested_matchers), std::nullopt),
                                  false));
  XdsMatcherPrefixMap matcher(std::make_unique<TestPathInput>(), std::move(map),
                              std::nullopt);
  EXPECT_TRUE(matcher.FindMatches(context, result));
  EXPECT_THAT(result, ::testing::ElementsAre(IsTestAction("foo_action")));
}

TEST(XdsMatcherPrefixMapTest, PrefixMatchWithKeepMatching) {
  TestMatchContext context("/foo/bar/baz");
  XdsMatcher::Result result;
  absl::flat_hash_map<std::string, XdsMatcher::OnMatch> map;
  map.emplace("/foo/",
              XdsMatcher::OnMatch(
                  std::make_unique<TestAction>("prefix_match_action"), true));
  XdsMatcherPrefixMap matcher(std::make_unique<TestPathInput>(), std::move(map),
                              std::nullopt);
  EXPECT_FALSE(matcher.FindMatches(context, result));
  EXPECT_THAT(result,
              ::testing::ElementsAre(IsTestAction("prefix_match_action")));
}

TEST(XdsMatcherPrefixMapTest, PrefixListCheck) {
  TestMatchContext context("/foo/bar/baz");
  XdsMatcher::Result result;
  absl::flat_hash_map<std::string, XdsMatcher::OnMatch> map;
  map.emplace("/foo/",
              XdsMatcher::OnMatch(
                  std::make_unique<TestAction>("shorter_prefix"), false));
  map.emplace("/foo/bar/",
              XdsMatcher::OnMatch(std::make_unique<TestAction>("longer_prefix"),
                                  false));
  XdsMatcherPrefixMap matcher(std::make_unique<TestPathInput>(), std::move(map),
                              std::nullopt);
  EXPECT_TRUE(matcher.FindMatches(context, result));
  EXPECT_THAT(result, ::testing::ElementsAre(IsTestAction("longer_prefix")));
}

TEST(XdsMatcherPrefixMapTest, PrefixMatchKeepMatchingMultipleMatch) {
  TestMatchContext context("/foo/bar/baz");
  XdsMatcher::Result result;
  absl::flat_hash_map<std::string, XdsMatcher::OnMatch> map;
  map.emplace("/foo", XdsMatcher::OnMatch(std::make_unique<TestAction>("first"),
                                          false));
  map.emplace("/foo/bar", XdsMatcher::OnMatch(
                              std::make_unique<TestAction>("second"), false));
  map.emplace("/foo/bar/baz",
              XdsMatcher::OnMatch(std::make_unique<TestAction>("third"), true));
  XdsMatcherPrefixMap matcher(std::make_unique<TestPathInput>(), std::move(map),
                              std::nullopt);
  EXPECT_TRUE(matcher.FindMatches(context, result));
  EXPECT_THAT(result, ::testing::ElementsAre(IsTestAction("third"),
                                             IsTestAction("second")));
}

TEST(XdsMatcherPrefixMapTest, NoMatch) {
  TestMatchContext context("/qux");
  XdsMatcher::Result result;
  absl::flat_hash_map<std::string, XdsMatcher::OnMatch> map;
  map.emplace("/foo/", XdsMatcher::OnMatch(
                           std::make_unique<TestAction>("foo_action"), false));
  XdsMatcherPrefixMap matcher(std::make_unique<TestPathInput>(), std::move(map),
                              std::nullopt);
  EXPECT_FALSE(matcher.FindMatches(context, result));
  EXPECT_TRUE(result.empty());
}

TEST(XdsMatcherPrefixMapTest, OnNoMatch) {
  TestMatchContext context("/qux");
  XdsMatcher::Result result;
  absl::flat_hash_map<std::string, XdsMatcher::OnMatch> map;
  map.emplace("/foo/", XdsMatcher::OnMatch(
                           std::make_unique<TestAction>("foo_action"), false));
  auto on_no_match = XdsMatcher::OnMatch(
      std::make_unique<TestAction>("no_match_action"), false);
  XdsMatcherPrefixMap matcher(std::make_unique<TestPathInput>(), std::move(map),
                              std::move(on_no_match));
  EXPECT_TRUE(matcher.FindMatches(context, result));
  EXPECT_THAT(result, ::testing::ElementsAre(IsTestAction("no_match_action")));
}

TEST(XdsMatcherPrefixMapTest, OnNoMatchWithKeepMatching) {
  TestMatchContext context("/foo");
  XdsMatcher::Result result;
  absl::flat_hash_map<std::string, XdsMatcher::OnMatch> map;
  map.emplace("/foo", XdsMatcher::OnMatch(
                          std::make_unique<TestAction>("foo_action"), true));
  auto on_no_match = XdsMatcher::OnMatch(
      std::make_unique<TestAction>("no_match_action"), false);
  XdsMatcherPrefixMap matcher(std::make_unique<TestPathInput>(), std::move(map),
                              std::move(on_no_match));
  EXPECT_TRUE(matcher.FindMatches(context, result));
  EXPECT_THAT(result, ::testing::ElementsAre(IsTestAction("foo_action"),
                                             IsTestAction("no_match_action")));
}

}  // namespace
}  // namespace testing
}  // namespace grpc_core

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
