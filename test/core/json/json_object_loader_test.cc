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

template <typename T>
T Parse(const std::string& json, ErrorList* errors) {
  grpc_error_handle error = GRPC_ERROR_NONE;
  auto parsed = Json::Parse(json, &error);
  EXPECT_EQ(error, GRPC_ERROR_NONE)
      << " parsing: " << json << "  error: " << grpc_error_std_string(error);
  return LoadFromJson<T>(parsed, errors);
}

struct TestStruct1 {
  int32_t a = 0;
  int32_t b = 1;
  uint32_t c = 2;
  std::string x;
  Duration d;

  static const JsonLoaderInterface* JsonLoader() {
    static const auto loader = JsonObjectLoader<TestStruct1>()
                                   .Field("a", &TestStruct1::a)
                                   .OptionalField("b", &TestStruct1::b)
                                   .OptionalField("c", &TestStruct1::c)
                                   .Field("x", &TestStruct1::x)
                                   .OptionalField("d", &TestStruct1::d)
                                   .Finish();
    return &loader;
  }
};

struct TestStruct2 {
  std::vector<TestStruct1> a;
  std::vector<int32_t> b;

  static const JsonLoaderInterface* JsonLoader() {
    static const auto loader = JsonObjectLoader<TestStruct2>()
                                   .Field("a", &TestStruct2::a)
                                   .Field("b", &TestStruct2::b)
                                   .Finish();
    return &loader;
  }
};

struct TestStruct3 {
  std::map<std::string, TestStruct1> a;
  std::map<std::string, int32_t> b;

  static const JsonLoaderInterface* JsonLoader() {
    static const auto loader = JsonObjectLoader<TestStruct3>()
                                   .Field("a", &TestStruct3::a)
                                   .Field("b", &TestStruct3::b)
                                   .Finish();
    return &loader;
  }
};

TEST(JsonObjectLoaderTest, LoadTestStruct1) {
  {
    ErrorList errors;
    auto s =
        Parse<TestStruct1>("{\"a\":1,\"b\":2,\"c\":3,\"x\":\"foo\"}", &errors);
    EXPECT_EQ(s.a, 1);
    EXPECT_EQ(s.b, 2);
    EXPECT_EQ(s.c, 3);
    EXPECT_EQ(s.x, "foo");
    EXPECT_EQ(errors.errors().size(), 0);
  }
  {
    ErrorList errors;
    auto s = Parse<TestStruct1>("{\"a\":7, \"x\":\"bar\"}", &errors);
    EXPECT_EQ(s.a, 7);
    EXPECT_EQ(s.b, 1);
    EXPECT_EQ(s.c, 2);
    EXPECT_EQ(s.x, "bar");
    EXPECT_EQ(errors.errors().size(), 0);
  }
  {
    ErrorList errors;
    auto s = Parse<TestStruct1>("{\"b\":\"foo\",\"x\":42}", &errors);
    EXPECT_EQ(absl::StrJoin(errors.errors(), "\n"),
              "field:.a error:does not exist.\n"
              "field:.b error:is not a number.\n"
              "field:.x error:is not a string.");
  }
}

TEST(JsonObjectLoaderTest, LoadTestStruct2) {
  {
    ErrorList errors;
    auto s = Parse<TestStruct2>(
        "{\"a\":[{\"a\":1,\"b\":2,\"c\":3,\"x\":\"foo\"}],\"b\":[1,2,3]}",
        &errors);
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
    auto s = Parse<TestStruct2>(
        "{\"a\":[{\"a\":7, \"x\":\"bar\"}],\"b\":[1,2,3]}", &errors);
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
    auto s = Parse<TestStruct2>(
        "{\"a\":[{\"a\":\"7\", \"x\":\"bar\"}],\"b\":[1,\"2\",3]}", &errors);
    EXPECT_EQ(absl::StrJoin(errors.errors(), "\n"),
              "field:.a[0].a error:is not a number.\n"
              "field:.b[1] error:is not a number.");
  }
}

TEST(JsonObjectLoaderTest, LoadTestStruct3) {
  {
    ErrorList errors;
    auto s = Parse<TestStruct3>(
        "{\"a\":{\"k1\":{\"a\":7, \"x\":\"bar\"}, "
        "\"k2\":{\"a\":1,\"b\":2,\"c\":3,\"x\":\"foo\"}}, "
        "\"b\":{\"k1\":1,\"k2\":2,\"k3\":3}}",
        &errors);
    EXPECT_EQ(s.a.size(), 2);
    EXPECT_EQ(s.a["k1"].a, 7);
    EXPECT_EQ(s.a["k1"].b, 1);
    EXPECT_EQ(s.a["k1"].c, 2);
    EXPECT_EQ(s.a["k1"].x, "bar");
    EXPECT_EQ(s.a["k2"].a, 1);
    EXPECT_EQ(s.a["k2"].b, 2);
    EXPECT_EQ(s.a["k2"].c, 3);
    EXPECT_EQ(s.a["k2"].x, "foo");
    EXPECT_EQ(s.b.size(), 3);
    EXPECT_EQ(s.b["k1"], 1);
    EXPECT_EQ(s.b["k2"], 2);
    EXPECT_EQ(s.b["k3"], 3);
    EXPECT_EQ(errors.errors().size(), 0);
  }
}

}  // namespace
}  // namespace grpc_core

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
