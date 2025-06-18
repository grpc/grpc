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

#include "src/core/xds/grpc/xds_matcher.h"

#include <memory>
#include <string>
#include <vector>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "absl/strings/string_view.h"
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
  UniqueTypeName context_type() const override {
    return GRPC_UNIQUE_TYPE_NAME_HERE("TestMatchContext");
  }

  std::optional<absl::string_view> GetValue(
      const XdsMatcher::MatchContext& context) const override {
    const auto* test_context =
        static_cast<const TestMatchContext*>(&context);
    return test_context->path();
  }
};

// A concrete implementation of Action for testing.
class TestAction : public XdsMatcher::Action {
 public:
  explicit TestAction(absl::string_view name) : name_(name) {}
  absl::string_view type_url() const override { return "test.TestAction"; }
  absl::string_view name() const { return name_; }

 private:
  std::string name_;
};

// A mock predicate for testing complex predicate structures.
class MockPredicate : public XdsMatcherList::Predicate {
 public:
  MOCK_METHOD(bool, Match, (const XdsMatcher::MatchContext& context),
              (const, override));
};

TEST(XdsMatcherTest, OnMatchWithAction) {
  TestMatchContext context("/foo");
  XdsMatcher::Result result;
  auto action = std::make_unique<TestAction>("test_action");
  XdsMatcher::OnMatch on_match(std::move(action), false);
  EXPECT_TRUE(on_match.FindMatches(context, result));
  ASSERT_EQ(result.size(), 1);
  EXPECT_EQ(static_cast<TestAction*>(result[0])->name(), "test_action");
}

TEST(XdsMatcherTest, OnMatchWithActionAndKeepMatching) {
  TestMatchContext context("/foo");
  XdsMatcher::Result result;
  auto action = std::make_unique<TestAction>("test_action");
  XdsMatcher::OnMatch on_match(std::move(action), true);
  EXPECT_FALSE(on_match.FindMatches(context, result));
  ASSERT_EQ(result.size(), 1);
  EXPECT_EQ(static_cast<TestAction*>(result[0])->name(), "test_action");
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
      std::make_unique<XdsMatcher::OnMatch>(
          std::make_unique<TestAction>("match"), false));
  XdsMatcherList matcher_list(std::move(matchers), nullptr);
  EXPECT_TRUE(matcher_list.FindMatches(context, result));
  ASSERT_EQ(result.size(), 1);
  EXPECT_EQ(static_cast<TestAction*>(result[0])->name(), "match");
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
      std::make_unique<XdsMatcher::OnMatch>(
          std::make_unique<TestAction>("match"), false));
  XdsMatcherList matcher_list(std::move(matchers), nullptr);
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
      std::make_unique<XdsMatcher::OnMatch>(
          std::make_unique<TestAction>("match"), false));
  auto on_no_match = std::make_unique<XdsMatcher::OnMatch>(
      std::make_unique<TestAction>("no_match"), false);
  XdsMatcherList matcher_list(std::move(matchers), std::move(on_no_match));
  EXPECT_TRUE(matcher_list.FindMatches(context, result));
  ASSERT_EQ(result.size(), 1);
  EXPECT_EQ(static_cast<TestAction*>(result[0])->name(), "no_match");
}

TEST(XdsMatcherListTest, AndPredicate) {
  TestMatchContext context("/foo");
  auto predicate1 = std::make_unique<MockPredicate>();
  auto predicate2 = std::make_unique<MockPredicate>();
  EXPECT_CALL(*predicate1, Match(::testing::_)).WillOnce(::testing::Return(true));
  EXPECT_CALL(*predicate2, Match(::testing::_)).WillOnce(::testing::Return(true));
  std::vector<std::unique_ptr<XdsMatcherList::Predicate>> predicates;
  predicates.push_back(std::move(predicate1));
  predicates.push_back(std::move(predicate2));
  XdsMatcherList::AndPredicate and_predicate(std::move(predicates));
  EXPECT_TRUE(and_predicate.Match(context));
}

TEST(XdsMatcherListTest, AndPredicateFail) {
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
  XdsMatcherList::AndPredicate and_predicate(std::move(predicates));
  EXPECT_FALSE(and_predicate.Match(context));
}

TEST(XdsMatcherListTest, OrPredicate) {
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
  XdsMatcherList::OrPredicate or_predicate(std::move(predicates));
  EXPECT_TRUE(or_predicate.Match(context));
}

TEST(XdsMatcherListTest, OrPredicateFail) {
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
  XdsMatcherList::OrPredicate or_predicate(std::move(predicates));
  EXPECT_FALSE(or_predicate.Match(context));
}

TEST(XdsMatcherListTest, NotPredicate) {
  TestMatchContext context("/foo");
  auto predicate = std::make_unique<MockPredicate>();
  EXPECT_CALL(*predicate, Match(::testing::_)).WillOnce(::testing::Return(false));
  XdsMatcherList::NotPredicate not_predicate(std::move(predicate));
  EXPECT_TRUE(not_predicate.Match(context));
}

TEST(XdsMatcherExactMapTest, Match) {
  TestMatchContext context("/foo");
  XdsMatcher::Result result;
  absl::flat_hash_map<std::string, std::unique_ptr<XdsMatcher::OnMatch>> map;
  map["/foo"] = std::make_unique<XdsMatcher::OnMatch>(
      std::make_unique<TestAction>("foo_action"), false);
  XdsMatcherExactMap matcher(std::make_unique<TestPathInput>(), std::move(map),
                             nullptr);
  EXPECT_TRUE(matcher.FindMatches(context, result));
  ASSERT_EQ(result.size(), 1);
  EXPECT_EQ(static_cast<TestAction*>(result[0])->name(), "foo_action");
}

TEST(XdsMatcherExactMapTest, NoMatch) {
  TestMatchContext context("/bar");
  XdsMatcher::Result result;
  absl::flat_hash_map<std::string, std::unique_ptr<XdsMatcher::OnMatch>> map;
  map["/foo"] = std::make_unique<XdsMatcher::OnMatch>(
      std::make_unique<TestAction>("foo_action"), false);
  XdsMatcherExactMap matcher(std::make_unique<TestPathInput>(), std::move(map),
                             nullptr);
  EXPECT_FALSE(matcher.FindMatches(context, result));
  EXPECT_TRUE(result.empty());
}

TEST(XdsMatcherExactMapTest, OnNoMatch) {
  TestMatchContext context("/bar");
  XdsMatcher::Result result;
  absl::flat_hash_map<std::string, std::unique_ptr<XdsMatcher::OnMatch>> map;
  map["/foo"] = std::make_unique<XdsMatcher::OnMatch>(
      std::make_unique<TestAction>("foo_action"), false);
  auto on_no_match = std::make_unique<XdsMatcher::OnMatch>(
      std::make_unique<TestAction>("no_match_action"), false);
  XdsMatcherExactMap matcher(std::make_unique<TestPathInput>(), std::move(map),
                             std::move(on_no_match));
  EXPECT_TRUE(matcher.FindMatches(context, result));
  ASSERT_EQ(result.size(), 1);
  EXPECT_EQ(static_cast<TestAction*>(result[0])->name(), "no_match_action");
}

TEST(XdsMatcherPrefixMapTest, ExactMatch) {
  TestMatchContext context("/foo/bar");
  XdsMatcher::Result result;
  absl::flat_hash_map<std::string, std::unique_ptr<XdsMatcher::OnMatch>> map;
  map["/foo/bar"] = std::make_unique<XdsMatcher::OnMatch>(
      std::make_unique<TestAction>("exact_match_action"), false);
  XdsMatcherPrefixMap matcher(std::make_unique<TestPathInput>(), std::move(map),
                              nullptr);
  EXPECT_TRUE(matcher.FindMatches(context, result));
  ASSERT_EQ(result.size(), 1);
  EXPECT_EQ(static_cast<TestAction*>(result[0])->name(), "exact_match_action");
}

TEST(XdsMatcherPrefixMapTest, PrefixMatch) {
  TestMatchContext context("/foo/bar/baz");
  XdsMatcher::Result result;
  absl::flat_hash_map<std::string, std::unique_ptr<XdsMatcher::OnMatch>> map;
  map["/foo/"] = std::make_unique<XdsMatcher::OnMatch>(
      std::make_unique<TestAction>("prefix_match_action"), false);
  XdsMatcherPrefixMap matcher(std::make_unique<TestPathInput>(), std::move(map),
                              nullptr);
  EXPECT_TRUE(matcher.FindMatches(context, result));
  ASSERT_EQ(result.size(), 1);
  EXPECT_EQ(static_cast<TestAction*>(result[0])->name(), "prefix_match_action");
}

TEST(XdsMatcherPrefixMapTest, LongestPrefixMatch) {
  TestMatchContext context("/foo/bar/baz");
  XdsMatcher::Result result;
  absl::flat_hash_map<std::string, std::unique_ptr<XdsMatcher::OnMatch>> map;
  map["/foo/"] = std::make_unique<XdsMatcher::OnMatch>(
      std::make_unique<TestAction>("shorter_prefix"), false);
  map["/foo/bar/"] = std::make_unique<XdsMatcher::OnMatch>(
      std::make_unique<TestAction>("longer_prefix"), false);
  XdsMatcherPrefixMap matcher(std::make_unique<TestPathInput>(), std::move(map),
                              nullptr);
  EXPECT_TRUE(matcher.FindMatches(context, result));
  ASSERT_EQ(result.size(), 1);
  EXPECT_EQ(static_cast<TestAction*>(result[0])->name(), "longer_prefix");
}

TEST(XdsMatcherPrefixMapTest, NoMatch) {
  TestMatchContext context("/qux");
  XdsMatcher::Result result;
  absl::flat_hash_map<std::string, std::unique_ptr<XdsMatcher::OnMatch>> map;
  map["/foo/"] = std::make_unique<XdsMatcher::OnMatch>(
      std::make_unique<TestAction>("foo_action"), false);
  XdsMatcherPrefixMap matcher(std::make_unique<TestPathInput>(), std::move(map),
                              nullptr);
  EXPECT_FALSE(matcher.FindMatches(context, result));
  EXPECT_TRUE(result.empty());
}

TEST(XdsMatcherPrefixMapTest, OnNoMatch) {
  TestMatchContext context("/qux");
  XdsMatcher::Result result;
  absl::flat_hash_map<std::string, std::unique_ptr<XdsMatcher::OnMatch>> map;
  map["/foo/"] = std::make_unique<XdsMatcher::OnMatch>(
      std::make_unique<TestAction>("foo_action"), false);
  auto on_no_match = std::make_unique<XdsMatcher::OnMatch>(
      std::make_unique<TestAction>("no_match_action"), false);
  XdsMatcherPrefixMap matcher(std::make_unique<TestPathInput>(), std::move(map),
                              std::move(on_no_match));
  EXPECT_TRUE(matcher.FindMatches(context, result));
  ASSERT_EQ(result.size(), 1);
  EXPECT_EQ(static_cast<TestAction*>(result[0])->name(), "no_match_action");
}
} // namespace
}  // namespace testing
}  // namespace grpc_core

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  auto result = RUN_ALL_TESTS();
  return result;
}