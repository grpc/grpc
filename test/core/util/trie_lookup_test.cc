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

#include "src/core/util/trie_lookup.h"

#include <memory>
#include <string>
#include <utility>

#include "gtest/gtest.h"

namespace grpc_core {
namespace testing {

class TrieLookupTreeTest : public ::testing::Test {
 protected:
  TrieLookupTree<std::string> trie_;
};

TEST_F(TrieLookupTreeTest, AddAndExactLookup) {
  ASSERT_TRUE(trie_.AddNode("key1", "value1"));
  auto* result = trie_.Lookup("key1");
  ASSERT_NE(result, nullptr);
  EXPECT_EQ(*result, "value1");
}

TEST_F(TrieLookupTreeTest, AddMultipleNodes) {
  ASSERT_TRUE(trie_.AddNode("key1", "value1"));
  ASSERT_TRUE(trie_.AddNode("key2", "value2"));
  auto* result1 = trie_.Lookup("key1");
  ASSERT_NE(result1, nullptr);
  EXPECT_EQ(*result1, "value1");
  auto* result2 = trie_.Lookup("key2");
  ASSERT_NE(result2, nullptr);
  EXPECT_EQ(*result2, "value2");
}

TEST_F(TrieLookupTreeTest, AddPrefixKey) {
  ASSERT_TRUE(trie_.AddNode("apple", "fruit"));
  ASSERT_TRUE(trie_.AddNode("app", "software"));
  auto* result1 = trie_.Lookup("apple");
  ASSERT_NE(result1, nullptr);
  EXPECT_EQ(*result1, "fruit");
  auto* result2 = trie_.Lookup("app");
  ASSERT_NE(result2, nullptr);
  EXPECT_EQ(*result2, "software");
}

TEST_F(TrieLookupTreeTest, OverwriteValue) {
  ASSERT_TRUE(trie_.AddNode("key", "old"));
  auto* result_old = trie_.Lookup("key");
  ASSERT_NE(result_old, nullptr);
  EXPECT_EQ(*result_old, "old");
  ASSERT_TRUE(trie_.AddNode("key", "new", /*allow_overwrite=*/true));
  auto* result_new = trie_.Lookup("key");
  ASSERT_NE(result_new, nullptr);
  EXPECT_EQ(*result_new, "new");
}

TEST_F(TrieLookupTreeTest, PreventOverwrite) {
  ASSERT_TRUE(trie_.AddNode("key", "value1"));
  ASSERT_FALSE(trie_.AddNode("key", "value2", /*allow_overwrite=*/false));
  // Verify the original value is still there.
  auto* result = trie_.Lookup("key");
  ASSERT_NE(result, nullptr);
  EXPECT_EQ(*result, "value1");
}

TEST_F(TrieLookupTreeTest, LookupNonExistent) {
  trie_.AddNode("key", "value");
  EXPECT_EQ(trie_.Lookup("non-existent-key"), nullptr);
}

TEST_F(TrieLookupTreeTest, LookupPrefixWithoutValue) {
  trie_.AddNode("apple", "value");
  EXPECT_EQ(trie_.Lookup("app"), nullptr);
}

// === Tests for LookupLongestPrefix ===

TEST_F(TrieLookupTreeTest, LongestPrefixExactMatch) {
  trie_.AddNode("a/b/c", "exact_match");
  auto* result = trie_.LookupLongestPrefix("a/b/c");
  ASSERT_NE(result, nullptr);
  EXPECT_EQ(*result, "exact_match");
}

TEST_F(TrieLookupTreeTest, LongestPrefixPartialMatch) {
  trie_.AddNode("a/b", "prefix_match");
  auto* result = trie_.LookupLongestPrefix("a/b/c/d");
  ASSERT_NE(result, nullptr);
  EXPECT_EQ(*result, "prefix_match");
}

TEST_F(TrieLookupTreeTest, LongestPrefixMultipleMatches) {
  trie_.AddNode("a", "first");
  trie_.AddNode("a/b/c", "second_longest");
  trie_.AddNode("a/b", "third");
  auto* result = trie_.LookupLongestPrefix("a/b/c/d");
  ASSERT_NE(result, nullptr);
  EXPECT_EQ(*result, "second_longest");
}

TEST_F(TrieLookupTreeTest, LongestPrefixNoMatch) {
  trie_.AddNode("x/y", "some_value");
  auto* result = trie_.LookupLongestPrefix("a/b/c");
  EXPECT_EQ(result, nullptr);
}

TEST_F(TrieLookupTreeTest, LongestPrefixPathExistsButNoValue) {
  trie_.AddNode("a/b/c", "value");
  // Longest prefix of "a/b" has no value, but the root does not have one
  // either.
  auto* result = trie_.LookupLongestPrefix("a/b");
  EXPECT_EQ(result, nullptr);
}

TEST_F(TrieLookupTreeTest, GetAllPrefixMatches) {
  trie_.AddNode("a", "first");
  trie_.AddNode("a/b/c", "second");
  trie_.AddNode("a/b", "third");
  trie_.AddNode("a/e", "unrelated");
  // Match "a" should return only "a"
  auto matches_1 = trie_.GetAllPrefixMatches("a");
  EXPECT_EQ(matches_1.size(), 1);
  EXPECT_EQ(*matches_1[0], "first");
  // Match "a/b" should return "first" and "third"
  auto matches_2 = trie_.GetAllPrefixMatches("a/b");
  EXPECT_EQ(matches_2.size(), 2);
  EXPECT_EQ(*matches_2[0], "first");
  EXPECT_EQ(*matches_2[1], "third");
  // Match "ab/b/c" should return "first", "third", "second"
  auto matches_3 = trie_.GetAllPrefixMatches("a/b/c");
  EXPECT_EQ(matches_3.size(), 3);
  EXPECT_EQ(*matches_3[0], "first");
  EXPECT_EQ(*matches_3[1], "third");
  EXPECT_EQ(*matches_3[2], "second");
}

}  // namespace testing
}  // namespace grpc_core

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}