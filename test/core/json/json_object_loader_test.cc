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

#include "src/core/lib/json/json_object_loader.h"

#include <gtest/gtest.h>

#include "absl/strings/str_join.h"

namespace grpc_core {
namespace {

template <typename Loader>
auto Parse(const std::string& json, const Loader* loader, ErrorList* errors) ->
    typename Loader::ResultType {
  grpc_error_handle error = GRPC_ERROR_NONE;
  auto parsed = Json::Parse(json, &error);
  EXPECT_EQ(error, GRPC_ERROR_NONE) << " parsing: " << json;
  EXPECT_EQ(parsed.type(), Json::Type::OBJECT) << " parsing: " << json;
  return loader->Load(parsed.object_value(), errors);
}

struct TestStruct1 {
  int32_t a = 0;
  int32_t b = 1;
  uint32_t c = 2;
  std::string x;
};

const auto kTestStruct1Loader = JsonObjectLoader<TestStruct1>()
                                    .Field("a", &TestStruct1::a)
                                    .OptionalField("b", &TestStruct1::b)
                                    .OptionalField("c", &TestStruct1::c)
                                    .Field("x", &TestStruct1::x)
                                    .Finish();

struct TestStruct2 {
  std::vector<TestStruct1> a;
  std::vector<int32_t> b;
};

const auto kTestStruct2Loader =
    JsonObjectLoader<TestStruct2>()
        .Field("a", &TestStruct2::a, &kTestStruct1Loader)
        .Field("b", &TestStruct2::b)
        .Finish();

TEST(JsonObjectLoaderTest, LoadTestStruct1) {
  {
    ErrorList errors;
    auto s = Parse("{\"a\":1,\"b\":2,\"c\":3,\"x\":\"foo\"}",
                   &kTestStruct1Loader, &errors);
    EXPECT_EQ(s.a, 1);
    EXPECT_EQ(s.b, 2);
    EXPECT_EQ(s.c, 3);
    EXPECT_EQ(s.x, "foo");
    EXPECT_EQ(errors.errors().size(), 0);
  }
  {
    ErrorList errors;
    auto s = Parse("{\"a\":7, \"x\":\"bar\"}", &kTestStruct1Loader, &errors);
    EXPECT_EQ(s.a, 7);
    EXPECT_EQ(s.b, 1);
    EXPECT_EQ(s.c, 2);
    EXPECT_EQ(s.x, "bar");
    EXPECT_EQ(errors.errors().size(), 0);
  }
  {
    ErrorList errors;
    auto s = Parse("{\"b\":\"foo\",\"x\":42}", &kTestStruct1Loader, &errors);
    EXPECT_EQ(absl::StrJoin(errors.errors(), "\n"),
              "field:.a error:does not exist.\n"
              "field:.b error:is not a number.\n"
              "field:.x error:is not a string.");
  }
}

TEST(JsonObjectLoaderTest, LoadTestStruct2) {
  {
    ErrorList errors;
    auto s =
        Parse("{\"a\":[{\"a\":1,\"b\":2,\"c\":3,\"x\":\"foo\"}],\"b\":[1,2,3]}",
              &kTestStruct2Loader, &errors);
    EXPECT_EQ(s.a.size(), 1);
    EXPECT_EQ(s.a[0].a, 1);
    EXPECT_EQ(s.a[0].b, 2);
    EXPECT_EQ(s.a[0].c, 3);
    EXPECT_EQ(s.a[0].x, "foo");
    EXPECT_EQ(s.b.size(), 3);
    EXPECT_EQ(s.b[0], 1);
    EXPECT_EQ(s.b[1], 2);
    EXPECT_EQ(s.b[2], 3);
    EXPECT_EQ(errors.errors().size(), 0);
  }
  {
    ErrorList errors;
    auto s = Parse("{\"a\":[{\"a\":7, \"x\":\"bar\"}],\"b\":[1,2,3]}",
                   &kTestStruct2Loader, &errors);
    EXPECT_EQ(s.a.size(), 1);
    EXPECT_EQ(s.a[0].a, 7);
    EXPECT_EQ(s.a[0].b, 1);
    EXPECT_EQ(s.a[0].c, 2);
    EXPECT_EQ(s.a[0].x, "bar");
    EXPECT_EQ(s.b.size(), 3);
    EXPECT_EQ(s.b[0], 1);
    EXPECT_EQ(s.b[1], 2);
    EXPECT_EQ(s.b[2], 3);
    EXPECT_EQ(errors.errors().size(), 0);
  }
  {
    ErrorList errors;
    auto s = Parse("{\"a\":[{\"a\":\"7\", \"x\":\"bar\"}],\"b\":[1,\"2\",3]}",
                   &kTestStruct2Loader, &errors);
    EXPECT_EQ(absl::StrJoin(errors.errors(), "\n"),
              "field:.a[0].a error:is not a number.\n"
              "field:.b[1] error:is not a number.");
  }
}

}  // namespace
}  // namespace grpc_core

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
