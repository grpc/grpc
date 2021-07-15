// Copyright 2021 gRPC authors.
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

#include "src/core/lib/gprpp/table.h"
#include <gtest/gtest.h>
#include <string>

namespace grpc_core {
namespace testing {

TEST(TypedTable, NoOp) {
  TypedTable<int, double, std::string> t;
  EXPECT_EQ(t.get<int>(), nullptr);
  EXPECT_EQ(t.get<double>(), nullptr);
  EXPECT_EQ(t.get<std::string>(), nullptr);
}

TEST(TypedTable, SetTheThings) {
  TypedTable<int, double, std::string> t;
  t.set<int>(3);
  t.set<double>(2.9);
  t.set<std::string>("Hello world!");
  EXPECT_EQ(*t.get<int>(), 3);
  EXPECT_EQ(*t.get<double>(), 2.9);
  EXPECT_EQ(*t.get<std::string>(), "Hello world!");
}

TEST(TypedTable, GetDefault) {
  TypedTable<int, double, std::string> t;
  EXPECT_EQ(*t.get_or_create<std::string>(), "");
  EXPECT_EQ(*t.get_or_create<double>(), 0.0);
  EXPECT_EQ(*t.get_or_create<int>(), 0);
}

TEST(TypedTable, Copy) {
  TypedTable<int, std::string> t;
  t.set<std::string>("abcdefghijklmnopqrstuvwxyz");
  EXPECT_EQ(*t.get<std::string>(), "abcdefghijklmnopqrstuvwxyz");
  EXPECT_EQ(t.get<int>(), nullptr);
  TypedTable<int, std::string> u(t);
  EXPECT_EQ(*u.get<std::string>(), "abcdefghijklmnopqrstuvwxyz");
  EXPECT_EQ(*t.get<std::string>(), "abcdefghijklmnopqrstuvwxyz");
  EXPECT_EQ(t.get<int>(), nullptr);
  EXPECT_EQ(u.get<int>(), nullptr);
  u.set<std::string>("hello");
  EXPECT_EQ(*u.get<std::string>(), "hello");
  EXPECT_EQ(*t.get<std::string>(), "abcdefghijklmnopqrstuvwxyz");
  t = u;
  EXPECT_EQ(*u.get<std::string>(), "hello");
  EXPECT_EQ(*t.get<std::string>(), "hello");
}

}  // namespace testing
}  // namespace grpc_core

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
