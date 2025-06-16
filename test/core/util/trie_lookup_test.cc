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

#include <optional>

#include "absl/container/flat_hash_map.h"
#include "absl/types/optional.h"
#include "gtest/gtest.h"

namespace grpc_core {

TEST(TrieLookupTest, Empty) {
  TrieLookupTree<int> tree;
  EXPECT_EQ(tree.lookup("Hello"), absl::nullopt);
}

TEST(TrieLookupTest, Add) {
  TrieLookupTree<int> tree;
  tree.addNode("Hello", 1);
  EXPECT_EQ(tree.lookup("Hello"), 1);
}

TEST(TrieLookupTest, Overwrite) {
  TrieLookupTree<int> tree;
  tree.addNode("Hello", 1);
  EXPECT_EQ(tree.lookup("Hello"), 1);
  tree.addNode("Hello", 2);
  EXPECT_EQ(tree.lookup("Hello"), 2);
}

TEST(TrieLookupTest, OverwriteFalse) {
  TrieLookupTree<int> tree;
  tree.addNode("Hello", 1);
  EXPECT_EQ(tree.lookup("Hello"), 1);
  EXPECT_FALSE(tree.addNode("Hello", 2, false));
  EXPECT_EQ(tree.lookup("Hello"), 1);
}

TEST(TrieLookupTest, Lookup) {
  TrieLookupTree<int> tree;
  tree.addNode("Hello", 1);
  tree.addNode("World", 2);
  EXPECT_EQ(tree.lookup("Hello"), 1);
  EXPECT_EQ(tree.lookup("World"), 2);
}

TEST(TrieLookupTest, LookupPrefix) {
  TrieLookupTree<int> tree;
  tree.addNode("Hello", 1);
  tree.addNode("World", 2);
  tree.addNode("Hello/World", 3);
  EXPECT_EQ(tree.lookup("Hello"), 1);
  EXPECT_EQ(tree.lookup("Hello/World"), 3);
  EXPECT_EQ(tree.lookup("Hel"), absl::nullopt);
  EXPECT_EQ(tree.lookup("Wor"), absl::nullopt);
  EXPECT_EQ(tree.lookup(""), absl::nullopt);
  EXPECT_EQ(tree.lookup("Foo"), absl::nullopt);
}

TEST(TrieLookupTest, LookupLongestPrefix) {
  TrieLookupTree<int> tree;
  tree.addNode("Hello", 1);
  tree.addNode("Hello/World", 2);
  EXPECT_EQ(tree.lookupLongestPrefix("Hello"), 1);
  EXPECT_EQ(tree.lookupLongestPrefix("Hello/Boq"), 1);
  EXPECT_EQ(tree.lookupLongestPrefix("Hello/Wor"), 1);
  EXPECT_EQ(tree.lookupLongestPrefix("Hello/World"), 2);
  EXPECT_EQ(tree.lookupLongestPrefix("Hello/World/Foo"), 2);
  EXPECT_EQ(tree.lookupLongestPrefix("Hel"), std::nullopt);
  EXPECT_EQ(tree.lookupLongestPrefix("Foo"), absl::nullopt);
  EXPECT_EQ(tree.lookupLongestPrefix(""), absl::nullopt);
}

}  // namespace grpc_core

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
