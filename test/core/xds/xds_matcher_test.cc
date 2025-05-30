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

#include "src/core/xds/grpc/xds_matcher.h"

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace matcher {
namespace testing {

using ::testing::_;
using ::testing::ByMove;
using ::testing::Return;
using ::testing::StrictMock;

// --- Mock Implementations ---
class MockCustomMatchData : public CustomMatchData {
 public:
  explicit MockCustomMatchData(int val) : value(val) {}
  MOCK_METHOD(void, DoSomething, ());  // Example method
  int value;
};

template <typename DataType>
class MockPredicate : public Predicate<DataType> {
 public:
  MOCK_METHOD(bool, match, (const DataType& data), (override));
};

class MockInputMatcher : public InputMatcher {
 public:
  MOCK_METHOD(bool, match, (const MatchDataType& input), (override));
};

template <typename DataType>
class MockDataInput : public DataInput<DataType> {
 public:
  MOCK_METHOD(std::optional<MatchDataType>, GetInput,
              (const DataType& data), (override));
  MOCK_METHOD(std::string_view, typeUrl, (), (override));
};

template <typename DataType>
class MockMatcher : public Matcher<DataType> {
 public:
  MOCK_METHOD(MatchResult<DataType>, match, (const DataType& data),
              (override));
};

struct ActionState {
  bool executed = false;
  void Execute() { executed = true; }
};

// --- AndMatcher Tests ---
using AndMatcherTest = ::testing::Test;

TEST_F(AndMatcherTest, EmptyPredicateListReturnsTrue) {
  AndMatcher<int> and_matcher({});
  EXPECT_TRUE(and_matcher.match(123));
}

TEST_F(AndMatcherTest, SingleTruePredicateReturnsTrue) {
  auto mock_predicate = std::make_unique<StrictMock<MockPredicate<int>>>();
  EXPECT_CALL(*mock_predicate, match(123)).WillOnce(Return(true));
  std::vector<PredicatePtr<int>> predicates;
  predicates.push_back(std::move(mock_predicate));
  AndMatcher<int> and_matcher(std::move(predicates));
  EXPECT_TRUE(and_matcher.match(123));
}

TEST_F(AndMatcherTest, SingleFalsePredicateReturnsFalse) {
  auto mock_predicate = std::make_unique<StrictMock<MockPredicate<int>>>();
  EXPECT_CALL(*mock_predicate, match(123)).WillOnce(Return(false));
  std::vector<PredicatePtr<int>> predicates;
  predicates.push_back(std::move(mock_predicate));
  AndMatcher<int> and_matcher(std::move(predicates));
  EXPECT_FALSE(and_matcher.match(123));
}

TEST_F(AndMatcherTest, MultipleTruePredicatesReturnTrue) {
  auto mock_predicate1 = std::make_unique<StrictMock<MockPredicate<int>>>();
  auto mock_predicate2 = std::make_unique<StrictMock<MockPredicate<int>>>();
  EXPECT_CALL(*mock_predicate1, match(123)).WillOnce(Return(true));
  EXPECT_CALL(*mock_predicate2, match(123)).WillOnce(Return(true));
  std::vector<PredicatePtr<int>> predicates;
  predicates.push_back(std::move(mock_predicate1));
  predicates.push_back(std::move(mock_predicate2));
  AndMatcher<int> and_matcher(std::move(predicates));
  EXPECT_TRUE(and_matcher.match(123));
}

TEST_F(AndMatcherTest, OneFalsePredicateInMultipleReturnsFalse) {
  auto mock_predicate1 = std::make_unique<StrictMock<MockPredicate<int>>>();
  auto mock_predicate2 = std::make_unique<StrictMock<MockPredicate<int>>>();
  auto mock_predicate3 = std::make_unique<StrictMock<MockPredicate<int>>>();
  EXPECT_CALL(*mock_predicate1, match(123)).WillOnce(Return(true));
  EXPECT_CALL(*mock_predicate2, match(123)).WillOnce(Return(false));
  // mock_predicate3.match should not be called due to short-circuiting
  std::vector<PredicatePtr<int>> predicates;
  predicates.push_back(std::move(mock_predicate1));
  predicates.push_back(std::move(mock_predicate2));
  predicates.push_back(std::move(mock_predicate3));
  AndMatcher<int> and_matcher(std::move(predicates));
  EXPECT_FALSE(and_matcher.match(123));
}

// --- OrMatcher Tests ---
using OrMatcherTest = ::testing::Test;

TEST_F(OrMatcherTest, EmptyPredicateListReturnsFalse) {
  OrMatcher<int> or_matcher({});
  EXPECT_FALSE(or_matcher.match(123));
}

TEST_F(OrMatcherTest, SingleTruePredicateReturnsTrue) {
  auto mock_predicate = std::make_unique<StrictMock<MockPredicate<int>>>();
  EXPECT_CALL(*mock_predicate, match(123)).WillOnce(Return(true));
  std::vector<PredicatePtr<int>> predicates;
  predicates.push_back(std::move(mock_predicate));
  OrMatcher<int> or_matcher(std::move(predicates));
  EXPECT_TRUE(or_matcher.match(123));
}

TEST_F(OrMatcherTest, SingleFalsePredicateReturnsFalse) {
  auto mock_predicate = std::make_unique<StrictMock<MockPredicate<int>>>();
  EXPECT_CALL(*mock_predicate, match(123)).WillOnce(Return(false));
  std::vector<PredicatePtr<int>> predicates;
  predicates.push_back(std::move(mock_predicate));
  OrMatcher<int> or_matcher(std::move(predicates));
  EXPECT_FALSE(or_matcher.match(123));
}

TEST_F(OrMatcherTest, MultipleFalsePredicatesReturnFalse) {
  auto mock_predicate1 = std::make_unique<StrictMock<MockPredicate<int>>>();
  auto mock_predicate2 = std::make_unique<StrictMock<MockPredicate<int>>>();
  EXPECT_CALL(*mock_predicate1, match(123)).WillOnce(Return(false));
  EXPECT_CALL(*mock_predicate2, match(123)).WillOnce(Return(false));
  std::vector<PredicatePtr<int>> predicates;
  predicates.push_back(std::move(mock_predicate1));
  predicates.push_back(std::move(mock_predicate2));
  OrMatcher<int> or_matcher(std::move(predicates));
  EXPECT_FALSE(or_matcher.match(123));
}

TEST_F(OrMatcherTest, OneTruePredicateInMultipleReturnsTrue) {
  auto mock_predicate1 = std::make_unique<StrictMock<MockPredicate<int>>>();
  auto mock_predicate2 = std::make_unique<StrictMock<MockPredicate<int>>>();
  auto mock_predicate3 = std::make_unique<StrictMock<MockPredicate<int>>>();
  EXPECT_CALL(*mock_predicate1, match(123)).WillOnce(Return(false));
  EXPECT_CALL(*mock_predicate2, match(123)).WillOnce(Return(true));
  // mock_predicate3.match should not be called due to short-circuiting
  std::vector<PredicatePtr<int>> predicates;
  predicates.push_back(std::move(mock_predicate1));
  predicates.push_back(std::move(mock_predicate2));
  predicates.push_back(std::move(mock_predicate3));
  OrMatcher<int> or_matcher(std::move(predicates));
  EXPECT_TRUE(or_matcher.match(123));
}

// --- SinglePredicate Tests ---
class SinglePredicateTest : public ::testing::Test {
 protected:
  std::unique_ptr<StrictMock<MockDataInput<int>>> mock_data_input_;
  std::unique_ptr<StrictMock<MockInputMatcher>> mock_input_matcher_;

  void SetUp() override {
    mock_data_input_ = std::make_unique<StrictMock<MockDataInput<int>>>();
    mock_input_matcher_ = std::make_unique<StrictMock<MockInputMatcher>>();
  }
};

TEST_F(SinglePredicateTest, MatchSucceeds) {
  EXPECT_CALL(*mock_data_input_, GetInput(123))
      .WillOnce(Return(MatchDataType("test_input")));
  EXPECT_CALL(*mock_input_matcher_, match(MatchDataType("test_input")))
      .WillOnce(Return(true));

  SinglePredicate<int> predicate(std::move(mock_data_input_),
                                 std::move(mock_input_matcher_));
  EXPECT_TRUE(predicate.match(123));
}

TEST_F(SinglePredicateTest, InputMatcherReturnsFalse) {
  EXPECT_CALL(*mock_data_input_, GetInput(123))
      .WillOnce(Return(MatchDataType("test_input")));
  EXPECT_CALL(*mock_input_matcher_, match(MatchDataType("test_input")))
      .WillOnce(Return(false));

  SinglePredicate<int> predicate(std::move(mock_data_input_),
                                 std::move(mock_input_matcher_));
  EXPECT_FALSE(predicate.match(123));
}

TEST_F(SinglePredicateTest, DataInputReturnsNullopt) {
  EXPECT_CALL(*mock_data_input_, GetInput(123)).WillOnce(Return(std::nullopt));
  // mock_input_matcher_.match should not be called

  SinglePredicate<int> predicate(std::move(mock_data_input_),
                                 std::move(mock_input_matcher_));
  EXPECT_FALSE(predicate.match(123));
}

TEST_F(SinglePredicateTest, NullDataInputReturnsFalse) {
  SinglePredicate<int> predicate(nullptr, std::move(mock_input_matcher_));
  EXPECT_FALSE(predicate.match(123));
}

TEST_F(SinglePredicateTest, NullInputMatcherReturnsFalse) {
  SinglePredicate<int> predicate(std::move(mock_data_input_), nullptr);
  EXPECT_FALSE(predicate.match(123));
}

TEST_F(SinglePredicateTest, MatchWithCustomMatchData) {
  auto custom_data = std::make_shared<MockCustomMatchData>(42);
  EXPECT_CALL(*mock_data_input_, GetInput(123))
      .WillOnce(Return(MatchDataType(custom_data)));
  EXPECT_CALL(*mock_input_matcher_, match(MatchDataType(custom_data)))
      .WillOnce(Return(true));

  SinglePredicate<int> predicate(std::move(mock_data_input_),
                                 std::move(mock_input_matcher_));
  EXPECT_TRUE(predicate.match(123));
}

TEST_F(SinglePredicateTest, MatchWithMonostate) {
  EXPECT_CALL(*mock_data_input_, GetInput(123))
      .WillOnce(Return(MatchDataType(std::monostate{})));
  EXPECT_CALL(*mock_input_matcher_, match(MatchDataType(std::monostate{})))
      .WillOnce(Return(true));

  SinglePredicate<int> predicate(std::move(mock_data_input_),
                                 std::move(mock_input_matcher_));
  EXPECT_TRUE(predicate.match(123));
}

// --- MatcherList Tests ---
class MatcherListTest : public ::testing::Test {
 protected:
  MatcherList<int> matcher_list_;
  ActionState action_state1_;
  ActionState action_state2_;
  ActionState no_match_action_state_;

  ExecuteActionCb MakeExecuteCb(ActionState& state) {
    return [&state]() { state.Execute(); };
  }
};

TEST_F(MatcherListTest, EmptyListNoOnNoMatchReturnsFalse) {
  auto result = matcher_list_.match(123);
  EXPECT_FALSE(result.result);
}

TEST_F(MatcherListTest, EmptyListWithOnNoMatchActionCb) {
  OnMatch<int> on_no_match;
  on_no_match.action_ = MakeExecuteCb(no_match_action_state_);
  on_no_match.keepMatching = false; // Does not matter for onNoMatch
  matcher_list_.setOnNoMatch(std::move(on_no_match));

  auto result = matcher_list_.match(123);
  EXPECT_TRUE(result.result);
  EXPECT_TRUE(no_match_action_state_.executed);
}

TEST_F(MatcherListTest, EmptyListWithOnNoMatchMatcher) {
  auto mock_sub_matcher = std::make_unique<StrictMock<MockMatcher<int>>>();
  EXPECT_CALL(*mock_sub_matcher, match(123))
      .WillOnce(Return(MatchResult<int>{true}));

  OnMatch<int> on_no_match;
  on_no_match.action_ = std::move(mock_sub_matcher);
  on_no_match.keepMatching = false;
  matcher_list_.setOnNoMatch(std::move(on_no_match));

  auto result = matcher_list_.match(123);
  EXPECT_TRUE(result.result);
}

TEST_F(MatcherListTest, FirstPredicateMatchesKeepMatchingFalse) {
  auto mock_predicate = std::make_unique<StrictMock<MockPredicate<int>>>();
  EXPECT_CALL(*mock_predicate, match(123)).WillOnce(Return(true));

  OnMatch<int> on_match;
  on_match.action_ = MakeExecuteCb(action_state1_);
  on_match.keepMatching = false;

  matcher_list_.addMatcher(std::move(mock_predicate), std::move(on_match));

  auto result = matcher_list_.match(123);
  EXPECT_TRUE(result.result);
  EXPECT_TRUE(action_state1_.executed);
}

TEST_F(MatcherListTest, FirstPredicateMatchesKeepMatchingTrueNoOtherMatches) {
  auto mock_predicate1 = std::make_unique<StrictMock<MockPredicate<int>>>();
  EXPECT_CALL(*mock_predicate1, match(123)).WillOnce(Return(true));
  OnMatch<int> on_match1;
  on_match1.action_ = MakeExecuteCb(action_state1_);
  on_match1.keepMatching = true;
  matcher_list_.addMatcher(std::move(mock_predicate1), std::move(on_match1));

  auto mock_predicate2 = std::make_unique<StrictMock<MockPredicate<int>>>();
  EXPECT_CALL(*mock_predicate2, match(123)).WillOnce(Return(false));
  OnMatch<int> on_match2; // Action won't be called
  on_match2.action_ = MakeExecuteCb(action_state2_);
  on_match2.keepMatching = false;
  matcher_list_.addMatcher(std::move(mock_predicate2), std::move(on_match2));

  auto result = matcher_list_.match(123);
  EXPECT_FALSE(result.result); // Because keepMatching=true and no further terminal match
  EXPECT_TRUE(action_state1_.executed);
  EXPECT_FALSE(action_state2_.executed);
}


TEST_F(MatcherListTest, FirstPredicateMatchesKeepMatchingTrueThenSecondMatchesTerminal) {
  auto mock_predicate1 = std::make_unique<StrictMock<MockPredicate<int>>>();
  EXPECT_CALL(*mock_predicate1, match(123)).WillOnce(Return(true));
  OnMatch<int> on_match1;
  on_match1.action_ = MakeExecuteCb(action_state1_);
  on_match1.keepMatching = true; // Continue matching
  matcher_list_.addMatcher(std::move(mock_predicate1), std::move(on_match1));

  auto mock_predicate2 = std::make_unique<StrictMock<MockPredicate<int>>>();
  EXPECT_CALL(*mock_predicate2, match(123)).WillOnce(Return(true));
  OnMatch<int> on_match2;
  on_match2.action_ = MakeExecuteCb(action_state2_);
  on_match2.keepMatching = false; // Terminal match
  matcher_list_.addMatcher(std::move(mock_predicate2), std::move(on_match2));

  auto result = matcher_list_.match(123);
  EXPECT_TRUE(result.result);
  EXPECT_TRUE(action_state1_.executed);
  EXPECT_TRUE(action_state2_.executed);
}


TEST_F(MatcherListTest, NoPredicateMatchesNoOnNoMatch) {
  auto mock_predicate = std::make_unique<StrictMock<MockPredicate<int>>>();
  EXPECT_CALL(*mock_predicate, match(123)).WillOnce(Return(false));
  OnMatch<int> on_match;
  on_match.action_ = MakeExecuteCb(action_state1_);
  on_match.keepMatching = false;
  matcher_list_.addMatcher(std::move(mock_predicate), std::move(on_match));

  auto result = matcher_list_.match(123);
  EXPECT_FALSE(result.result);
  EXPECT_FALSE(action_state1_.executed);
}

TEST_F(MatcherListTest, NoPredicateMatchesWithOnNoMatchActionCb) {
  auto mock_predicate = std::make_unique<StrictMock<MockPredicate<int>>>();
  EXPECT_CALL(*mock_predicate, match(123)).WillOnce(Return(false));
  OnMatch<int> on_match;
  on_match.action_ = MakeExecuteCb(action_state1_);
  on_match.keepMatching = false;
  matcher_list_.addMatcher(std::move(mock_predicate), std::move(on_match));

  OnMatch<int> on_no_match;
  on_no_match.action_ = MakeExecuteCb(no_match_action_state_);
  matcher_list_.setOnNoMatch(std::move(on_no_match));

  auto result = matcher_list_.match(123);
  EXPECT_TRUE(result.result);
  EXPECT_FALSE(action_state1_.executed);
  EXPECT_TRUE(no_match_action_state_.executed);
}

TEST_F(MatcherListTest, PredicateMatchesWithSubMatcher) {
  auto mock_predicate = std::make_unique<StrictMock<MockPredicate<int>>>();
  EXPECT_CALL(*mock_predicate, match(123)).WillOnce(Return(true));

  auto mock_sub_matcher = std::make_unique<StrictMock<MockMatcher<int>>>();
  EXPECT_CALL(*mock_sub_matcher, match(123))
      .WillOnce(Return(MatchResult<int>{true})); // Sub-matcher's result

  OnMatch<int> on_match;
  on_match.action_ = std::move(mock_sub_matcher);
  on_match.keepMatching = false; // Terminal
  matcher_list_.addMatcher(std::move(mock_predicate), std::move(on_match));

  auto result = matcher_list_.match(123);
  EXPECT_TRUE(result.result);
}

TEST_F(MatcherListTest, PredicateMatchesWithSubMatcherKeepMatchingTrue) {
  auto mock_predicate1 = std::make_unique<StrictMock<MockPredicate<int>>>();
  EXPECT_CALL(*mock_predicate1, match(123)).WillOnce(Return(true));

  auto mock_sub_matcher = std::make_unique<StrictMock<MockMatcher<int>>>();
  // Sub-matcher is called, its specific return value doesn't directly affect
  // MatcherList's return if keepMatching is true for the parent OnMatch.
  EXPECT_CALL(*mock_sub_matcher, match(123))
      .WillOnce(Return(MatchResult<int>{true}));

  OnMatch<int> on_match1;
  on_match1.action_ = std::move(mock_sub_matcher);
  on_match1.keepMatching = true; // Keep matching
  matcher_list_.addMatcher(std::move(mock_predicate1), std::move(on_match1));

  // Add a second predicate that won't match, to ensure keepMatching works
  auto mock_predicate2 = std::make_unique<StrictMock<MockPredicate<int>>>();
  EXPECT_CALL(*mock_predicate2, match(123)).WillOnce(Return(false));
  OnMatch<int> on_match2;
  on_match2.action_ = MakeExecuteCb(action_state2_);
  on_match2.keepMatching = false;
  matcher_list_.addMatcher(std::move(mock_predicate2), std::move(on_match2));


  auto result = matcher_list_.match(123);
  // Result is false because the first match had keepMatching=true, and the
  // second predicate did not match. No onNoMatch is set.
  EXPECT_FALSE(result.result);
  EXPECT_FALSE(action_state2_.executed);
}

// --- ExactMatcherTree Tests ---
// For ExactMatcherTree, DataType must be std::string due to current
// implementation details of doMatch and OnMatch::MatchAction.
class ExactMatcherTreeTest : public ::testing::Test {
 protected:
  // ExactMatcherTree is a subclass of MatcherTree.
  // We need to set map_, dataInput_, and onNoMatch_ which are protected
  // members of MatcherTree. We do this via a derived test class.
  class TestExactMatcherTree : public ExactMatcherTree<std::string> {
   public:
    void SetDataInput(DataInputPtr<std::string> data_input) {
      dataInput_ = std::move(data_input);
    }
    void SetMap(
        std::unordered_map<std::string, OnMatch<std::string>> map_param) {
      map_ = std::move(map_param);
    }
    void SetOnNoMatch(OnMatch<std::string> on_no_match) {
      onNoMatch_ = std::move(on_no_match);
    }
    // Expose protected doMatch for whitebox testing if needed, though blackbox via match() is preferred.
    // MatchResult<std::string> PublicDoMatch(const std::string& data) {
    //   return ExactMatcherTree<std::string>::doMatch(data);
    // }
  };

  TestExactMatcherTree matcher_tree_;
  std::unique_ptr<StrictMock<MockDataInput<std::string>>> mock_data_input_;
  ActionState found_action_state_;
  ActionState not_found_action_state_;

  void SetUp() override {
    mock_data_input_ =
        std::make_unique<StrictMock<MockDataInput<std::string>>>();
    matcher_tree_.SetDataInput(std::move(mock_data_input_));
  }

  ExecuteActionCb MakeExecuteCb(ActionState& state) {
    return [&state]() { state.Execute(); };
  }
};

TEST_F(ExactMatcherTreeTest, KeyFoundInMapKeepMatchingFalse) {
  // Setup DataInput
  // Need to use the member mock_data_input_ that was moved to matcher_tree_
  // Re-assign mock_data_input_ for setting expectations, then move it.
  mock_data_input_ = std::make_unique<StrictMock<MockDataInput<std::string>>>();
  EXPECT_CALL(*mock_data_input_, GetInput("input_data_for_key_extraction"))
      .WillOnce(Return(MatchDataType("test_key")));
  matcher_tree_.SetDataInput(std::move(mock_data_input_));

  // Setup map
  OnMatch<std::string> on_match;
  on_match.action_ = MakeExecuteCb(found_action_state_);
  on_match.keepMatching = false; // Terminal
  std::unordered_map<std::string, OnMatch<std::string>> map_param;
  map_param["test_key"] = std::move(on_match);
  matcher_tree_.SetMap(std::move(map_param));

  // Perform match
  // The `data` parameter to `match` is `DataType`.
  // The `data` parameter to `OnMatch::MatchAction` is also `DataType`.
  // `ExactMatcherTree::doMatch(const std::string& key)` calls `it->second.MatchAction(key)`.
  // This implies that for `ExactMatcherTree` to work as written, `DataType` must be `std::string`
  // so that `key` (a std::string) can be passed to `MatchAction(const std::string& data)`.
  auto result = matcher_tree_.match("input_data_for_key_extraction");

  EXPECT_TRUE(result.result);
  EXPECT_TRUE(found_action_state_.executed);
  EXPECT_FALSE(not_found_action_state_.executed);
}

TEST_F(ExactMatcherTreeTest, KeyFoundInMapKeepMatchingTrue) {
  mock_data_input_ = std::make_unique<StrictMock<MockDataInput<std::string>>>();
  EXPECT_CALL(*mock_data_input_, GetInput("input_data"))
      .WillOnce(Return(MatchDataType("test_key")));
  matcher_tree_.SetDataInput(std::move(mock_data_input_));

  OnMatch<std::string> on_match;
  on_match.action_ = MakeExecuteCb(found_action_state_);
  on_match.keepMatching = true; // Non-terminal from OnMatch's perspective
  std::unordered_map<std::string, OnMatch<std::string>> map_param;
  map_param["test_key"] = std::move(on_match);
  matcher_tree_.SetMap(std::move(map_param));

  // MatcherTree::match returns the result of OnMatch::MatchAction if key is found.
  // OnMatch::MatchAction returns !keepMatching.
  // So if keepMatching is true, MatchAction returns false.
  auto result = matcher_tree_.match("input_data");

  EXPECT_FALSE(result.result);
  EXPECT_TRUE(found_action_state_.executed);
}

TEST_F(ExactMatcherTreeTest, KeyNotFoundInMapNoOnNoMatch) {
  mock_data_input_ = std::make_unique<StrictMock<MockDataInput<std::string>>>();
  EXPECT_CALL(*mock_data_input_, GetInput("input_data"))
      .WillOnce(Return(MatchDataType("unknown_key")));
  matcher_tree_.SetDataInput(std::move(mock_data_input_));

  // Empty map
  matcher_tree_.SetMap({});

  auto result = matcher_tree_.match("input_data");
  EXPECT_FALSE(result.result);
  EXPECT_FALSE(found_action_state_.executed);
}

TEST_F(ExactMatcherTreeTest, KeyNotFoundInMapWithOnNoMatchActionCb) {
  mock_data_input_ = std::make_unique<StrictMock<MockDataInput<std::string>>>();
  EXPECT_CALL(*mock_data_input_, GetInput("input_data"))
      .WillOnce(Return(MatchDataType("unknown_key")));
  matcher_tree_.SetDataInput(std::move(mock_data_input_));

  matcher_tree_.SetMap({}); // Empty map

  OnMatch<std::string> on_no_match;
  on_no_match.action_ = MakeExecuteCb(not_found_action_state_);
  on_no_match.keepMatching = false; // This makes OnMatch::MatchAction return true
  matcher_tree_.SetOnNoMatch(std::move(on_no_match));

  auto result = matcher_tree_.match("input_data");
  EXPECT_TRUE(result.result); // Because onNoMatch action taken and its keepMatching=false
  EXPECT_TRUE(not_found_action_state_.executed);
}

TEST_F(ExactMatcherTreeTest, DataInputReturnsNulloptNoOnNoMatch) {
  mock_data_input_ = std::make_unique<StrictMock<MockDataInput<std::string>>>();
  EXPECT_CALL(*mock_data_input_, GetInput("input_data"))
      .WillOnce(Return(std::nullopt));
  matcher_tree_.SetDataInput(std::move(mock_data_input_));

  auto result = matcher_tree_.match("input_data");
  EXPECT_FALSE(result.result);
}

TEST_F(ExactMatcherTreeTest, DataInputReturnsNulloptWithOnNoMatch) {
  mock_data_input_ = std::make_unique<StrictMock<MockDataInput<std::string>>>();
  EXPECT_CALL(*mock_data_input_, GetInput("input_data"))
      .WillOnce(Return(std::nullopt));
  matcher_tree_.SetDataInput(std::move(mock_data_input_));

  OnMatch<std::string> on_no_match;
  on_no_match.action_ = MakeExecuteCb(not_found_action_state_);
  on_no_match.keepMatching = false; // Makes OnMatch::MatchAction return true
  matcher_tree_.SetOnNoMatch(std::move(on_no_match));

  auto result = matcher_tree_.match("input_data");
  EXPECT_TRUE(result.result);
  EXPECT_TRUE(not_found_action_state_.executed);
}

TEST_F(ExactMatcherTreeTest, DataInputReturnsNonStringNoOnNoMatch) {
  mock_data_input_ = std::make_unique<StrictMock<MockDataInput<std::string>>>();
  // Return a MatchDataType that is not a string
  EXPECT_CALL(*mock_data_input_, GetInput("input_data"))
      .WillOnce(Return(MatchDataType(std::monostate{})));
  matcher_tree_.SetDataInput(std::move(mock_data_input_));

  auto result = matcher_tree_.match("input_data");
  EXPECT_FALSE(result.result);
}

TEST_F(ExactMatcherTreeTest, DataInputReturnsNonStringWithOnNoMatch) {
  mock_data_input_ = std::make_unique<StrictMock<MockDataInput<std::string>>>();
  EXPECT_CALL(*mock_data_input_, GetInput("input_data"))
      .WillOnce(Return(MatchDataType(std::monostate{})));
   matcher_tree_.SetDataInput(std::move(mock_data_input_));

  OnMatch<std::string> on_no_match;
  on_no_match.action_ = MakeExecuteCb(not_found_action_state_);
  on_no_match.keepMatching = false;
  matcher_tree_.SetOnNoMatch(std::move(on_no_match));

  auto result = matcher_tree_.match("input_data");
  EXPECT_TRUE(result.result);
  EXPECT_TRUE(not_found_action_state_.executed);
}


TEST_F(ExactMatcherTreeTest, KeyFoundWithSubMatcherKeepMatchingFalse) {
  mock_data_input_ = std::make_unique<StrictMock<MockDataInput<std::string>>>();
  EXPECT_CALL(*mock_data_input_, GetInput("input_data"))
      .WillOnce(Return(MatchDataType("test_key")));
  matcher_tree_.SetDataInput(std::move(mock_data_input_));

  auto mock_sub_matcher = std::make_unique<StrictMock<MockMatcher<std::string>>>();
  // The sub-matcher is called with the key "test_key" because DataType is std::string
  // and ExactMatcherTree::doMatch calls OnMatch::MatchAction with the key.
  EXPECT_CALL(*mock_sub_matcher, match("test_key"))
      .WillOnce(Return(MatchResult<std::string>{true})); // Sub-matcher's own result

  OnMatch<std::string> on_match;
  on_match.action_ = std::move(mock_sub_matcher);
  on_match.keepMatching = false; // Terminal for this OnMatch

  std::unordered_map<std::string, OnMatch<std::string>> map_param;
  map_param["test_key"] = std::move(on_match);
  matcher_tree_.SetMap(std::move(map_param));

  auto result = matcher_tree_.match("input_data");
  // Result is true because on_match.keepMatching is false.
  EXPECT_TRUE(result.result);
}

TEST_F(ExactMatcherTreeTest, NullDataInput) {
    matcher_tree_.SetDataInput(nullptr);
    // Setup map (won't be reached if DataInput is null)
    OnMatch<std::string> on_match;
    on_match.action_ = MakeExecuteCb(found_action_state_);
    on_match.keepMatching = false;
    std::unordered_map<std::string, OnMatch<std::string>> map_param;
    map_param["test_key"] = std::move(on_match);
    matcher_tree_.SetMap(std::move(map_param));

    auto result = matcher_tree_.match("input_data_for_key_extraction");
    EXPECT_FALSE(result.result); // Expect false as DataInput is null
    EXPECT_FALSE(found_action_state_.executed);
}

// --- OnMatch::MatchAction Tests (Illustrative, assuming corrections) ---
// These tests are more about the assumed behavior of OnMatch::MatchAction
// as used by ExactMatcherTree.

using OnMatchActionTest = ::testing::Test;

TEST_F(OnMatchActionTest, ExecuteCbKeepMatchingFalse) {
  ActionState state;
  OnMatch<int> on_match;
  on_match.action_ = [&state]() { state.Execute(); };
  on_match.keepMatching = false;

  // Simulate how ExactMatcherTree might call it
  // We assume the variant access inside MatchAction is corrected for this test.
  // If not, this test might reflect the header's current (potentially problematic) state.
  auto result = on_match.MatchAction(123);

  EXPECT_TRUE(state.executed);
  EXPECT_TRUE(result.result); // !keepMatching
}

TEST_F(OnMatchActionTest, ExecuteCbKeepMatchingTrue) {
  ActionState state;
  OnMatch<int> on_match;
  on_match.action_ = [&state]() { state.Execute(); };
  on_match.keepMatching = true;

  auto result = on_match.MatchAction(123);

  EXPECT_TRUE(state.executed);
  EXPECT_FALSE(result.result); // !keepMatching
}

TEST_F(OnMatchActionTest, SubMatcherKeepMatchingFalse) {
  auto mock_sub_matcher = std::make_unique<StrictMock<MockMatcher<int>>>();
  EXPECT_CALL(*mock_sub_matcher, match(123))
      .WillOnce(Return(MatchResult<int>{true})); // Sub-matcher action

  OnMatch<int> on_match;
  on_match.action_ = std::move(mock_sub_matcher);
  on_match.keepMatching = false;

  auto result = on_match.MatchAction(123);
  EXPECT_TRUE(result.result); // !keepMatching
}

TEST_F(OnMatchActionTest, SubMatcherKeepMatchingTrue) {
  auto mock_sub_matcher = std::make_unique<StrictMock<MockMatcher<int>>>();
  EXPECT_CALL(*mock_sub_matcher, match(123))
      .WillOnce(Return(MatchResult<int>{false})); // Sub-matcher no action

  OnMatch<int> on_match;
  on_match.action_ = std::move(mock_sub_matcher);
  on_match.keepMatching = true;

  auto result = on_match.MatchAction(123);
  // The current OnMatch::MatchAction in the header ignores the sub-matcher's
  // result and just returns !keepMatching.
  EXPECT_FALSE(result.result); // !keepMatching
}

}  // namespace testing
}  // namespace matcher

int main(int argc, char** argv) {
  ::testing::InitGoogleMock(&argc, argv);
  return RUN_ALL_TESTS();
}
