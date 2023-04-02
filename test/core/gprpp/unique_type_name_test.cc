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

#include "src/core/lib/gprpp/unique_type_name.h"

#include <initializer_list>
#include <iosfwd>
#include <map>

#include "absl/strings/str_format.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace grpc_core {

// Teach gtest to print names usefully.
std::ostream& operator<<(std::ostream& os, const UniqueTypeName& name) {
  return os << absl::StrFormat("%s (%p)", name.name(), name.name().data());
}

namespace {

class Interface {
 public:
  virtual ~Interface() = default;
  virtual UniqueTypeName type() const = 0;
};

class Foo : public Interface {
 public:
  UniqueTypeName type() const override {
    static UniqueTypeName::Factory kFactory("Foo");
    return kFactory.Create();
  }
};

class Bar : public Interface {
 public:
  UniqueTypeName type() const override {
    static UniqueTypeName::Factory kFactory("Bar");
    return kFactory.Create();
  }
};

// Uses the same string as Foo.
class Foo2 : public Interface {
 public:
  UniqueTypeName type() const override {
    static UniqueTypeName::Factory kFactory("Foo");
    return kFactory.Create();
  }
};

TEST(UniqueTypeNameTest, MultipleInstancesShareName) {
  Foo foo1;
  Foo foo2;
  EXPECT_EQ(foo1.type(), foo2.type());
  EXPECT_EQ(0, foo1.type().Compare(foo2.type()));
}

TEST(UniqueTypeNameTest, DifferentImplsDoNotShareName) {
  Foo foo;
  Bar bar;
  EXPECT_NE(foo.type(), bar.type());
  EXPECT_NE(0, foo.type().Compare(bar.type()));
}

TEST(UniqueTypeNameTest, MultipleInstancesOfSameStringAreNotEqual) {
  Foo foo1;
  Foo2 foo2;
  EXPECT_NE(foo1.type(), foo2.type());
  EXPECT_NE(0, foo1.type().Compare(foo2.type()));
}

TEST(UniqueTypeNameTest, CanUseAsMapKey) {
  Foo foo;
  Bar bar;
  std::map<UniqueTypeName, int> m;
  m[foo.type()] = 1;
  m[bar.type()] = 2;
  EXPECT_THAT(m,
              ::testing::UnorderedElementsAre(::testing::Pair(foo.type(), 1),
                                              ::testing::Pair(bar.type(), 2)));
}

}  // namespace
}  // namespace grpc_core

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
