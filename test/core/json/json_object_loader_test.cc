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

#include "absl/status/status.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

#include <grpc/support/json.h>

#include "src/core/lib/gprpp/ref_counted.h"
#include "src/core/lib/gprpp/ref_counted_ptr.h"
#include "src/core/lib/json/json_reader.h"
#include "src/core/lib/json/json_writer.h"

namespace grpc_core {
namespace {

template <typename T>
absl::StatusOr<T> Parse(absl::string_view json,
                        const JsonArgs& args = JsonArgs()) {
  auto parsed = JsonParse(json);
  if (!parsed.ok()) return parsed.status();
  return LoadFromJson<T>(*parsed, args);
}

//
// Signed integer tests
//

template <typename T>
class SignedIntegerTest : public ::testing::Test {};

TYPED_TEST_SUITE_P(SignedIntegerTest);

TYPED_TEST_P(SignedIntegerTest, IntegerFields) {
  struct TestStruct {
    TypeParam value = 0;
    TypeParam optional_value = 0;
    absl::optional<TypeParam> absl_optional_value;
    std::unique_ptr<TypeParam> unique_ptr_value;

    static const JsonLoaderInterface* JsonLoader(const JsonArgs&) {
      static const auto* loader =
          JsonObjectLoader<TestStruct>()
              .Field("value", &TestStruct::value)
              .OptionalField("optional_value", &TestStruct::optional_value)
              .OptionalField("absl_optional_value",
                             &TestStruct::absl_optional_value)
              .OptionalField("unique_ptr_value", &TestStruct::unique_ptr_value)
              .Finish();
      return loader;
    }
  };
  // Positive number.
  auto test_struct = Parse<TestStruct>("{\"value\": 5}");
  ASSERT_TRUE(test_struct.ok()) << test_struct.status();
  EXPECT_EQ(test_struct->value, 5);
  EXPECT_EQ(test_struct->optional_value, 0);
  EXPECT_FALSE(test_struct->absl_optional_value.has_value());
  // Negative number.
  test_struct = Parse<TestStruct>("{\"value\": -5}");
  ASSERT_TRUE(test_struct.ok()) << test_struct.status();
  EXPECT_EQ(test_struct->value, -5);
  EXPECT_EQ(test_struct->optional_value, 0);
  EXPECT_FALSE(test_struct->absl_optional_value.has_value());
  // Encoded in a JSON string.
  test_struct = Parse<TestStruct>("{\"value\": \"5\"}");
  ASSERT_TRUE(test_struct.ok()) << test_struct.status();
  EXPECT_EQ(test_struct->value, 5);
  EXPECT_EQ(test_struct->optional_value, 0);
  EXPECT_FALSE(test_struct->absl_optional_value.has_value());
  // Fails to parse number from JSON string.
  test_struct = Parse<TestStruct>("{\"value\": \"foo\"}");
  EXPECT_EQ(test_struct.status().code(), absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(
      test_struct.status().message(),
      "errors validating JSON: [field:value error:failed to parse number]")
      << test_struct.status();
  // Fails if required field is not present.
  test_struct = Parse<TestStruct>("{}");
  EXPECT_EQ(test_struct.status().code(), absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(test_struct.status().message(),
            "errors validating JSON: [field:value error:field not present]")
      << test_struct.status();
  // Optional fields present.
  test_struct = Parse<TestStruct>(
      "{\"value\": 5, \"optional_value\": 7, "
      "\"absl_optional_value\": 9, \"unique_ptr_value\": 11}");
  ASSERT_TRUE(test_struct.ok()) << test_struct.status();
  EXPECT_EQ(test_struct->value, 5);
  EXPECT_EQ(test_struct->optional_value, 7);
  EXPECT_EQ(test_struct->absl_optional_value, 9);
  ASSERT_NE(test_struct->unique_ptr_value, nullptr);
  EXPECT_EQ(*test_struct->unique_ptr_value, 11);
  // Wrong JSON type.
  test_struct = Parse<TestStruct>(
      "{\"value\": [], \"optional_value\": {}, "
      "\"absl_optional_value\": true, \"unique_ptr_value\": false}");
  EXPECT_EQ(test_struct.status().code(), absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(test_struct.status().message(),
            "errors validating JSON: ["
            "field:absl_optional_value error:is not a number; "
            "field:optional_value error:is not a number; "
            "field:unique_ptr_value error:is not a number; "
            "field:value error:is not a number]")
      << test_struct.status();
}

REGISTER_TYPED_TEST_SUITE_P(SignedIntegerTest, IntegerFields);

using IntegerTypes = ::testing::Types<int32_t, int64_t>;
INSTANTIATE_TYPED_TEST_SUITE_P(My, SignedIntegerTest, IntegerTypes);

//
// Unsigned integer tests
//

template <typename T>
class UnsignedIntegerTest : public ::testing::Test {};

TYPED_TEST_SUITE_P(UnsignedIntegerTest);

TYPED_TEST_P(UnsignedIntegerTest, IntegerFields) {
  struct TestStruct {
    TypeParam value = 0;
    TypeParam optional_value = 0;
    absl::optional<TypeParam> absl_optional_value;
    std::unique_ptr<TypeParam> unique_ptr_value;

    static const JsonLoaderInterface* JsonLoader(const JsonArgs&) {
      static const auto* loader =
          JsonObjectLoader<TestStruct>()
              .Field("value", &TestStruct::value)
              .OptionalField("optional_value", &TestStruct::optional_value)
              .OptionalField("absl_optional_value",
                             &TestStruct::absl_optional_value)
              .OptionalField("unique_ptr_value", &TestStruct::unique_ptr_value)
              .Finish();
      return loader;
    }
  };
  // Positive number.
  auto test_struct = Parse<TestStruct>("{\"value\": 5}");
  ASSERT_TRUE(test_struct.ok()) << test_struct.status();
  EXPECT_EQ(test_struct->value, 5);
  EXPECT_EQ(test_struct->optional_value, 0);
  EXPECT_FALSE(test_struct->absl_optional_value.has_value());
  // Negative number.
  test_struct = Parse<TestStruct>("{\"value\": -5}");
  EXPECT_EQ(test_struct.status().code(), absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(test_struct.status().message(),
            "errors validating JSON: ["
            "field:value error:failed to parse non-negative number]")
      << test_struct.status();
  // Encoded in a JSON string.
  test_struct = Parse<TestStruct>("{\"value\": \"5\"}");
  ASSERT_TRUE(test_struct.ok()) << test_struct.status();
  EXPECT_EQ(test_struct->value, 5);
  EXPECT_EQ(test_struct->optional_value, 0);
  EXPECT_FALSE(test_struct->absl_optional_value.has_value());
  // Fails to parse number from JSON string.
  test_struct = Parse<TestStruct>("{\"value\": \"foo\"}");
  EXPECT_EQ(test_struct.status().code(), absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(test_struct.status().message(),
            "errors validating JSON: ["
            "field:value error:failed to parse non-negative number]")
      << test_struct.status();
  // Fails if required field is not present.
  test_struct = Parse<TestStruct>("{}");
  EXPECT_EQ(test_struct.status().code(), absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(test_struct.status().message(),
            "errors validating JSON: [field:value error:field not present]")
      << test_struct.status();
  // Optional fields present.
  test_struct = Parse<TestStruct>(
      "{\"value\": 5, \"optional_value\": 7, "
      "\"absl_optional_value\": 9, \"unique_ptr_value\": 11}");
  ASSERT_TRUE(test_struct.ok()) << test_struct.status();
  EXPECT_EQ(test_struct->value, 5);
  EXPECT_EQ(test_struct->optional_value, 7);
  ASSERT_TRUE(test_struct->absl_optional_value.has_value());
  EXPECT_EQ(*test_struct->absl_optional_value, 9);
  ASSERT_NE(test_struct->unique_ptr_value, nullptr);
  EXPECT_EQ(*test_struct->unique_ptr_value, 11);
  // Wrong JSON type.
  test_struct = Parse<TestStruct>(
      "{\"value\": [], \"optional_value\": {}, "
      "\"absl_optional_value\": true, \"unique_ptr_value\": false}");
  EXPECT_EQ(test_struct.status().code(), absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(test_struct.status().message(),
            "errors validating JSON: ["
            "field:absl_optional_value error:is not a number; "
            "field:optional_value error:is not a number; "
            "field:unique_ptr_value error:is not a number; "
            "field:value error:is not a number]")
      << test_struct.status();
}

REGISTER_TYPED_TEST_SUITE_P(UnsignedIntegerTest, IntegerFields);

using UnsignedIntegerTypes = ::testing::Types<uint32_t, uint64_t>;
INSTANTIATE_TYPED_TEST_SUITE_P(My, UnsignedIntegerTest, UnsignedIntegerTypes);

//
// Floating-point tests
//

template <typename T>
class FloatingPointTest : public ::testing::Test {};

TYPED_TEST_SUITE_P(FloatingPointTest);

TYPED_TEST_P(FloatingPointTest, FloatFields) {
  struct TestStruct {
    TypeParam value = 0;
    TypeParam optional_value = 0;
    absl::optional<TypeParam> absl_optional_value;
    std::unique_ptr<TypeParam> unique_ptr_value;

    static const JsonLoaderInterface* JsonLoader(const JsonArgs&) {
      static const auto* loader =
          JsonObjectLoader<TestStruct>()
              .Field("value", &TestStruct::value)
              .OptionalField("optional_value", &TestStruct::optional_value)
              .OptionalField("absl_optional_value",
                             &TestStruct::absl_optional_value)
              .OptionalField("unique_ptr_value", &TestStruct::unique_ptr_value)
              .Finish();
      return loader;
    }
  };
  // Positive number.
  auto test_struct = Parse<TestStruct>("{\"value\": 5.2}");
  ASSERT_TRUE(test_struct.ok()) << test_struct.status();
  EXPECT_NEAR(test_struct->value, 5.2, 0.0001);
  EXPECT_EQ(test_struct->optional_value, 0);
  EXPECT_FALSE(test_struct->absl_optional_value.has_value());
  // Negative number.
  test_struct = Parse<TestStruct>("{\"value\": -5.2}");
  ASSERT_TRUE(test_struct.ok()) << test_struct.status();
  EXPECT_NEAR(test_struct->value, -5.2, 0.0001);
  EXPECT_EQ(test_struct->optional_value, 0);
  EXPECT_FALSE(test_struct->absl_optional_value.has_value());
  // Encoded in a JSON string.
  test_struct = Parse<TestStruct>("{\"value\": \"5.2\"}");
  ASSERT_TRUE(test_struct.ok()) << test_struct.status();
  EXPECT_NEAR(test_struct->value, 5.2, 0.0001);
  EXPECT_EQ(test_struct->optional_value, 0);
  EXPECT_FALSE(test_struct->absl_optional_value.has_value());
  // Fails to parse number from JSON string.
  test_struct = Parse<TestStruct>("{\"value\": \"foo\"}");
  EXPECT_EQ(test_struct.status().code(), absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(test_struct.status().message(),
            "errors validating JSON: ["
            "field:value error:failed to parse floating-point number]")
      << test_struct.status();
  // Fails if required field is not present.
  test_struct = Parse<TestStruct>("{}");
  EXPECT_EQ(test_struct.status().code(), absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(test_struct.status().message(),
            "errors validating JSON: [field:value error:field not present]")
      << test_struct.status();
  // Optional fields present.
  test_struct = Parse<TestStruct>(
      "{\"value\": 5.2, \"optional_value\": 7.5, "
      "\"absl_optional_value\": 9.8, \"unique_ptr_value\": 11.5}");
  ASSERT_TRUE(test_struct.ok()) << test_struct.status();
  EXPECT_NEAR(test_struct->value, 5.2, 0.0001);
  EXPECT_NEAR(test_struct->optional_value, 7.5, 0.0001);
  ASSERT_TRUE(test_struct->absl_optional_value.has_value());
  EXPECT_NEAR(*test_struct->absl_optional_value, 9.8, 0.0001);
  ASSERT_NE(test_struct->unique_ptr_value, nullptr);
  EXPECT_NEAR(*test_struct->unique_ptr_value, 11.5, 0.0001);
  // Wrong JSON type.
  test_struct = Parse<TestStruct>(
      "{\"value\": [], \"optional_value\": {}, "
      "\"absl_optional_value\": true, \"unique_ptr_value\": false}");
  EXPECT_EQ(test_struct.status().code(), absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(test_struct.status().message(),
            "errors validating JSON: ["
            "field:absl_optional_value error:is not a number; "
            "field:optional_value error:is not a number; "
            "field:unique_ptr_value error:is not a number; "
            "field:value error:is not a number]")
      << test_struct.status();
}

REGISTER_TYPED_TEST_SUITE_P(FloatingPointTest, FloatFields);

using FloatingPointTypes = ::testing::Types<float, double>;
INSTANTIATE_TYPED_TEST_SUITE_P(My, FloatingPointTest, FloatingPointTypes);

//
// Boolean tests
//

TEST(JsonObjectLoader, BooleanFields) {
  struct TestStruct {
    bool value = false;
    bool optional_value = true;
    absl::optional<bool> absl_optional_value;
    std::unique_ptr<bool> unique_ptr_value;

    static const JsonLoaderInterface* JsonLoader(const JsonArgs&) {
      static const auto* loader =
          JsonObjectLoader<TestStruct>()
              .Field("value", &TestStruct::value)
              .OptionalField("optional_value", &TestStruct::optional_value)
              .OptionalField("absl_optional_value",
                             &TestStruct::absl_optional_value)
              .OptionalField("unique_ptr_value", &TestStruct::unique_ptr_value)
              .Finish();
      return loader;
    }
  };
  // True.
  auto test_struct = Parse<TestStruct>("{\"value\": true}");
  ASSERT_TRUE(test_struct.ok()) << test_struct.status();
  EXPECT_EQ(test_struct->value, true);
  EXPECT_EQ(test_struct->optional_value, true);  // Unmodified.
  EXPECT_FALSE(test_struct->absl_optional_value.has_value());
  // False.
  test_struct = Parse<TestStruct>("{\"value\": false}");
  ASSERT_TRUE(test_struct.ok()) << test_struct.status();
  EXPECT_EQ(test_struct->value, false);
  EXPECT_EQ(test_struct->optional_value, true);  // Unmodified.
  EXPECT_FALSE(test_struct->absl_optional_value.has_value());
  // Fails if required field is not present.
  test_struct = Parse<TestStruct>("{}");
  EXPECT_EQ(test_struct.status().code(), absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(test_struct.status().message(),
            "errors validating JSON: [field:value error:field not present]")
      << test_struct.status();
  // Optional fields present.
  test_struct = Parse<TestStruct>(
      "{\"value\": true, \"optional_value\": false,"
      "\"absl_optional_value\": true, \"unique_ptr_value\": false}");
  ASSERT_TRUE(test_struct.ok()) << test_struct.status();
  EXPECT_EQ(test_struct->value, true);
  EXPECT_EQ(test_struct->optional_value, false);
  EXPECT_EQ(test_struct->absl_optional_value, true);
  ASSERT_NE(test_struct->unique_ptr_value, nullptr);
  EXPECT_EQ(*test_struct->unique_ptr_value, false);
  // Wrong JSON type.
  test_struct = Parse<TestStruct>(
      "{\"value\": [], \"optional_value\": {}, "
      "\"absl_optional_value\": 1, \"unique_ptr_value\": \"foo\"}");
  EXPECT_EQ(test_struct.status().code(), absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(test_struct.status().message(),
            "errors validating JSON: ["
            "field:absl_optional_value error:is not a boolean; "
            "field:optional_value error:is not a boolean; "
            "field:unique_ptr_value error:is not a boolean; "
            "field:value error:is not a boolean]")
      << test_struct.status();
}

//
// String tests
//

TEST(JsonObjectLoader, StringFields) {
  struct TestStruct {
    std::string value;
    std::string optional_value;
    absl::optional<std::string> absl_optional_value;
    std::unique_ptr<std::string> unique_ptr_value;

    static const JsonLoaderInterface* JsonLoader(const JsonArgs&) {
      static const auto* loader =
          JsonObjectLoader<TestStruct>()
              .Field("value", &TestStruct::value)
              .OptionalField("optional_value", &TestStruct::optional_value)
              .OptionalField("absl_optional_value",
                             &TestStruct::absl_optional_value)
              .OptionalField("unique_ptr_value", &TestStruct::unique_ptr_value)
              .Finish();
      return loader;
    }
  };
  // Valid string.
  auto test_struct = Parse<TestStruct>("{\"value\": \"foo\"}");
  ASSERT_TRUE(test_struct.ok()) << test_struct.status();
  EXPECT_EQ(test_struct->value, "foo");
  EXPECT_EQ(test_struct->optional_value, "");
  EXPECT_FALSE(test_struct->absl_optional_value.has_value());
  // Fails if required field is not present.
  test_struct = Parse<TestStruct>("{}");
  EXPECT_EQ(test_struct.status().code(), absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(test_struct.status().message(),
            "errors validating JSON: [field:value error:field not present]")
      << test_struct.status();
  // Optional fields present.
  test_struct = Parse<TestStruct>(
      "{\"value\": \"foo\", \"optional_value\": \"bar\","
      "\"absl_optional_value\": \"baz\", \"unique_ptr_value\": \"quux\"}");
  ASSERT_TRUE(test_struct.ok()) << test_struct.status();
  EXPECT_EQ(test_struct->value, "foo");
  EXPECT_EQ(test_struct->optional_value, "bar");
  EXPECT_EQ(test_struct->absl_optional_value, "baz");
  ASSERT_NE(test_struct->unique_ptr_value, nullptr);
  EXPECT_EQ(*test_struct->unique_ptr_value, "quux");
  // Wrong JSON type.
  test_struct = Parse<TestStruct>(
      "{\"value\": [], \"optional_value\": {}, "
      "\"absl_optional_value\": 1, \"unique_ptr_value\": true}");
  EXPECT_EQ(test_struct.status().code(), absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(test_struct.status().message(),
            "errors validating JSON: ["
            "field:absl_optional_value error:is not a string; "
            "field:optional_value error:is not a string; "
            "field:unique_ptr_value error:is not a string; "
            "field:value error:is not a string]")
      << test_struct.status();
}

//
// Duration tests
//

TEST(JsonObjectLoader, DurationFields) {
  struct TestStruct {
    Duration value = Duration::Zero();
    Duration optional_value = Duration::Zero();
    absl::optional<Duration> absl_optional_value;
    std::unique_ptr<Duration> unique_ptr_value;

    static const JsonLoaderInterface* JsonLoader(const JsonArgs&) {
      static const auto* loader =
          JsonObjectLoader<TestStruct>()
              .Field("value", &TestStruct::value)
              .OptionalField("optional_value", &TestStruct::optional_value)
              .OptionalField("absl_optional_value",
                             &TestStruct::absl_optional_value)
              .OptionalField("unique_ptr_value", &TestStruct::unique_ptr_value)
              .Finish();
      return loader;
    }
  };
  // Valid duration string.
  auto test_struct = Parse<TestStruct>("{\"value\": \"3s\"}");
  ASSERT_TRUE(test_struct.ok()) << test_struct.status();
  EXPECT_EQ(test_struct->value, Duration::Seconds(3));
  EXPECT_EQ(test_struct->optional_value, Duration::Zero());
  EXPECT_FALSE(test_struct->absl_optional_value.has_value());
  // Invalid duration strings.
  test_struct = Parse<TestStruct>(
      "{\"value\": \"3sec\", \"optional_value\": \"foos\","
      "\"absl_optional_value\": \"1.0123456789s\"}");
  EXPECT_EQ(test_struct.status().code(), absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(test_struct.status().message(),
            "errors validating JSON: ["
            "field:absl_optional_value error:"
            "Not a duration (too many digits after decimal); "
            "field:optional_value error:"
            "Not a duration (not a number of seconds); "
            "field:value error:Not a duration (no s suffix)]")
      << test_struct.status();
  test_struct = Parse<TestStruct>("{\"value\": \"315576000001s\"}");
  EXPECT_EQ(test_struct.status().code(), absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(test_struct.status().message(),
            "errors validating JSON: ["
            "field:value error:seconds must be in the range [0, 315576000000]]")
      << test_struct.status();
  test_struct = Parse<TestStruct>("{\"value\": \"3.xs\"}");
  EXPECT_EQ(test_struct.status().code(), absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(test_struct.status().message(),
            "errors validating JSON: ["
            "field:value error:Not a duration (not a number of nanoseconds)]")
      << test_struct.status();
  // Fails if required field is not present.
  test_struct = Parse<TestStruct>("{}");
  EXPECT_EQ(test_struct.status().code(), absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(test_struct.status().message(),
            "errors validating JSON: [field:value error:field not present]")
      << test_struct.status();
  // Optional fields present.
  test_struct = Parse<TestStruct>(
      "{\"value\": \"3s\", \"optional_value\": \"3.2s\", "
      "\"absl_optional_value\": \"10s\", \"unique_ptr_value\": \"11s\"}");
  ASSERT_TRUE(test_struct.ok()) << test_struct.status();
  EXPECT_EQ(test_struct->value, Duration::Seconds(3));
  EXPECT_EQ(test_struct->optional_value, Duration::Milliseconds(3200));
  EXPECT_EQ(test_struct->absl_optional_value, Duration::Seconds(10));
  ASSERT_NE(test_struct->unique_ptr_value, nullptr);
  EXPECT_EQ(*test_struct->unique_ptr_value, Duration::Seconds(11));
  // Wrong JSON type.
  test_struct = Parse<TestStruct>(
      "{\"value\": [], \"optional_value\": {}, "
      "\"absl_optional_value\": 1, \"unique_ptr_value\": true}");
  EXPECT_EQ(test_struct.status().code(), absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(test_struct.status().message(),
            "errors validating JSON: ["
            "field:absl_optional_value error:is not a string; "
            "field:optional_value error:is not a string; "
            "field:unique_ptr_value error:is not a string; "
            "field:value error:is not a string]")
      << test_struct.status();
}

//
// Json::Object tests
//

TEST(JsonObjectLoader, JsonObjectFields) {
  struct TestStruct {
    Json::Object value;
    Json::Object optional_value;
    absl::optional<Json::Object> absl_optional_value;
    std::unique_ptr<Json::Object> unique_ptr_value;

    static const JsonLoaderInterface* JsonLoader(const JsonArgs&) {
      static const auto* loader =
          JsonObjectLoader<TestStruct>()
              .Field("value", &TestStruct::value)
              .OptionalField("optional_value", &TestStruct::optional_value)
              .OptionalField("absl_optional_value",
                             &TestStruct::absl_optional_value)
              .OptionalField("unique_ptr_value", &TestStruct::unique_ptr_value)
              .Finish();
      return loader;
    }
  };
  // Valid object.
  auto test_struct = Parse<TestStruct>("{\"value\": {\"a\":1}}");
  ASSERT_TRUE(test_struct.ok()) << test_struct.status();
  EXPECT_EQ(JsonDump(Json::FromObject(test_struct->value)), "{\"a\":1}");
  EXPECT_EQ(JsonDump(Json::FromObject(test_struct->optional_value)), "{}");
  EXPECT_FALSE(test_struct->absl_optional_value.has_value());
  // Fails if required field is not present.
  test_struct = Parse<TestStruct>("{}");
  EXPECT_EQ(test_struct.status().code(), absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(test_struct.status().message(),
            "errors validating JSON: [field:value error:field not present]")
      << test_struct.status();
  // Optional fields present.
  test_struct = Parse<TestStruct>(
      "{\"value\": {\"a\":1}, \"optional_value\": {\"b\":2}, "
      "\"absl_optional_value\": {\"c\":3}, \"unique_ptr_value\": {\"d\":4}}");
  ASSERT_TRUE(test_struct.ok()) << test_struct.status();
  EXPECT_EQ(JsonDump(Json::FromObject(test_struct->value)), "{\"a\":1}");
  EXPECT_EQ(JsonDump(Json::FromObject(test_struct->optional_value)),
            "{\"b\":2}");
  ASSERT_TRUE(test_struct->absl_optional_value.has_value());
  EXPECT_EQ(JsonDump(Json::FromObject(*test_struct->absl_optional_value)),
            "{\"c\":3}");
  ASSERT_NE(test_struct->unique_ptr_value, nullptr);
  EXPECT_EQ(JsonDump(Json::FromObject(*test_struct->unique_ptr_value)),
            "{\"d\":4}");
  // Wrong JSON type.
  test_struct = Parse<TestStruct>(
      "{\"value\": [], \"optional_value\": true, "
      "\"absl_optional_value\": 1, \"unique_ptr_value\": \"foo\"}");
  EXPECT_EQ(test_struct.status().code(), absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(test_struct.status().message(),
            "errors validating JSON: ["
            "field:absl_optional_value error:is not an object; "
            "field:optional_value error:is not an object; "
            "field:unique_ptr_value error:is not an object; "
            "field:value error:is not an object]")
      << test_struct.status();
}

//
// Json::Array tests
//

TEST(JsonObjectLoader, JsonArrayFields) {
  struct TestStruct {
    Json::Array value;
    Json::Array optional_value;
    absl::optional<Json::Array> absl_optional_value;
    std::unique_ptr<Json::Array> unique_ptr_value;

    static const JsonLoaderInterface* JsonLoader(const JsonArgs&) {
      static const auto* loader =
          JsonObjectLoader<TestStruct>()
              .Field("value", &TestStruct::value)
              .OptionalField("optional_value", &TestStruct::optional_value)
              .OptionalField("absl_optional_value",
                             &TestStruct::absl_optional_value)
              .OptionalField("unique_ptr_value", &TestStruct::unique_ptr_value)
              .Finish();
      return loader;
    }
  };
  // Valid object.
  auto test_struct = Parse<TestStruct>("{\"value\": [1, \"a\"]}");
  ASSERT_TRUE(test_struct.ok()) << test_struct.status();
  EXPECT_EQ(JsonDump(Json::FromArray(test_struct->value)), "[1,\"a\"]");
  EXPECT_EQ(JsonDump(Json::FromArray(test_struct->optional_value)), "[]");
  EXPECT_FALSE(test_struct->absl_optional_value.has_value());
  EXPECT_EQ(test_struct->unique_ptr_value, nullptr);
  // Fails if required field is not present.
  test_struct = Parse<TestStruct>("{}");
  EXPECT_EQ(test_struct.status().code(), absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(test_struct.status().message(),
            "errors validating JSON: [field:value error:field not present]")
      << test_struct.status();
  // Optional fields present.
  test_struct = Parse<TestStruct>(
      "{\"value\": [1, \"a\"], \"optional_value\": [2, \"b\"], "
      "\"absl_optional_value\": [3, \"c\"], \"unique_ptr_value\": [4, \"d\"]}");
  ASSERT_TRUE(test_struct.ok()) << test_struct.status();
  EXPECT_EQ(JsonDump(Json::FromArray(test_struct->value)), "[1,\"a\"]");
  EXPECT_EQ(JsonDump(Json::FromArray(test_struct->optional_value)),
            "[2,\"b\"]");
  ASSERT_TRUE(test_struct->absl_optional_value.has_value());
  EXPECT_EQ(JsonDump(Json::FromArray(*test_struct->absl_optional_value)),
            "[3,\"c\"]");
  ASSERT_NE(test_struct->unique_ptr_value, nullptr);
  EXPECT_EQ(JsonDump(Json::FromArray(*test_struct->unique_ptr_value)),
            "[4,\"d\"]");
  // Wrong JSON type.
  test_struct = Parse<TestStruct>(
      "{\"value\": {}, \"optional_value\": true, "
      "\"absl_optional_value\": 1, \"unique_ptr_value\": \"foo\"}");
  EXPECT_EQ(test_struct.status().code(), absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(test_struct.status().message(),
            "errors validating JSON: ["
            "field:absl_optional_value error:is not an array; "
            "field:optional_value error:is not an array; "
            "field:unique_ptr_value error:is not an array; "
            "field:value error:is not an array]")
      << test_struct.status();
}

//
// map<> tests
//

TEST(JsonObjectLoader, MapFields) {
  struct TestStruct {
    std::map<std::string, int32_t> value;
    std::map<std::string, std::string> optional_value;
    absl::optional<std::map<std::string, bool>> absl_optional_value;
    std::unique_ptr<std::map<std::string, int32_t>> unique_ptr_value;

    static const JsonLoaderInterface* JsonLoader(const JsonArgs&) {
      static const auto* loader =
          JsonObjectLoader<TestStruct>()
              .Field("value", &TestStruct::value)
              .OptionalField("optional_value", &TestStruct::optional_value)
              .OptionalField("absl_optional_value",
                             &TestStruct::absl_optional_value)
              .OptionalField("unique_ptr_value", &TestStruct::unique_ptr_value)
              .Finish();
      return loader;
    }
  };
  // Valid map.
  auto test_struct = Parse<TestStruct>("{\"value\": {\"a\":1}}");
  ASSERT_TRUE(test_struct.ok()) << test_struct.status();
  EXPECT_THAT(test_struct->value,
              ::testing::ElementsAre(::testing::Pair("a", 1)));
  EXPECT_THAT(test_struct->optional_value, ::testing::ElementsAre());
  EXPECT_FALSE(test_struct->absl_optional_value.has_value());
  // Fails if required field is not present.
  test_struct = Parse<TestStruct>("{}");
  EXPECT_EQ(test_struct.status().code(), absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(test_struct.status().message(),
            "errors validating JSON: [field:value error:field not present]")
      << test_struct.status();
  // Optional fields present.
  test_struct = Parse<TestStruct>(
      "{\"value\": {\"a\":1}, \"optional_value\": {\"b\":\"foo\"}, "
      "\"absl_optional_value\": {\"c\":true}, "
      "\"unique_ptr_value\": {\"d\":4}}");
  ASSERT_TRUE(test_struct.ok()) << test_struct.status();
  EXPECT_THAT(test_struct->value,
              ::testing::ElementsAre(::testing::Pair("a", 1)));
  EXPECT_THAT(test_struct->optional_value,
              ::testing::ElementsAre(::testing::Pair("b", "foo")));
  ASSERT_TRUE(test_struct->absl_optional_value.has_value());
  EXPECT_THAT(*test_struct->absl_optional_value,
              ::testing::ElementsAre(::testing::Pair("c", true)));
  ASSERT_NE(test_struct->unique_ptr_value, nullptr);
  EXPECT_THAT(*test_struct->unique_ptr_value,
              ::testing::ElementsAre(::testing::Pair("d", 4)));
  // Wrong JSON type.
  test_struct = Parse<TestStruct>(
      "{\"value\": [], \"optional_value\": true, "
      "\"absl_optional_value\": 1, \"unique_ptr_value\": \"foo\"}");
  EXPECT_EQ(test_struct.status().code(), absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(test_struct.status().message(),
            "errors validating JSON: ["
            "field:absl_optional_value error:is not an object; "
            "field:optional_value error:is not an object; "
            "field:unique_ptr_value error:is not an object; "
            "field:value error:is not an object]")
      << test_struct.status();
  // Wrong JSON type for map value.
  test_struct = Parse<TestStruct>(
      "{\"value\": {\"a\":\"foo\"}, \"optional_value\": {\"b\":true}, "
      "\"absl_optional_value\": {\"c\":1}}");
  EXPECT_EQ(test_struct.status().code(), absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(test_struct.status().message(),
            "errors validating JSON: ["
            "field:absl_optional_value[\"c\"] error:is not a boolean; "
            "field:optional_value[\"b\"] error:is not a string; "
            "field:value[\"a\"] error:failed to parse number]")
      << test_struct.status();
}

//
// vector<> tests
//

TEST(JsonObjectLoader, VectorFields) {
  struct TestStruct {
    std::vector<int32_t> value;
    std::vector<std::string> optional_value;
    absl::optional<std::vector<bool>> absl_optional_value;
    std::unique_ptr<std::vector<int32_t>> unique_ptr_value;

    static const JsonLoaderInterface* JsonLoader(const JsonArgs&) {
      static const auto* loader =
          JsonObjectLoader<TestStruct>()
              .Field("value", &TestStruct::value)
              .OptionalField("optional_value", &TestStruct::optional_value)
              .OptionalField("absl_optional_value",
                             &TestStruct::absl_optional_value)
              .OptionalField("unique_ptr_value", &TestStruct::unique_ptr_value)
              .Finish();
      return loader;
    }
  };
  // Valid map.
  auto test_struct = Parse<TestStruct>("{\"value\": [1, 2, 3]}");
  ASSERT_TRUE(test_struct.ok()) << test_struct.status();
  EXPECT_THAT(test_struct->value, ::testing::ElementsAre(1, 2, 3));
  EXPECT_THAT(test_struct->optional_value, ::testing::ElementsAre());
  EXPECT_FALSE(test_struct->absl_optional_value.has_value());
  // Fails if required field is not present.
  test_struct = Parse<TestStruct>("{}");
  EXPECT_EQ(test_struct.status().code(), absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(test_struct.status().message(),
            "errors validating JSON: [field:value error:field not present]")
      << test_struct.status();
  // Optional fields present.
  test_struct = Parse<TestStruct>(
      "{\"value\": [4, 5, 6], \"optional_value\": [\"foo\", \"bar\"], "
      "\"absl_optional_value\": [true, false, true], "
      "\"unique_ptr_value\": [1, 2, 3]}");
  ASSERT_TRUE(test_struct.ok()) << test_struct.status();
  EXPECT_THAT(test_struct->value, ::testing::ElementsAre(4, 5, 6));
  EXPECT_THAT(test_struct->optional_value,
              ::testing::ElementsAre("foo", "bar"));
  ASSERT_TRUE(test_struct->absl_optional_value.has_value());
  EXPECT_THAT(*test_struct->absl_optional_value,
              ::testing::ElementsAre(true, false, true));
  ASSERT_NE(test_struct->unique_ptr_value, nullptr);
  EXPECT_THAT(*test_struct->unique_ptr_value, ::testing::ElementsAre(1, 2, 3));
  // Wrong JSON type.
  test_struct = Parse<TestStruct>(
      "{\"value\": {}, \"optional_value\": true, "
      "\"absl_optional_value\": 1, \"unique_ptr_value\": \"foo\"}");
  EXPECT_EQ(test_struct.status().code(), absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(test_struct.status().message(),
            "errors validating JSON: ["
            "field:absl_optional_value error:is not an array; "
            "field:optional_value error:is not an array; "
            "field:unique_ptr_value error:is not an array; "
            "field:value error:is not an array]")
      << test_struct.status();
  // Wrong JSON type for map value.
  test_struct = Parse<TestStruct>(
      "{\"value\": [\"foo\", \"bar\"], \"optional_value\": [true, false], "
      "\"absl_optional_value\": [1, 2]}");
  EXPECT_EQ(test_struct.status().code(), absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(test_struct.status().message(),
            "errors validating JSON: ["
            "field:absl_optional_value[0] error:is not a boolean; "
            "field:absl_optional_value[1] error:is not a boolean; "
            "field:optional_value[0] error:is not a string; "
            "field:optional_value[1] error:is not a string; "
            "field:value[0] error:failed to parse number; "
            "field:value[1] error:failed to parse number]")
      << test_struct.status();
}

//
// Nested struct tests
//

TEST(JsonObjectLoader, NestedStructFields) {
  struct NestedStruct {
    int32_t inner = 0;

    static const JsonLoaderInterface* JsonLoader(const JsonArgs&) {
      static const auto* loader = JsonObjectLoader<NestedStruct>()
                                      .Field("inner", &NestedStruct::inner)
                                      .Finish();
      return loader;
    }
  };
  struct TestStruct {
    NestedStruct outer;
    NestedStruct optional_outer;
    absl::optional<NestedStruct> absl_optional_outer;
    std::unique_ptr<NestedStruct> unique_ptr_outer;

    static const JsonLoaderInterface* JsonLoader(const JsonArgs&) {
      static const auto* loader =
          JsonObjectLoader<TestStruct>()
              .Field("outer", &TestStruct::outer)
              .OptionalField("optional_outer", &TestStruct::optional_outer)
              .OptionalField("absl_optional_outer",
                             &TestStruct::absl_optional_outer)
              .OptionalField("unique_ptr_outer", &TestStruct::unique_ptr_outer)
              .Finish();
      return loader;
    }
  };
  // Valid nested struct.
  auto test_struct = Parse<TestStruct>("{\"outer\": {\"inner\": 1}}");
  ASSERT_TRUE(test_struct.ok()) << test_struct.status();
  EXPECT_EQ(test_struct->outer.inner, 1);
  EXPECT_EQ(test_struct->optional_outer.inner, 0);
  EXPECT_FALSE(test_struct->absl_optional_outer.has_value());
  // Fails if required field is not present.
  test_struct = Parse<TestStruct>("{}");
  EXPECT_EQ(test_struct.status().code(), absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(test_struct.status().message(),
            "errors validating JSON: [field:outer error:field not present]")
      << test_struct.status();
  // Fails if inner required field is not present.
  test_struct = Parse<TestStruct>("{\"outer\": {}}");
  EXPECT_EQ(test_struct.status().code(), absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(
      test_struct.status().message(),
      "errors validating JSON: [field:outer.inner error:field not present]")
      << test_struct.status();
  // Optional fields present.
  test_struct = Parse<TestStruct>(
      "{\"outer\": {\"inner\":1}, \"optional_outer\": {\"inner\":2}, "
      "\"absl_optional_outer\": {\"inner\":3}, "
      "\"unique_ptr_outer\": {\"inner\":4}}");
  ASSERT_TRUE(test_struct.ok()) << test_struct.status();
  EXPECT_EQ(test_struct->outer.inner, 1);
  EXPECT_EQ(test_struct->optional_outer.inner, 2);
  ASSERT_TRUE(test_struct->absl_optional_outer.has_value());
  EXPECT_EQ(test_struct->absl_optional_outer->inner, 3);
  ASSERT_NE(test_struct->unique_ptr_outer, nullptr);
  EXPECT_EQ(test_struct->unique_ptr_outer->inner, 4);
  // Wrong JSON type.
  test_struct = Parse<TestStruct>(
      "{\"outer\": \"foo\", \"optional_outer\": true, "
      "\"absl_optional_outer\": 1, \"unique_ptr_outer\": \"foo\"}");
  EXPECT_EQ(test_struct.status().code(), absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(test_struct.status().message(),
            "errors validating JSON: ["
            "field:absl_optional_outer error:is not an object; "
            "field:optional_outer error:is not an object; "
            "field:outer error:is not an object; "
            "field:unique_ptr_outer error:is not an object]")
      << test_struct.status();
  // Wrong JSON type for inner value.
  test_struct = Parse<TestStruct>(
      "{\"outer\": {\"inner\":\"foo\"}, \"optional_outer\": {\"inner\":true}, "
      "\"absl_optional_outer\": {\"inner\":[]}}");
  EXPECT_EQ(test_struct.status().code(), absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(test_struct.status().message(),
            "errors validating JSON: ["
            "field:absl_optional_outer.inner error:is not a number; "
            "field:optional_outer.inner error:is not a number; "
            "field:outer.inner error:failed to parse number]")
      << test_struct.status();
}

TEST(JsonObjectLoader, BareString) {
  auto parsed = Parse<std::string>("\"foo\"");
  ASSERT_TRUE(parsed.ok()) << parsed.status();
  EXPECT_EQ(*parsed, "foo");
}

TEST(JsonObjectLoader, BareDuration) {
  auto parsed = Parse<Duration>("\"1.5s\"");
  ASSERT_TRUE(parsed.ok()) << parsed.status();
  EXPECT_EQ(*parsed, Duration::Milliseconds(1500));
}

TEST(JsonObjectLoader, BareSignedInteger) {
  auto parsed = Parse<int32_t>("5");
  ASSERT_TRUE(parsed.ok()) << parsed.status();
  EXPECT_EQ(*parsed, 5);
}

TEST(JsonObjectLoader, BareUnsignedInteger) {
  auto parsed = Parse<uint32_t>("5");
  ASSERT_TRUE(parsed.ok()) << parsed.status();
  EXPECT_EQ(*parsed, 5);
}

TEST(JsonObjectLoader, BareFloat) {
  auto parsed = Parse<float>("5.2");
  ASSERT_TRUE(parsed.ok()) << parsed.status();
  EXPECT_NEAR(*parsed, 5.2, 0.001);
}

TEST(JsonObjectLoader, BareBool) {
  auto parsed = Parse<bool>("true");
  ASSERT_TRUE(parsed.ok()) << parsed.status();
  EXPECT_TRUE(*parsed);
}

TEST(JsonObjectLoader, BareUniquePtr) {
  auto parsed = Parse<std::unique_ptr<uint32_t>>("3");
  ASSERT_TRUE(parsed.ok()) << parsed.status();
  ASSERT_NE(*parsed, nullptr);
  EXPECT_EQ(**parsed, 3);
}

TEST(JsonObjectLoader, BareRefCountedPtr) {
  class RefCountedObject : public RefCounted<RefCountedObject> {
   public:
    RefCountedObject() = default;

    int value() const { return value_; }

    static const JsonLoaderInterface* JsonLoader(const JsonArgs&) {
      static const auto* loader = JsonObjectLoader<RefCountedObject>()
                                      .Field("value", &RefCountedObject::value_)
                                      .Finish();
      return loader;
    }

   private:
    int value_ = -1;
  };
  auto parsed = Parse<RefCountedPtr<RefCountedObject>>("{\"value\": 3}");
  ASSERT_TRUE(parsed.ok()) << parsed.status();
  ASSERT_NE(*parsed, nullptr);
  EXPECT_EQ((*parsed)->value(), 3);
  parsed = Parse<RefCountedPtr<RefCountedObject>>("5");
  EXPECT_EQ(parsed.status().code(), absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(parsed.status().message(),
            "errors validating JSON: [field: error:is not an object]")
      << parsed.status();
}

TEST(JsonObjectLoader, BareVector) {
  auto parsed = Parse<std::vector<int32_t>>("[1, 2, 3]");
  ASSERT_TRUE(parsed.ok()) << parsed.status();
  EXPECT_THAT(*parsed, ::testing::ElementsAre(1, 2, 3));
}

TEST(JsonObjectLoader, BareMap) {
  auto parsed =
      Parse<std::map<std::string, int32_t>>("{\"a\":1, \"b\":2, \"c\":3}");
  ASSERT_TRUE(parsed.ok()) << parsed.status();
  EXPECT_THAT(*parsed, ::testing::ElementsAre(::testing::Pair("a", 1),
                                              ::testing::Pair("b", 2),
                                              ::testing::Pair("c", 3)));
}

TEST(JsonObjectLoader, IgnoresUnsupportedFields) {
  struct TestStruct {
    int32_t a = 0;

    static const JsonLoaderInterface* JsonLoader(const JsonArgs&) {
      static const auto* loader =
          JsonObjectLoader<TestStruct>().Field("a", &TestStruct::a).Finish();
      return loader;
    }
  };
  auto test_struct = Parse<TestStruct>("{\"a\": 3, \"b\":false}");
  ASSERT_TRUE(test_struct.ok()) << test_struct.status();
  EXPECT_EQ(test_struct->a, 3);
}

TEST(JsonObjectLoader, IgnoresDisabledFields) {
  class FakeJsonArgs : public JsonArgs {
   public:
    FakeJsonArgs() = default;

    bool IsEnabled(absl::string_view key) const override {
      return key != "disabled";
    }
  };
  struct TestStruct {
    int32_t a = 0;
    int32_t b = 0;
    int32_t c = 0;

    static const JsonLoaderInterface* JsonLoader(const JsonArgs&) {
      static const auto* loader =
          JsonObjectLoader<TestStruct>()
              .Field("a", &TestStruct::a, "disabled")
              .OptionalField("b", &TestStruct::b, "disabled")
              .OptionalField("c", &TestStruct::c, "enabled")
              .Finish();
      return loader;
    }
  };
  // Fields "a" and "b" have the wrong types, but we ignore them,
  // because they're disabled.
  auto test_struct =
      Parse<TestStruct>("{\"a\":false, \"b\":false, \"c\":1}", FakeJsonArgs());
  ASSERT_TRUE(test_struct.ok()) << test_struct.status();
  EXPECT_EQ(test_struct->a, 0);
  EXPECT_EQ(test_struct->b, 0);
  EXPECT_EQ(test_struct->c, 1);
}

TEST(JsonObjectLoader, PostLoadHook) {
  struct TestStruct {
    int32_t a = 0;

    static const JsonLoaderInterface* JsonLoader(const JsonArgs&) {
      static const auto* loader = JsonObjectLoader<TestStruct>()
                                      .OptionalField("a", &TestStruct::a)
                                      .Finish();
      return loader;
    }

    void JsonPostLoad(const Json& /*source*/, const JsonArgs& /*args*/,
                      ValidationErrors* /*errors*/) {
      ++a;
    }
  };
  auto test_struct = Parse<TestStruct>("{\"a\": 1}");
  ASSERT_TRUE(test_struct.ok()) << test_struct.status();
  EXPECT_EQ(test_struct->a, 2);
  test_struct = Parse<TestStruct>("{}");
  ASSERT_TRUE(test_struct.ok()) << test_struct.status();
  EXPECT_EQ(test_struct->a, 1);
}

TEST(JsonObjectLoader, CustomValidationInPostLoadHook) {
  struct TestStruct {
    int32_t a = 0;

    static const JsonLoaderInterface* JsonLoader(const JsonArgs&) {
      static const auto* loader =
          JsonObjectLoader<TestStruct>().Field("a", &TestStruct::a).Finish();
      return loader;
    }

    void JsonPostLoad(const Json& /*source*/, const JsonArgs& /*args*/,
                      ValidationErrors* errors) {
      ValidationErrors::ScopedField field(errors, ".a");
      if (!errors->FieldHasErrors() && a <= 0) {
        errors->AddError("must be greater than 0");
      }
    }
  };
  // Value greater than 0.
  auto test_struct = Parse<TestStruct>("{\"a\": 1}");
  ASSERT_TRUE(test_struct.ok()) << test_struct.status();
  EXPECT_EQ(test_struct->a, 1);
  // Value 0, triggers custom validation.
  test_struct = Parse<TestStruct>("{\"a\": 0}");
  EXPECT_EQ(test_struct.status().code(), absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(test_struct.status().message(),
            "errors validating JSON: [field:a error:must be greater than 0]")
      << test_struct.status();
  // Invalid type, generates built-in parsing error, so custom
  // validation will not generate a new error.
  test_struct = Parse<TestStruct>("{\"a\": []}");
  EXPECT_EQ(test_struct.status().code(), absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(test_struct.status().message(),
            "errors validating JSON: [field:a error:is not a number]")
      << test_struct.status();
}

TEST(JsonObjectLoader, LoadFromJsonWithValidationErrors) {
  struct TestStruct {
    int32_t a = 0;

    static const JsonLoaderInterface* JsonLoader(const JsonArgs&) {
      static const auto* loader =
          JsonObjectLoader<TestStruct>().Field("a", &TestStruct::a).Finish();
      return loader;
    }
  };
  // Valid.
  {
    absl::string_view json_str = "{\"a\":1}";
    auto json = JsonParse(json_str);
    ASSERT_TRUE(json.ok()) << json.status();
    ValidationErrors errors;
    TestStruct test_struct =
        LoadFromJson<TestStruct>(*json, JsonArgs(), &errors);
    ASSERT_TRUE(errors.ok()) << errors.status(
        absl::StatusCode::kInvalidArgument, "unexpected errors");
    EXPECT_EQ(test_struct.a, 1);
  }
  // Invalid.
  {
    absl::string_view json_str = "{\"a\":\"foo\"}";
    auto json = JsonParse(json_str);
    ASSERT_TRUE(json.ok()) << json.status();
    ValidationErrors errors;
    LoadFromJson<TestStruct>(*json, JsonArgs(), &errors);
    absl::Status status = errors.status(absl::StatusCode::kInvalidArgument,
                                        "errors validating JSON");
    EXPECT_EQ(status.code(), absl::StatusCode::kInvalidArgument);
    EXPECT_EQ(status.message(),
              "errors validating JSON: [field:a error:failed to parse number]")
        << status;
  }
}

TEST(JsonObjectLoader, LoadJsonObjectField) {
  absl::string_view json_str = "{\"int\":1}";
  auto json = JsonParse(json_str);
  ASSERT_TRUE(json.ok()) << json.status();
  // Load a valid field.
  {
    ValidationErrors errors;
    auto value = LoadJsonObjectField<int32_t>(json->object(), JsonArgs(), "int",
                                              &errors);
    ASSERT_TRUE(value.has_value()) << errors.status(
        absl::StatusCode::kInvalidArgument, "unexpected errors");
    EXPECT_EQ(*value, 1);
    EXPECT_TRUE(errors.ok());
  }
  // An optional field that is not present.
  {
    ValidationErrors errors;
    auto value = LoadJsonObjectField<int32_t>(json->object(), JsonArgs(),
                                              "not_present", &errors,
                                              /*required=*/false);
    EXPECT_FALSE(value.has_value());
    EXPECT_TRUE(errors.ok());
  }
  // A required field that is not present.
  {
    ValidationErrors errors;
    auto value = LoadJsonObjectField<int32_t>(json->object(), JsonArgs(),
                                              "not_present", &errors);
    EXPECT_FALSE(value.has_value());
    auto status = errors.status(absl::StatusCode::kInvalidArgument,
                                "errors validating JSON");
    EXPECT_THAT(status.code(), absl::StatusCode::kInvalidArgument);
    EXPECT_EQ(status.message(),
              "errors validating JSON: ["
              "field:not_present error:field not present]")
        << status;
  }
  // Value has the wrong type.
  {
    ValidationErrors errors;
    auto value = LoadJsonObjectField<std::string>(json->object(), JsonArgs(),
                                                  "int", &errors);
    EXPECT_FALSE(value.has_value());
    auto status = errors.status(absl::StatusCode::kInvalidArgument,
                                "errors validating JSON");
    EXPECT_THAT(status.code(), absl::StatusCode::kInvalidArgument);
    EXPECT_EQ(status.message(),
              "errors validating JSON: [field:int error:is not a string]")
        << status;
  }
}

}  // namespace
}  // namespace grpc_core

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
