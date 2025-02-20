//
// Copyright 2024 gRPC authors.
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

#include "src/core/util/lru_cache.h"

#include "absl/log/check.h"
#include "absl/strings/numbers.h"
#include "absl/strings/str_cat.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace grpc_core {

TEST(LruCache, Basic) {
  std::vector<int> created_list;
  auto create = [&](const std::string& key) {
    int value;
    CHECK(absl::SimpleAtoi(key, &value));
    created_list.push_back(value);
    return value;
  };
  // Create a cache with max size 5.
  LruCache<std::string, int> cache(5);
  // Insert 5 values.
  const std::array<int, 5> kOrder = {3, 1, 2, 0, 4};
  for (int i : kOrder) {
    std::string key = absl::StrCat(i);
    EXPECT_EQ(std::nullopt, cache.Get(key));
    EXPECT_EQ(i, cache.GetOrInsert(key, create));
    EXPECT_EQ(i, cache.Get(key));
  }
  EXPECT_THAT(created_list, ::testing::ElementsAreArray(kOrder));
  created_list.clear();
  // Get those same 5 values.  This should not trigger any more insertions.
  for (int i : kOrder) {
    std::string key = absl::StrCat(i);
    EXPECT_EQ(i, cache.GetOrInsert(key, create));
  }
  EXPECT_THAT(created_list, ::testing::ElementsAre());
  // Now insert new elements.
  // Each insertion should remove the least recently used element.
  const std::array<int, 5> kOrder2 = {7, 6, 8, 5, 9};
  for (size_t i = 0; i < kOrder2.size(); ++i) {
    int value2 = kOrder2[i];
    std::string key2 = absl::StrCat(value2);
    EXPECT_EQ(std::nullopt, cache.Get(key2));
    EXPECT_EQ(value2, cache.GetOrInsert(key2, create));
    EXPECT_EQ(value2, cache.Get(key2));
    int value1 = kOrder[i];
    std::string key1 = absl::StrCat(value1);
    EXPECT_EQ(std::nullopt, cache.Get(key1));
  }
  EXPECT_THAT(created_list, ::testing::ElementsAreArray(kOrder2));
}

TEST(LruCache, SetMaxSize) {
  auto create = [&](const std::string& key) {
    int value;
    CHECK(absl::SimpleAtoi(key, &value));
    return value;
  };
  // Create a cache with max size 10.
  LruCache<std::string, int> cache(10);
  // Insert 10 values.
  for (int i = 1; i <= 10; ++i) {
    std::string key = absl::StrCat(i);
    EXPECT_EQ(std::nullopt, cache.Get(key));
    EXPECT_EQ(i, cache.GetOrInsert(key, create));
    EXPECT_EQ(i, cache.Get(key));
  }
  // Set max size to 15.  All elements should still be present.
  cache.SetMaxSize(15);
  for (int i = 1; i <= 10; ++i) {
    std::string key = absl::StrCat(i);
    EXPECT_EQ(i, cache.Get(key));
  }
  // Set max size to 6.  This should remove the first 4 elements.
  cache.SetMaxSize(6);
  for (int i = 1; i <= 4; ++i) {
    std::string key = absl::StrCat(i);
    EXPECT_EQ(std::nullopt, cache.Get(key)) << i;
  }
  for (int i = 5; i <= 10; ++i) {
    std::string key = absl::StrCat(i);
    EXPECT_EQ(i, cache.Get(key));
  }
}

}  // namespace grpc_core

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
