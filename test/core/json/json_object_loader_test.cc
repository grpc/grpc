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

#include <cstdint>

#include <gtest/gtest.h>

#include "absl/strings/str_join.h"

namespace grpc_core {
namespace {

template <typename T>
absl::StatusOr<T> Parse(const std::string& json) {
  auto parsed = Json::Parse(json);
  if (!parsed.ok()) return parsed.status();
  return LoadFromJson<T>(*parsed);
}

struct TestStruct1 {
  int32_t a = 0;
  int32_t b = 1;
  uint32_t c = 2;
  std::string x;
  Duration d;
  bool boolean;
  Json::Object j;
  absl::optional<int32_t> e;

  static const JsonLoaderInterface* JsonLoader() {
    static const auto* loader = JsonObjectLoader<TestStruct1>()
                                    .Field("a", &TestStruct1::a)
                                    .OptionalField("b", &TestStruct1::b)
                                    .OptionalField("c", &TestStruct1::c)
                                    .Field("x", &TestStruct1::x)
                                    .OptionalField("d", &TestStruct1::d)
                                    .OptionalField("e", &TestStruct1::e)
                                    .OptionalField("j", &TestStruct1::j)
                                    .OptionalField("boolean",
                                                   &TestStruct1::boolean)
                                    .Finish();
    return loader;
  }
};

struct TestStruct2 {
  std::vector<TestStruct1> a;
  std::vector<int32_t> b;
  TestStruct1 c;

  static const JsonLoaderInterface* JsonLoader() {
    static const auto* loader = JsonObjectLoader<TestStruct2>()
                                    .Field("a", &TestStruct2::a)
                                    .Field("b", &TestStruct2::b)
                                    .OptionalField("c", &TestStruct2::c)
                                    .Finish();
    return loader;
  }
};

struct TestStruct3 {
  std::map<std::string, TestStruct1> a;
  std::map<std::string, int32_t> b;

  static const JsonLoaderInterface* JsonLoader() {
    static const auto* loader = JsonObjectLoader<TestStruct3>()
                                    .Field("a", &TestStruct3::a)
                                    .Field("b", &TestStruct3::b)
                                    .Finish();
    return loader;
  }
};

struct TestPostLoadStruct1 {
  int32_t a = 0;
  int32_t b = 1;
  uint32_t c = 2;
  std::string x;
  Duration d;

  static const JsonLoaderInterface* JsonLoader() {
    static const auto* loader = JsonObjectLoader<TestPostLoadStruct1>()
                                    .Field("a", &TestPostLoadStruct1::a)
                                    .OptionalField("b", &TestPostLoadStruct1::b)
                                    .OptionalField("c", &TestPostLoadStruct1::c)
                                    .Field("x", &TestPostLoadStruct1::x)
                                    .OptionalField("d", &TestPostLoadStruct1::d)
                                    .Finish();
    return loader;
  }

  void JsonPostLoad(const Json& source, ErrorList* errors) { ++a; }
};

TEST(JsonObjectLoaderTest, LoadTestStruct1) {
  {
    auto s = Parse<TestStruct1>(
        "{\"a\":1,\"b\":\"2\",\"c\":3,\"x\":\"foo\",\"d\":\"1.3s\","
        "\"j\":{\"foo\":\"bar\"},\"boolean\":true}");
    ASSERT_TRUE(s.ok()) << s.status();
    EXPECT_EQ(s->a, 1);
    EXPECT_EQ(s->b, 2);
    EXPECT_EQ(s->c, 3);
    EXPECT_EQ(s->x, "foo");
    EXPECT_EQ(s->d, Duration::Milliseconds(1300));
    EXPECT_EQ(s->e, absl::nullopt);
    EXPECT_EQ(Json{s->j}.Dump(), "{\"foo\":\"bar\"}");
    EXPECT_EQ(s->boolean, true);
  }
  {
    auto s = Parse<TestStruct1>(
        "{\"a\":1,\"b\":\"2\",\"c\":3,\"x\":\"foo\",\"d\":\"1.3s\","
        "\"j\":{\"foo\":\"bar\"},\"e\":null}");
    ASSERT_TRUE(s.ok()) << s.status();
    EXPECT_EQ(s->a, 1);
    EXPECT_EQ(s->b, 2);
    EXPECT_EQ(s->c, 3);
    EXPECT_EQ(s->x, "foo");
    EXPECT_EQ(s->d, Duration::Milliseconds(1300));
    EXPECT_EQ(s->e, absl::nullopt);
    EXPECT_EQ(Json{s->j}.Dump(), "{\"foo\":\"bar\"}");
  }
  {
    auto s = Parse<TestStruct1>(
        "{\"a\":1,\"b\":\"2\",\"c\":3,\"x\":\"foo\",\"d\":\"1.3s\","
        "\"j\":{\"foo\":\"bar\"},\"e\":3}");
    ASSERT_TRUE(s.ok()) << s.status();
    EXPECT_EQ(s->a, 1);
    EXPECT_EQ(s->b, 2);
    EXPECT_EQ(s->c, 3);
    EXPECT_EQ(s->x, "foo");
    EXPECT_EQ(s->d, Duration::Milliseconds(1300));
    EXPECT_EQ(s->e, absl::optional<int32_t>(3));
    EXPECT_EQ(Json{s->j}.Dump(), "{\"foo\":\"bar\"}");
  }
  {
    auto s = Parse<TestStruct1>("{\"a\":7, \"x\":\"bar\"}");
    ASSERT_TRUE(s.ok()) << s.status();
    EXPECT_EQ(s->a, 7);
    EXPECT_EQ(s->b, 1);
    EXPECT_EQ(s->c, 2);
    EXPECT_EQ(s->x, "bar");
  }
  {
    auto s = Parse<TestStruct1>("{\"b\":[1],\"c\":\"foo\",\"x\":42}");
    EXPECT_EQ(s.status().code(), absl::StatusCode::kInvalidArgument);
    EXPECT_EQ(s.status().message(),
              "errors validating JSON: ["
              "field:a error:field not present; "
              "field:b error:is not a number; "
              "field:c error:failed to parse non-negative number; "
              "field:x error:is not a string]")
        << s.status();
  }
}

TEST(JsonObjectLoaderTest, LoadPostLoadTestStruct1) {
  {
    auto s = Parse<TestPostLoadStruct1>(
        "{\"a\":1,\"b\":2,\"c\":3,\"x\":\"foo\",\"d\":\"1.3s\"}");
    ASSERT_TRUE(s.ok()) << s.status();
    EXPECT_EQ(s->a, 2);
    EXPECT_EQ(s->b, 2);
    EXPECT_EQ(s->c, 3);
    EXPECT_EQ(s->x, "foo");
    EXPECT_EQ(s->d, Duration::Milliseconds(1300));
  }
}

TEST(JsonObjectLoaderTest, LoadTestStruct2) {
  {
    auto s = Parse<TestStruct2>(
        "{\"a\":[{\"a\":1,\"b\":2,\"c\":3,\"x\":\"foo\"}],\"b\":[1,2,3],"
        "\"c\":{\"a\":1,\"x\":\"foo\"}}");
    ASSERT_TRUE(s.ok()) << s.status();
    EXPECT_EQ(s->a.size(), 1);
    EXPECT_EQ(s->a[0].a, 1);
    EXPECT_EQ(s->a[0].b, 2);
    EXPECT_EQ(s->a[0].c, 3);
    EXPECT_EQ(s->a[0].x, "foo");
    EXPECT_EQ(s->b.size(), 3);
    EXPECT_EQ(s->b[0], 1);
    EXPECT_EQ(s->b[1], 2);
    EXPECT_EQ(s->b[2], 3);
    EXPECT_EQ(s->c.a, 1);
    EXPECT_EQ(s->c.x, "foo");
  }
  {
    auto s =
        Parse<TestStruct2>("{\"a\":[{\"a\":7, \"x\":\"bar\"}],\"b\":[1,2,3]}");
    ASSERT_TRUE(s.ok()) << s.status();
    EXPECT_EQ(s->a.size(), 1);
    EXPECT_EQ(s->a[0].a, 7);
    EXPECT_EQ(s->a[0].b, 1);
    EXPECT_EQ(s->a[0].c, 2);
    EXPECT_EQ(s->a[0].x, "bar");
    EXPECT_EQ(s->b.size(), 3);
    EXPECT_EQ(s->b[0], 1);
    EXPECT_EQ(s->b[1], 2);
    EXPECT_EQ(s->b[2], 3);
  }
  {
    auto s = Parse<TestStruct2>(
        "{\"a\":[{\"a\":\"foo\", \"x\":\"bar\"}],\"b\":[1,{},3],\"c\":1}");
    EXPECT_EQ(s.status().code(), absl::StatusCode::kInvalidArgument);
    EXPECT_EQ(s.status().message(),
              "errors validating JSON: ["
              "field:a[0].a error:failed to parse number; "
              "field:b[1] error:is not a number; "
              "field:c error:is not an object]")
        << s.status();
  }
}

TEST(JsonObjectLoaderTest, LoadTestStruct3) {
  {
    auto s = Parse<TestStruct3>(
        "{\"a\":{\"k1\":{\"a\":7, \"x\":\"bar\"}, "
        "\"k2\":{\"a\":1,\"b\":2,\"c\":3,\"x\":\"foo\"}}, "
        "\"b\":{\"k1\":1,\"k2\":2,\"k3\":3}}");
    ASSERT_TRUE(s.ok()) << s.status();
    EXPECT_EQ(s->a.size(), 2);
    EXPECT_EQ(s->a["k1"].a, 7);
    EXPECT_EQ(s->a["k1"].b, 1);
    EXPECT_EQ(s->a["k1"].c, 2);
    EXPECT_EQ(s->a["k1"].x, "bar");
    EXPECT_EQ(s->a["k2"].a, 1);
    EXPECT_EQ(s->a["k2"].b, 2);
    EXPECT_EQ(s->a["k2"].c, 3);
    EXPECT_EQ(s->a["k2"].x, "foo");
    EXPECT_EQ(s->b.size(), 3);
    EXPECT_EQ(s->b["k1"], 1);
    EXPECT_EQ(s->b["k2"], 2);
    EXPECT_EQ(s->b["k3"], 3);
  }
}

}  // namespace
}  // namespace grpc_core

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
