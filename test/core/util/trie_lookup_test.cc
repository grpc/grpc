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
#include <optional>
#include <string>

#include "absl/container/flat_hash_map.h"
#include "absl/types/optional.h"
#include "gtest/gtest.h"
#include "src/core/util/trie_lookup.h"  // Adjust this path to where you saved the header

namespace grpc_core {
namespace testing {

class TrieLookupTreeTest : public ::testing::Test {
 protected:
  TrieLookupTree<std::string> trie_;
};

TEST_F(TrieLookupTreeTest, AddAndExactLookup) {
  auto value = std::make_shared<std::string>("value1");
  ASSERT_TRUE(trie_.addNode("key1", value));
  auto result = trie_.lookup("key1");
  ASSERT_TRUE(result != nullptr);
  EXPECT_EQ(*result, "value1");
}

TEST_F(TrieLookupTreeTest, AddMultipleNodes) {
  auto value1 = std::make_shared<std::string>("value1");
  auto value2 = std::make_shared<std::string>("value2");
  ASSERT_TRUE(trie_.addNode("key1", value1));
  ASSERT_TRUE(trie_.addNode("key2", value2));
  auto result1 = trie_.lookup("key1");
  ASSERT_TRUE(result1 != nullptr);
  EXPECT_EQ(*result1, "value1");
  auto result2 = trie_.lookup("key2");
  ASSERT_TRUE(result2 != nullptr);
  EXPECT_EQ(*result2, "value2");
}

TEST_F(TrieLookupTreeTest, AddPrefixKey) {
  auto apple_value = std::make_shared<std::string>("fruit");
  auto app_value = std::make_shared<std::string>("software");
  ASSERT_TRUE(trie_.addNode("apple", apple_value));
  ASSERT_TRUE(trie_.addNode("app", app_value));
  auto result1 = trie_.lookup("apple");
  ASSERT_TRUE(result1 != nullptr);
  EXPECT_EQ(*result1, "fruit");
  auto result2 = trie_.lookup("app");
  ASSERT_TRUE(result2 != nullptr);
  EXPECT_EQ(*result2, "software");
}

TEST_F(TrieLookupTreeTest, OverwriteValue) {
  auto old_value = std::make_shared<std::string>("old");
  auto new_value = std::make_shared<std::string>("new");
  ASSERT_TRUE(trie_.addNode("key", old_value));
  auto result_old = trie_.lookup("key");
  ASSERT_TRUE(result_old != nullptr);
  EXPECT_EQ(*result_old, "old");
  ASSERT_TRUE(trie_.addNode("key", new_value, /*allow_overwrite=*/true));
  auto result_new = trie_.lookup("key");
  ASSERT_TRUE(result_new != nullptr);
  EXPECT_EQ(*result_new, "new");
}

TEST_F(TrieLookupTreeTest, PreventOverwrite) {
  auto value1 = std::make_shared<std::string>("value1");
  auto value2 = std::make_shared<std::string>("value2");
  ASSERT_TRUE(trie_.addNode("key", value1));
  ASSERT_FALSE(trie_.addNode("key", value2, /*allow_overwrite=*/false));
  // Verify the original value is still there.
  auto result = trie_.lookup("key");
  ASSERT_TRUE(result != nullptr);
  EXPECT_EQ(*result, "value1");
}

TEST_F(TrieLookupTreeTest, LookupNonExistent) {
  auto value = std::make_shared<std::string>("value");
  trie_.addNode("key", value);
  EXPECT_EQ(trie_.lookup("non-existent-key"), nullptr);
}

TEST_F(TrieLookupTreeTest, LookupPrefixWithoutValue) {
  auto value = std::make_shared<std::string>("value");
  trie_.addNode("apple", value);
  EXPECT_EQ(trie_.lookup("app"), nullptr);
}

// === Tests for lookupLongestPrefix ===

TEST_F(TrieLookupTreeTest, LongestPrefixExactMatch) {
  auto value = std::make_shared<std::string>("exact_match");
  trie_.addNode("a/b/c", value);
  auto result = trie_.lookupLongestPrefix("a/b/c");
  ASSERT_TRUE(result != nullptr);
  EXPECT_EQ(*result, "exact_match");
}

TEST_F(TrieLookupTreeTest, LongestPrefixPartialMatch) {
  auto value = std::make_shared<std::string>("prefix_match");
  trie_.addNode("a/b", value);
  auto result = trie_.lookupLongestPrefix("a/b/c/d");
  ASSERT_TRUE(result != nullptr);
  EXPECT_EQ(*result, "prefix_match");
}

TEST_F(TrieLookupTreeTest, LongestPrefixMultipleMatches) {
  auto value1 = std::make_shared<std::string>("first");
  auto value2 = std::make_shared<std::string>("second_longest");
  auto value3 = std::make_shared<std::string>("third");
  trie_.addNode("a", value1);
  trie_.addNode("a/b/c", value2);
  trie_.addNode("a/b", value3);
  auto result = trie_.lookupLongestPrefix("a/b/c/d");
  ASSERT_TRUE(result != nullptr);
  EXPECT_EQ(*result, "second_longest");
}

TEST_F(TrieLookupTreeTest, LongestPrefixNoMatch) {
  trie_.addNode("x/y", std::make_shared<std::string>("some_value"));
  auto result = trie_.lookupLongestPrefix("a/b/c");
  EXPECT_EQ(result, nullptr);
}

TEST_F(TrieLookupTreeTest, LongestPrefixPathExistsButNoValue) {
  trie_.addNode("a/b/c", std::make_shared<std::string>("value"));
  auto result = trie_.lookupLongestPrefix("a/b");
  EXPECT_EQ(result, nullptr);
}

}  // namespace testing
}  // namespace grpc_core

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
