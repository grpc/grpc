// Copyright 2022 gRPC authors.
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

#include "src/core/lib/resolver/resolver_attributes.h"

#include <memory>
#include <string>

#include <gtest/gtest.h>

#include "absl/memory/memory.h"
#include "absl/strings/str_cat.h"

#include "src/core/lib/gpr/useful.h"
#include "test/core/util/test_config.h"

namespace grpc_core {

class IntegerAttribute : public ResolverAttributeMap::AttributeInterface {
 public:
  explicit IntegerAttribute(int value) : value_(value) {}
  static const char* Type() { return "integer_attribute"; }
  const char* type() const override { return Type(); }
  std::unique_ptr<AttributeInterface> Copy() const override {
    return absl::make_unique<IntegerAttribute>(value_);
  }
  int Compare(const AttributeInterface* other) const override {
    auto* other_attribute = static_cast<const IntegerAttribute*>(other);
    return QsortCompare(value_, other_attribute->value_);
  }
  std::string ToString() const override {
    return absl::StrCat("{value=", value_, "}");
  }
  int value() const { return value_; }
  static const IntegerAttribute* GetFromMap(const ResolverAttributeMap& map) {
    return static_cast<const IntegerAttribute*>(map.Get(Type()));
  }

 private:
  int value_;
};

class StringAttribute : public ResolverAttributeMap::AttributeInterface {
 public:
  explicit StringAttribute(std::string value) : value_(std::move(value)) {}
  static const char* Type() { return "string_attribute"; }
  const char* type() const override { return Type(); }
  std::unique_ptr<AttributeInterface> Copy() const override {
    return absl::make_unique<StringAttribute>(value_);
  }
  int Compare(const AttributeInterface* other) const override {
    auto* other_attribute = static_cast<const StringAttribute*>(other);
    return QsortCompare(value_, other_attribute->value_);
  }
  std::string ToString() const override {
    return absl::StrCat("{value=", value_, "}");
  }
  const std::string& value() const { return value_; }
  static const StringAttribute* GetFromMap(const ResolverAttributeMap& map) {
    return static_cast<const StringAttribute*>(map.Get(Type()));
  }

 private:
  std::string value_;
};

TEST(ResolverAttributeMapTest, SetAndGet) {
  ResolverAttributeMap map;
  // No attributes to start with.
  EXPECT_EQ(nullptr, IntegerAttribute::GetFromMap(map));
  EXPECT_EQ(nullptr, StringAttribute::GetFromMap(map));
  EXPECT_TRUE(map.empty());
  // Add integer attribute.
  map.Set(absl::make_unique<IntegerAttribute>(3));
  EXPECT_FALSE(map.empty());
  ASSERT_NE(nullptr, IntegerAttribute::GetFromMap(map));
  EXPECT_EQ(3, IntegerAttribute::GetFromMap(map)->value());
  // Add string attribute.
  map.Set(absl::make_unique<StringAttribute>("foo"));
  ASSERT_NE(nullptr, StringAttribute::GetFromMap(map));
  EXPECT_EQ("foo", StringAttribute::GetFromMap(map)->value());
  // Integer attribute should still be present.
  ASSERT_NE(nullptr, IntegerAttribute::GetFromMap(map));
  EXPECT_EQ(3, IntegerAttribute::GetFromMap(map)->value());
}

TEST(ResolverAttributeMapTest, Remove) {
  ResolverAttributeMap map;
  // Add both integer and string attributes.
  map.Set(absl::make_unique<IntegerAttribute>(3));
  map.Set(absl::make_unique<StringAttribute>("foo"));
  ASSERT_NE(nullptr, IntegerAttribute::GetFromMap(map));
  EXPECT_EQ(3, IntegerAttribute::GetFromMap(map)->value());
  ASSERT_NE(nullptr, StringAttribute::GetFromMap(map));
  EXPECT_EQ("foo", StringAttribute::GetFromMap(map)->value());
  // Remove integer.
  map.Remove(IntegerAttribute::Type());
  EXPECT_EQ(nullptr, IntegerAttribute::GetFromMap(map));
  // String is still present.
  ASSERT_NE(nullptr, StringAttribute::GetFromMap(map));
  EXPECT_EQ("foo", StringAttribute::GetFromMap(map)->value());
  // Remove string.
  map.Remove(StringAttribute::Type());
  EXPECT_EQ(nullptr, StringAttribute::GetFromMap(map));
  EXPECT_TRUE(map.empty());
}

TEST(ResolverAttributeMapTest, Replace) {
  ResolverAttributeMap map;
  // Add integer attribute.
  map.Set(absl::make_unique<IntegerAttribute>(3));
  ASSERT_NE(nullptr, IntegerAttribute::GetFromMap(map));
  EXPECT_EQ(3, IntegerAttribute::GetFromMap(map)->value());
  // Re-add the same attribute with a different value.
  map.Set(absl::make_unique<IntegerAttribute>(5));
  ASSERT_NE(nullptr, IntegerAttribute::GetFromMap(map));
  EXPECT_EQ(5, IntegerAttribute::GetFromMap(map)->value());
}

TEST(ResolverAttributeMapTest, CopyConstruction) {
  ResolverAttributeMap map1;
  map1.Set(absl::make_unique<IntegerAttribute>(3));
  map1.Set(absl::make_unique<StringAttribute>("foo"));
  ResolverAttributeMap map2(map1);
  ASSERT_NE(nullptr, IntegerAttribute::GetFromMap(map1));
  EXPECT_EQ(3, IntegerAttribute::GetFromMap(map1)->value());
  ASSERT_NE(nullptr, StringAttribute::GetFromMap(map1));
  EXPECT_EQ("foo", StringAttribute::GetFromMap(map1)->value());
  ASSERT_NE(nullptr, IntegerAttribute::GetFromMap(map2));
  EXPECT_EQ(3, IntegerAttribute::GetFromMap(map2)->value());
  ASSERT_NE(nullptr, StringAttribute::GetFromMap(map2));
  EXPECT_EQ("foo", StringAttribute::GetFromMap(map2)->value());
}

TEST(ResolverAttributeMapTest, CopyAssignment) {
  ResolverAttributeMap map1;
  map1.Set(absl::make_unique<IntegerAttribute>(3));
  map1.Set(absl::make_unique<StringAttribute>("foo"));
  ResolverAttributeMap map2 = map1;
  ASSERT_NE(nullptr, IntegerAttribute::GetFromMap(map1));
  EXPECT_EQ(3, IntegerAttribute::GetFromMap(map1)->value());
  ASSERT_NE(nullptr, StringAttribute::GetFromMap(map1));
  EXPECT_EQ("foo", StringAttribute::GetFromMap(map1)->value());
  ASSERT_NE(nullptr, IntegerAttribute::GetFromMap(map2));
  EXPECT_EQ(3, IntegerAttribute::GetFromMap(map2)->value());
  ASSERT_NE(nullptr, StringAttribute::GetFromMap(map2));
  EXPECT_EQ("foo", StringAttribute::GetFromMap(map2)->value());
}

TEST(ResolverAttributeMapTest, MoveConstruction) {
  ResolverAttributeMap map1;
  map1.Set(absl::make_unique<IntegerAttribute>(3));
  map1.Set(absl::make_unique<StringAttribute>("foo"));
  ResolverAttributeMap map2(std::move(map1));
  // NOLINTNEXTLINE(bugprone-use-after-move)
  EXPECT_EQ(nullptr, IntegerAttribute::GetFromMap(map1));
  // NOLINTNEXTLINE(bugprone-use-after-move)
  EXPECT_EQ(nullptr, StringAttribute::GetFromMap(map1));
  ASSERT_NE(nullptr, IntegerAttribute::GetFromMap(map2));
  EXPECT_EQ(3, IntegerAttribute::GetFromMap(map2)->value());
  ASSERT_NE(nullptr, StringAttribute::GetFromMap(map2));
  EXPECT_EQ("foo", StringAttribute::GetFromMap(map2)->value());
}

TEST(ResolverAttributeMapTest, MoveAssignment) {
  ResolverAttributeMap map1;
  map1.Set(absl::make_unique<IntegerAttribute>(3));
  map1.Set(absl::make_unique<StringAttribute>("foo"));
  ResolverAttributeMap map2 = std::move(map1);
  // NOLINTNEXTLINE(bugprone-use-after-move)
  EXPECT_EQ(nullptr, IntegerAttribute::GetFromMap(map1));
  // NOLINTNEXTLINE(bugprone-use-after-move)
  EXPECT_EQ(nullptr, StringAttribute::GetFromMap(map1));
  ASSERT_NE(nullptr, IntegerAttribute::GetFromMap(map2));
  EXPECT_EQ(3, IntegerAttribute::GetFromMap(map2)->value());
  ASSERT_NE(nullptr, StringAttribute::GetFromMap(map2));
  EXPECT_EQ("foo", StringAttribute::GetFromMap(map2)->value());
}

TEST(ResolverAttributeMapTest, ToString) {
  ResolverAttributeMap map;
  EXPECT_EQ("{}", map.ToString());
  // Add integer attribute.
  map.Set(absl::make_unique<IntegerAttribute>(3));
  EXPECT_EQ("{integer_attribute={value=3}}", map.ToString());
  // Add string attribute.
  map.Set(absl::make_unique<StringAttribute>("foo"));
  EXPECT_EQ("{integer_attribute={value=3}, string_attribute={value=foo}}",
            map.ToString());
}

TEST(ResolverAttributeMapTest, Compare) {
  // Equal maps.
  ResolverAttributeMap map1;
  map1.Set(absl::make_unique<IntegerAttribute>(3));
  ResolverAttributeMap map2;
  map2.Set(absl::make_unique<IntegerAttribute>(3));
  EXPECT_EQ(0, map1.Compare(map2));
  // map2 value is greater than map1.
  map2.Set(absl::make_unique<IntegerAttribute>(4));
  EXPECT_GT(0, map1.Compare(map2));
  EXPECT_LT(0, map2.Compare(map1));
  // map2 value is less than map1.
  map2.Set(absl::make_unique<IntegerAttribute>(2));
  EXPECT_LT(0, map1.Compare(map2));
  EXPECT_GT(0, map2.Compare(map1));
  // map3 has a different attribute than map1.
  ResolverAttributeMap map3;
  map3.Set(absl::make_unique<StringAttribute>("foo"));
  int c1 = map1.Compare(map3);
  int c2 = map3.Compare(map1);
  EXPECT_NE(0, c1);
  EXPECT_NE(0, c2);
  EXPECT_EQ(c1, -c2);  // Should be opposite signs.
  // map3 is a superset of map1.
  map3.Set(absl::make_unique<IntegerAttribute>(3));
  c1 = map1.Compare(map3);
  c2 = map3.Compare(map1);
  EXPECT_NE(0, c1);
  EXPECT_NE(0, c2);
  EXPECT_EQ(c1, -c2);  // Should be opposite signs.
}

}  // namespace grpc_core

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  grpc::testing::TestEnvironment env(&argc, argv);
  return RUN_ALL_TESTS();
}
