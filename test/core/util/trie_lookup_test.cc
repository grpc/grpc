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

#include <string>
#include <unordered_map>
#include <vector>

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace grpc_core {
namespace testing {

class TrieLookupTreeTest : public ::testing::Test {
 protected:
  TrieLookupTree<std::string> trie_;
};

TEST_F(TrieLookupTreeTest, AddAndExactLookup) {
  ASSERT_TRUE(trie_.AddNode("key1", "value1"));
  EXPECT_THAT(trie_.Lookup("key1"), ::testing::Pointee(std::string("value1")));
}

TEST_F(TrieLookupTreeTest, AddMultipleNodes) {
  ASSERT_TRUE(trie_.AddNode("key1", "value1"));
  ASSERT_TRUE(trie_.AddNode("key2", "value2"));
  EXPECT_THAT(trie_.Lookup("key1"), ::testing::Pointee(std::string("value1")));
  EXPECT_THAT(trie_.Lookup("key2"), ::testing::Pointee(std::string("value2")));
}

TEST_F(TrieLookupTreeTest, AddPrefixKey) {
  ASSERT_TRUE(trie_.AddNode("apple", "fruit"));
  ASSERT_TRUE(trie_.AddNode("app", "software"));
  EXPECT_THAT(trie_.Lookup("apple"), ::testing::Pointee(std::string("fruit")));
  EXPECT_THAT(trie_.Lookup("app"), ::testing::Pointee(std::string("software")));
}

TEST_F(TrieLookupTreeTest, LookupNonExistent) {
  trie_.AddNode("key", "value");
  EXPECT_THAT(trie_.Lookup("non-existent-key"), ::testing::IsNull());
}

TEST_F(TrieLookupTreeTest, LookupPrefixWithoutValue) {
  trie_.AddNode("apple", "value");
  EXPECT_THAT(trie_.Lookup("app"), ::testing::IsNull());
}

// === Tests for LookupLongestPrefix ===

TEST_F(TrieLookupTreeTest, LongestPrefixExactMatch) {
  trie_.AddNode("a/b/c", "exact_match");
  EXPECT_THAT(trie_.LookupLongestPrefix("a/b/c"),
              ::testing::Pointee(std::string("exact_match")));
}

TEST_F(TrieLookupTreeTest, LongestPrefixPartialMatch) {
  trie_.AddNode("a/b", "prefix_match");
  EXPECT_THAT(trie_.LookupLongestPrefix("a/b/c/d"),
              ::testing::Pointee(std::string("prefix_match")));
}

TEST_F(TrieLookupTreeTest, LongestPrefixMultipleMatches) {
  trie_.AddNode("a", "first");
  trie_.AddNode("a/b/c", "second_longest");
  trie_.AddNode("a/b", "third");
  EXPECT_THAT(trie_.LookupLongestPrefix("a/b/c/d"),
              ::testing::Pointee(std::string("second_longest")));
}

TEST_F(TrieLookupTreeTest, LongestPrefixNoMatch) {
  trie_.AddNode("x/y", "some_value");
  EXPECT_THAT(trie_.LookupLongestPrefix("a/b/c"), ::testing::IsNull());
}

TEST_F(TrieLookupTreeTest, LongestPrefixPathExistsButNoValue) {
  trie_.AddNode("a/b/c", "value");
  EXPECT_THAT(trie_.LookupLongestPrefix("a/b"), ::testing::IsNull());
}

TEST_F(TrieLookupTreeTest, ForEachPrefixMatch) {
  trie_.AddNode("a", "first");
  trie_.AddNode("a/b/c", "second");
  trie_.AddNode("a/b", "third");
  trie_.AddNode("a/e", "unrelated");
  // Match "a" should return only "a"
  std::vector<std::string> matches_1;
  trie_.ForEachPrefixMatch(
      "a", [&](const std::string& v) { matches_1.push_back(v); });
  EXPECT_THAT(matches_1, ::testing::ElementsAre("first"));
  // Match "a/b" should return "first" and "third"
  std::vector<std::string> matches_2;
  trie_.ForEachPrefixMatch(
      "a/b", [&](const std::string& v) { matches_2.push_back(v); });
  EXPECT_THAT(matches_2, ::testing::ElementsAre("first", "third"));
  // Match "a/b/c" should return "first", "third", "second"
  std::vector<std::string> matches_3;
  trie_.ForEachPrefixMatch(
      "a/b/c", [&](const std::string& v) { matches_3.push_back(v); });
  EXPECT_THAT(matches_3, ::testing::ElementsAre("first", "third", "second"));
}

TEST_F(TrieLookupTreeTest, ForEachTest) {
  trie_.AddNode("a", "first");
  trie_.AddNode("a/b/c", "second");
  trie_.AddNode("a/b", "third");
  trie_.AddNode("a/e", "unrelated");
  std::unordered_map<std::string, std::string> map;
  trie_.ForEach([&](const std::string_view key, const std::string& value) {
    map[std::string(key)] = value;
  });
  EXPECT_THAT(map, ::testing::UnorderedElementsAre(
                       ::testing::Pair("a", "first"),
                       ::testing::Pair("a/b/c", "second"),
                       ::testing::Pair("a/b", "third"),
                       ::testing::Pair("a/e", "unrelated")));
}

TEST_F(TrieLookupTreeTest, EqualsTest) {
  trie_.AddNode("a", "first");
  trie_.AddNode("a/b/c", "second");
  trie_.AddNode("a/b", "third");
  trie_.AddNode("a/e", "unrelated");
  TrieLookupTree<std::string> trie_new;
  trie_new.AddNode("a", "first");
  trie_new.AddNode("a/b/c", "second");
  trie_new.AddNode("a/b", "third");
  trie_new.AddNode("a/e", "unrelated");
  EXPECT_EQ(trie_, trie_new);
}

}  // namespace testing
}  // namespace grpc_core

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
