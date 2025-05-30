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

#include "src/core/util/memory_usage.h"

#include "gtest/gtest.h"

namespace grpc_core {

TEST(MemoryUsageTest, Int) { EXPECT_EQ(MemoryUsage(42), sizeof(int)); }

TEST(MemoryUsageTest, Double) { EXPECT_EQ(MemoryUsage(42.0), sizeof(double)); }

TEST(MemoryUsageTest, String) {
  EXPECT_GE(MemoryUsage(std::string("hello")),
            sizeof(std::string) + strlen("hello"));
}

TEST(MemoryUsageTest, StructOfInt) {
  struct Foo {
    int a;
    int b;
    int c;
  };
  EXPECT_GE(MemoryUsage(Foo()), sizeof(Foo));
}

TEST(MemoryUsageTest, StructOfString) {
  struct Foo {
    std::string a;
    std::string b;
    std::string c;
  };
  EXPECT_GE(MemoryUsage(Foo{"a", "b", "c"}), 3 * sizeof(std::string) + 3);
}

TEST(MemoryUsageTest, VeryAlignedStruct) {
  struct Foo {
    alignas(128) char a;
    alignas(128) char b;
  };
  EXPECT_EQ(MemoryUsage(Foo{1, 2}), sizeof(Foo));
}

TEST(MemoryUsageTest, OptionalInt) {
  EXPECT_EQ(MemoryUsage(std::optional<int>()), sizeof(std::optional<int>));
  EXPECT_EQ(MemoryUsage(std::optional<int>(42)), sizeof(std::optional<int>));
}

TEST(MemoryUsageTest, OptionalString) {
  EXPECT_EQ(MemoryUsage(std::optional<std::string>()),
            sizeof(std::optional<std::string>));
  EXPECT_GE(MemoryUsage(std::optional<std::string>("hello")),
            sizeof(std::optional<std::string>) + strlen("hello"));
}

TEST(MemoryUsageTest, Regression1) {
  struct Foo {
    struct Inner {
      uint64_t x;
      double y;
    };
    uint64_t a;
    std::optional<Inner> b;
    double c;
    std::optional<double> d;
  };
  EXPECT_EQ(MemoryUsage(Foo()), sizeof(Foo));
}

TEST(MemoryUsageTest, EscapeHatch) {
  struct Foo {
    size_t MemoryUsage() const { return 12345; }
  };
  EXPECT_EQ(MemoryUsage(Foo()), 12345);
}

TEST(MemoryUsageTest, UniquePtrInAStruct) {
  struct Foo {
    std::unique_ptr<int> a;
  };
  Foo x;
  EXPECT_EQ(MemoryUsage(x), sizeof(Foo));
  x.a.reset(new int(42));
  EXPECT_EQ(MemoryUsage(x), sizeof(Foo) + sizeof(int));
}

}  // namespace grpc_core
