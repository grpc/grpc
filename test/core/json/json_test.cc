/*
 *
 * Copyright 2015-2016 gRPC authors.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

#include <string.h>

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "src/core/lib/gpr/string.h"
#include "src/core/lib/gpr/useful.h"
#include "src/core/lib/json/json.h"

#include "test/core/util/test_config.h"

namespace grpc_core {

void ValidateValue(const Json& actual, const Json& expected);

void ValidateObject(const Json::Object& actual, const Json::Object& expected) {
  ASSERT_EQ(actual.size(), expected.size());
  auto actual_it = actual.begin();
  for (const auto& p : expected) {
    EXPECT_EQ(actual_it->first, p.first);
    ValidateValue(actual_it->second, p.second);
    ++actual_it;
  }
}

void ValidateArray(const Json::Array& actual, const Json::Array& expected) {
  ASSERT_EQ(actual.size(), expected.size());
  for (size_t i = 0; i < expected.size(); ++i) {
    ValidateValue(actual[i], expected[i]);
  }
}

void ValidateValue(const Json& actual, const Json& expected) {
  ASSERT_EQ(actual.type(), expected.type());
  switch (expected.type()) {
    case Json::Type::JSON_NULL:
    case Json::Type::JSON_TRUE:
    case Json::Type::JSON_FALSE:
      break;
    case Json::Type::STRING:
    case Json::Type::NUMBER:
      EXPECT_EQ(actual.string_value(), expected.string_value());
      break;
    case Json::Type::OBJECT:
      ValidateObject(actual.object_value(), expected.object_value());
      break;
    case Json::Type::ARRAY:
      ValidateArray(actual.array_value(), expected.array_value());
      break;
  }
}

void RunSuccessTest(const char* input, const Json& expected,
                    const char* expected_output) {
  gpr_log(GPR_INFO, "parsing string \"%s\" - should succeed", input);
  grpc_error_handle error = GRPC_ERROR_NONE;
  Json json = Json::Parse(input, &error);
  ASSERT_EQ(error, GRPC_ERROR_NONE) << grpc_error_std_string(error);
  ValidateValue(json, expected);
  std::string output = json.Dump();
  EXPECT_EQ(output, expected_output);
}

TEST(Json, Whitespace) {
  RunSuccessTest(" 0 ", 0, "0");
  RunSuccessTest(" 1 ", 1, "1");
  RunSuccessTest(" \"    \" ", "    ", "\"    \"");
  RunSuccessTest(" \"a\" ", "a", "\"a\"");
  RunSuccessTest(" true ", true, "true");
}

TEST(Json, Utf16) {
  RunSuccessTest("\"\\u0020\\\\\\u0010\\u000a\\u000D\"", " \\\u0010\n\r",
                 "\" \\\\\\u0010\\n\\r\"");
}

TEST(Json, Utf8) {
  RunSuccessTest("\"ßâñć௵⇒\"", "ßâñć௵⇒",
                 "\"\\u00df\\u00e2\\u00f1\\u0107\\u0bf5\\u21d2\"");
  RunSuccessTest("\"\\u00df\\u00e2\\u00f1\\u0107\\u0bf5\\u21d2\"", "ßâñć௵⇒",
                 "\"\\u00df\\u00e2\\u00f1\\u0107\\u0bf5\\u21d2\"");
  // Testing UTF-8 character "𝄞", U+11D1E.
  RunSuccessTest("\"\xf0\x9d\x84\x9e\"", "\xf0\x9d\x84\x9e",
                 "\"\\ud834\\udd1e\"");
  RunSuccessTest("\"\\ud834\\udd1e\"", "\xf0\x9d\x84\x9e",
                 "\"\\ud834\\udd1e\"");
  RunSuccessTest("{\"\\ud834\\udd1e\":0}",
                 Json::Object{{"\xf0\x9d\x84\x9e", 0}},
                 "{\"\\ud834\\udd1e\":0}");
}

TEST(Json, NestedEmptyContainers) {
  RunSuccessTest(" [ [ ] , { } , [ ] ] ",
                 Json::Array{
                     Json::Array(),
                     Json::Object(),
                     Json::Array(),
                 },
                 "[[],{},[]]");
}

TEST(Json, EscapesAndControlCharactersInKeyStrings) {
  RunSuccessTest(" { \"\\u007f\x7f\\n\\r\\\"\\f\\b\\\\a , b\": 1, \"\": 0 } ",
                 Json::Object{
                     {"\u007f\u007f\n\r\"\f\b\\a , b", 1},
                     {"", 0},
                 },
                 "{\"\":0,\"\\u007f\\u007f\\n\\r\\\"\\f\\b\\\\a , b\":1}");
}

TEST(Json, WriterCutsOffInvalidUtf8) {
  RunSuccessTest("\"abc\xf0\x9d\x24\"", "abc\xf0\x9d\x24", "\"abc\"");
  RunSuccessTest("\"\xff\"", "\xff", "\"\"");
}

TEST(Json, ValidNumbers) {
  RunSuccessTest("[0, 42 , 0.0123, 123.456]",
                 Json::Array{
                     0,
                     42,
                     Json("0.0123", /*is_number=*/true),
                     Json("123.456", /*is_number=*/true),
                 },
                 "[0,42,0.0123,123.456]");
  RunSuccessTest("[1e4,-53.235e-31, 0.3e+3]",
                 Json::Array{
                     Json("1e4", /*is_number=*/true),
                     Json("-53.235e-31", /*is_number=*/true),
                     Json("0.3e+3", /*is_number=*/true),
                 },
                 "[1e4,-53.235e-31,0.3e+3]");
}

TEST(Json, Keywords) {
  RunSuccessTest("[true, false, null]",
                 Json::Array{
                     Json(true),
                     Json(false),
                     Json(),
                 },
                 "[true,false,null]");
}

void RunParseFailureTest(const char* input) {
  gpr_log(GPR_INFO, "parsing string \"%s\" - should fail", input);
  grpc_error_handle error = GRPC_ERROR_NONE;
  Json json = Json::Parse(input, &error);
  gpr_log(GPR_INFO, "error: %s", grpc_error_std_string(error).c_str());
  EXPECT_NE(error, GRPC_ERROR_NONE);
  GRPC_ERROR_UNREF(error);
}

TEST(Json, InvalidInput) {
  RunParseFailureTest("\\");
  RunParseFailureTest("nu ll");
  RunParseFailureTest("{\"foo\": bar}");
  RunParseFailureTest("{\"foo\": bar\"x\"}");
  RunParseFailureTest("fals");
  RunParseFailureTest("0,0 ");
  RunParseFailureTest("\"foo\",[]");
}

TEST(Json, UnterminatedString) { RunParseFailureTest("\"\\x"); }

TEST(Json, InvalidUtf16) {
  RunParseFailureTest("\"\\u123x");
  RunParseFailureTest("{\"\\u123x");
}

TEST(Json, ImbalancedSurrogatePairs) {
  RunParseFailureTest("\"\\ud834f");
  RunParseFailureTest("{\"\\ud834f\":0}");
  RunParseFailureTest("\"\\ud834\\n");
  RunParseFailureTest("{\"\\ud834\\n\":0}");
  RunParseFailureTest("\"\\udd1ef");
  RunParseFailureTest("{\"\\udd1ef\":0}");
  RunParseFailureTest("\"\\ud834\\ud834\"");
  RunParseFailureTest("{\"\\ud834\\ud834\"\":0}");
  RunParseFailureTest("\"\\ud834\\u1234\"");
  RunParseFailureTest("{\"\\ud834\\u1234\"\":0}");
  RunParseFailureTest("\"\\ud834]\"");
  RunParseFailureTest("{\"\\ud834]\"\":0}");
  RunParseFailureTest("\"\\ud834 \"");
  RunParseFailureTest("{\"\\ud834 \"\":0}");
  RunParseFailureTest("\"\\ud834\\\\\"");
  RunParseFailureTest("{\"\\ud834\\\\\"\":0}");
}

TEST(Json, EmbeddedInvalidWhitechars) {
  RunParseFailureTest("\"\n\"");
  RunParseFailureTest("\"\t\"");
}

TEST(Json, EmptyString) { RunParseFailureTest(""); }

TEST(Json, ExtraCharsAtEndOfParsing) {
  RunParseFailureTest("{},");
  RunParseFailureTest("{}x");
}

TEST(Json, ImbalancedContainers) {
  RunParseFailureTest("{}}");
  RunParseFailureTest("[]]");
  RunParseFailureTest("{{}");
  RunParseFailureTest("[[]");
  RunParseFailureTest("[}");
  RunParseFailureTest("{]");
}

TEST(Json, BadContainers) {
  RunParseFailureTest("{x}");
  RunParseFailureTest("{x=0,y}");
}

TEST(Json, DuplicateObjectKeys) { RunParseFailureTest("{\"x\": 1, \"x\": 1}"); }

TEST(Json, TrailingComma) {
  RunParseFailureTest("{,}");
  RunParseFailureTest("[1,2,3,4,]");
  RunParseFailureTest("{\"a\": 1, }");
}

TEST(Json, KeySyntaxInArray) { RunParseFailureTest("[\"x\":0]"); }

TEST(Json, InvalidNumbers) {
  RunParseFailureTest("1.");
  RunParseFailureTest("1e");
  RunParseFailureTest(".12");
  RunParseFailureTest("1.x");
  RunParseFailureTest("1.12x");
  RunParseFailureTest("1ex");
  RunParseFailureTest("1e12x");
  RunParseFailureTest(".12x");
  RunParseFailureTest("000");
};

TEST(Json, Equality) {
  // Null.
  EXPECT_EQ(Json(), Json());
  // Numbers.
  EXPECT_EQ(Json(1), Json(1));
  EXPECT_NE(Json(1), Json(2));
  EXPECT_EQ(Json(1), Json("1", /*is_number=*/true));
  EXPECT_EQ(Json("-5e5", /*is_number=*/true), Json("-5e5", /*is_number=*/true));
  // Booleans.
  EXPECT_EQ(Json(true), Json(true));
  EXPECT_EQ(Json(false), Json(false));
  EXPECT_NE(Json(true), Json(false));
  // Strings.
  EXPECT_EQ(Json("foo"), Json("foo"));
  EXPECT_NE(Json("foo"), Json("bar"));
  // Arrays.
  EXPECT_EQ(Json(Json::Array{"foo"}), Json(Json::Array{"foo"}));
  EXPECT_NE(Json(Json::Array{"foo"}), Json(Json::Array{"bar"}));
  // Objects.
  EXPECT_EQ(Json(Json::Object{{"foo", 1}}), Json(Json::Object{{"foo", 1}}));
  EXPECT_NE(Json(Json::Object{{"foo", 1}}), Json(Json::Object{{"foo", 2}}));
  EXPECT_NE(Json(Json::Object{{"foo", 1}}), Json(Json::Object{{"bar", 1}}));
  // Differing types.
  EXPECT_NE(Json(1), Json("foo"));
  EXPECT_NE(Json(1), Json(true));
  EXPECT_NE(Json(1), Json(Json::Array{}));
  EXPECT_NE(Json(1), Json(Json::Object{}));
  EXPECT_NE(Json(1), Json());
}

}  // namespace grpc_core

int main(int argc, char** argv) {
  grpc::testing::TestEnvironment env(argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
