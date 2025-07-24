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

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "src/core/util/down_cast.h"
#include "test/core/test_util/test_config.h"

namespace grpc_core {
namespace testing {
namespace {

class TestMatchContext : public XdsMatcher::MatchContext {
 public:
  TestMatchContext(std::map<absl::string_view, absl::string_view> string_values,
                   std::map<absl::string_view, int> int_values)
      : string_values_(std::move(string_values)),
        int_values_(std::move(int_values)) {}

  static UniqueTypeName Type() { return GRPC_UNIQUE_TYPE_NAME_HERE("test"); }

  UniqueTypeName type() const override { return Type(); }

  absl::string_view GetStringValue(absl::string_view key) const {
    auto it = string_values_.find(key);
    if (it != string_values_.end()) return it->second;
    return "";
  }

  std::optional<int> GetIntValue(absl::string_view key) const {
    auto it = int_values_.find(key);
    if (it != int_values_.end()) return it->second;
    return std::nullopt;
  }

 private:
  std::map<absl::string_view, absl::string_view> string_values_;
  std::map<absl::string_view, int> int_values_;
};

class TestIntInput : public XdsMatcher::InputValue<int> {
 public:
  explicit TestIntInput(absl::string_view key) : key_(key) {}

  UniqueTypeName context_type() const override {
    return TestMatchContext::Type();
  }

  std::optional<int> GetValue(
      const XdsMatcher::MatchContext& context) const override {
    return DownCast<const TestMatchContext&>(context).GetIntValue(key_);
  }

 private:
  absl::string_view key_;
};

class TestStringInput : public XdsMatcher::InputValue<absl::string_view> {
 public:
  explicit TestStringInput(absl::string_view key) : key_(key) {}

  UniqueTypeName context_type() const override {
    return TestMatchContext::Type();
  }

  std::optional<absl::string_view> GetValue(
      const XdsMatcher::MatchContext& context) const override {
    return DownCast<const TestMatchContext&>(context).GetStringValue(key_);
  }

 private:
  absl::string_view key_;
};

class TestIntMatcher : public XdsMatcherList::InputMatcher<int> {
 public:
  explicit TestIntMatcher(int expected) : expected_(expected) {}

  bool Match(const std::optional<int>& input) const override {
    return input.has_value() && *input == expected_;
  }

 private:
  int expected_;
};

class TestAction : public XdsMatcher::Action {
 public:
  absl::string_view type_url() const override { return "whatever"; }
};

TEST(SinglePredicate, CreateFailsWithMismatchedTypes) {
  EXPECT_EQ(XdsMatcherList::CreateSinglePredicate(
                std::make_unique<TestStringInput>("foo"),
                std::make_unique<TestIntMatcher>(1)),
            nullptr);
}

TEST(SinglePredicate, Matches) {
  auto predicate = XdsMatcherList::CreateSinglePredicate(
      std::make_unique<TestIntInput>("foo"),
      std::make_unique<TestIntMatcher>(1));
  ASSERT_NE(predicate, nullptr);
  TestMatchContext context({}, {{"foo", 1}});
  EXPECT_TRUE(predicate->Match(context));
}

TEST(SinglePredicate, DoesNotMatch) {
  auto predicate = XdsMatcherList::CreateSinglePredicate(
      std::make_unique<TestIntInput>("foo"),
      std::make_unique<TestIntMatcher>(2));
  ASSERT_NE(predicate, nullptr);
  TestMatchContext context({}, {{"foo", 1}});
  EXPECT_FALSE(predicate->Match(context));
}

TEST(XdsMatcherList, SinglePredicate) {
  auto predicate = XdsMatcherList::CreateSinglePredicate(
      std::make_unique<TestIntInput>("foo"),
      std::make_unique<TestIntMatcher>(1));
  auto action = std::make_unique<TestAction>();
  auto action_ptr = action.get();
  std::vector<XdsMatcherList::FieldMatcher> field_matchers;
  field_matchers.emplace_back(std::move(predicate),
                              XdsMatcher::OnMatch{std::move(action)});
  auto matcher =
      std::make_unique<XdsMatcherList>(std::move(field_matchers), std::nullopt);
  TestMatchContext context({}, {{"foo", 1}});
  XdsMatcher::Result result;
  auto found_match = matcher->FindMatches(context, result);
  EXPECT_TRUE(found_match);
  EXPECT_THAT(result, ::testing::ElementsAre(action_ptr));
}

}  // namespace
}  // namespace testing
}  // namespace grpc_core

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  grpc::testing::TestEnvironment env(&argc, argv);
  return RUN_ALL_TESTS();
}
