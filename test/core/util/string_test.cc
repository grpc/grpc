//
//
// Copyright 2015 gRPC authors.
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
//

#include "src/core/util/string.h"

#include <grpc/support/alloc.h>
#include <grpc/support/string_util.h>
#include <limits.h>
#include <stddef.h>
#include <string.h>

#include "gtest/gtest.h"
#include "test/core/test_util/test_config.h"

TEST(StringTest, Strdup) {
  static const char* src1 = "hello world";
  char* dst1;

  dst1 = gpr_strdup(src1);
  ASSERT_STREQ(src1, dst1);
  gpr_free(dst1);

  ASSERT_EQ(nullptr, gpr_strdup(nullptr));
}

static void expect_dump(const char* buf, size_t len, uint32_t flags,
                        const char* result) {
  char* got = gpr_dump(buf, len, flags);
  ASSERT_STREQ(got, result);
  gpr_free(got);
}

TEST(StringTest, Dump) {
  expect_dump("\x01", 1, GPR_DUMP_HEX, "01");
  expect_dump("\x01", 1, GPR_DUMP_HEX | GPR_DUMP_ASCII, "01 '.'");
  expect_dump("\x01\x02", 2, GPR_DUMP_HEX, "01 02");
  expect_dump("\x01\x23\x45\x67\x89\xab\xcd\xef", 8, GPR_DUMP_HEX,
              "01 23 45 67 89 ab cd ef");
  expect_dump("ab", 2, GPR_DUMP_HEX | GPR_DUMP_ASCII, "61 62 'ab'");
}

static void test_pu32_fail(const char* s) {
  uint32_t out;
  ASSERT_FALSE(gpr_parse_bytes_to_uint32(s, strlen(s), &out));
}

static void test_pu32_succeed(const char* s, uint32_t want) {
  uint32_t out;
  ASSERT_TRUE(gpr_parse_bytes_to_uint32(s, strlen(s), &out));
  ASSERT_EQ(out, want);
}

TEST(StringTest, ParseUint32) {
  test_pu32_fail("-1");
  test_pu32_fail("a");
  test_pu32_fail("");
  test_pu32_succeed("0", 0);
  test_pu32_succeed("1", 1);
  test_pu32_succeed("2", 2);
  test_pu32_succeed("3", 3);
  test_pu32_succeed("4", 4);
  test_pu32_succeed("5", 5);
  test_pu32_succeed("6", 6);
  test_pu32_succeed("7", 7);
  test_pu32_succeed("8", 8);
  test_pu32_succeed("9", 9);
  test_pu32_succeed("10", 10);
  test_pu32_succeed("11", 11);
  test_pu32_succeed("12", 12);
  test_pu32_succeed("13", 13);
  test_pu32_succeed("14", 14);
  test_pu32_succeed("15", 15);
  test_pu32_succeed("16", 16);
  test_pu32_succeed("17", 17);
  test_pu32_succeed("18", 18);
  test_pu32_succeed("19", 19);
  test_pu32_succeed("1234567890", 1234567890);
  test_pu32_succeed("4294967295", 4294967295u);
  test_pu32_fail("4294967296");
  test_pu32_fail("4294967297");
  test_pu32_fail("4294967298");
  test_pu32_fail("4294967299");
}

TEST(StringTest, Asprintf) {
  char* buf;
  int i, j;

  // Print an empty string.
  ASSERT_EQ(gpr_asprintf(&buf, "%s", ""), 0);
  ASSERT_EQ(buf[0], '\0');
  gpr_free(buf);

  // Print strings of various lengths.
  for (i = 1; i < 100; i++) {
    ASSERT_EQ(gpr_asprintf(&buf, "%0*d", i, 1), i);

    // The buffer should resemble "000001\0".
    for (j = 0; j < i - 2; j++) {
      ASSERT_EQ(buf[j], '0');
    }
    ASSERT_EQ(buf[i - 1], '1');
    ASSERT_EQ(buf[i], '\0');
    gpr_free(buf);
  }
}

TEST(StringTest, Strjoin) {
  const char* parts[4] = {"one", "two", "three", "four"};
  size_t joined_len;
  char* joined;

  joined = gpr_strjoin(parts, 4, &joined_len);
  ASSERT_STREQ("onetwothreefour", joined);
  gpr_free(joined);

  joined = gpr_strjoin(parts, 0, &joined_len);
  ASSERT_STREQ("", joined);
  gpr_free(joined);

  joined = gpr_strjoin(parts, 1, &joined_len);
  ASSERT_STREQ("one", joined);
  gpr_free(joined);
}

TEST(StringTest, StrjoinSep) {
  const char* parts[4] = {"one", "two", "three", "four"};
  size_t joined_len;
  char* joined;

  joined = gpr_strjoin_sep(parts, 4, ", ", &joined_len);
  ASSERT_STREQ("one, two, three, four", joined);
  gpr_free(joined);

  // empty separator
  joined = gpr_strjoin_sep(parts, 4, "", &joined_len);
  ASSERT_STREQ("onetwothreefour", joined);
  gpr_free(joined);

  // degenerated case specifying zero input parts
  joined = gpr_strjoin_sep(parts, 0, ", ", &joined_len);
  ASSERT_STREQ("", joined);
  gpr_free(joined);

  // single part should have no separator
  joined = gpr_strjoin_sep(parts, 1, ", ", &joined_len);
  ASSERT_STREQ("one", joined);
  gpr_free(joined);
}

TEST(StringTest, Ltoa) {
  char* str;
  char buf[GPR_LTOA_MIN_BUFSIZE];

  // zero
  ASSERT_EQ(1, gpr_ltoa(0, buf));
  ASSERT_STREQ("0", buf);

  // positive number
  ASSERT_EQ(3, gpr_ltoa(123, buf));
  ASSERT_STREQ("123", buf);

  // negative number
  ASSERT_EQ(6, gpr_ltoa(-12345, buf));
  ASSERT_STREQ("-12345", buf);

  // large negative - we don't know the size of long in advance
  ASSERT_TRUE(gpr_asprintf(&str, "%lld", (long long)LONG_MIN));
  ASSERT_EQ(strlen(str), (size_t)gpr_ltoa(LONG_MIN, buf));
  ASSERT_STREQ(str, buf);
  gpr_free(str);
}

TEST(StringTest, Int64Toa) {
  char buf[GPR_INT64TOA_MIN_BUFSIZE];

  // zero
  ASSERT_EQ(1, int64_ttoa(0, buf));
  ASSERT_STREQ("0", buf);

  // positive
  ASSERT_EQ(3, int64_ttoa(123, buf));
  ASSERT_STREQ("123", buf);

  // large positive
  ASSERT_EQ(19, int64_ttoa(9223372036854775807LL, buf));
  ASSERT_STREQ("9223372036854775807", buf);

  // large negative
  ASSERT_EQ(20, int64_ttoa(-9223372036854775807LL - 1, buf));
  ASSERT_STREQ("-9223372036854775808", buf);
}

TEST(StringTest, Leftpad) {
  char* padded;

  padded = gpr_leftpad("foo", ' ', 5);
  ASSERT_STREQ("  foo", padded);
  gpr_free(padded);

  padded = gpr_leftpad("foo", ' ', 4);
  ASSERT_STREQ(" foo", padded);
  gpr_free(padded);

  padded = gpr_leftpad("foo", ' ', 3);
  ASSERT_STREQ("foo", padded);
  gpr_free(padded);

  padded = gpr_leftpad("foo", ' ', 2);
  ASSERT_STREQ("foo", padded);
  gpr_free(padded);

  padded = gpr_leftpad("foo", ' ', 1);
  ASSERT_STREQ("foo", padded);
  gpr_free(padded);

  padded = gpr_leftpad("foo", ' ', 0);
  ASSERT_STREQ("foo", padded);
  gpr_free(padded);

  padded = gpr_leftpad("foo", '0', 5);
  ASSERT_STREQ("00foo", padded);
  gpr_free(padded);
}

TEST(StringTest, Stricmp) {
  ASSERT_EQ(0, gpr_stricmp("hello", "hello"));
  ASSERT_EQ(0, gpr_stricmp("HELLO", "hello"));
  ASSERT_LT(gpr_stricmp("a", "b"), 0);
  ASSERT_GT(gpr_stricmp("b", "a"), 0);
}

TEST(StringTest, Memrchr) {
  ASSERT_EQ(nullptr, gpr_memrchr(nullptr, 'a', 0));
  ASSERT_EQ(nullptr, gpr_memrchr("", 'a', 0));
  ASSERT_EQ(nullptr, gpr_memrchr("hello", 'b', 5));
  ASSERT_STREQ((const char*)gpr_memrchr("hello", 'h', 5), "hello");
  ASSERT_STREQ((const char*)gpr_memrchr("hello", 'o', 5), "o");
  ASSERT_STREQ((const char*)gpr_memrchr("hello", 'l', 5), "lo");
}

TEST(StringTest, ParseBoolValue) {
  bool ret;
  ASSERT_TRUE(true == gpr_parse_bool_value("truE", &ret) && true == ret);
  ASSERT_TRUE(true == gpr_parse_bool_value("falsE", &ret) && false == ret);
  ASSERT_TRUE(true == gpr_parse_bool_value("1", &ret) && true == ret);
  ASSERT_TRUE(true == gpr_parse_bool_value("0", &ret) && false == ret);
  ASSERT_TRUE(true == gpr_parse_bool_value("Yes", &ret) && true == ret);
  ASSERT_TRUE(true == gpr_parse_bool_value("No", &ret) && false == ret);
  ASSERT_TRUE(true == gpr_parse_bool_value("Y", &ret) && true == ret);
  ASSERT_TRUE(true == gpr_parse_bool_value("N", &ret) && false == ret);
  ASSERT_EQ(false, gpr_parse_bool_value(nullptr, &ret));
  ASSERT_EQ(false, gpr_parse_bool_value("", &ret));
}

int main(int argc, char** argv) {
  grpc::testing::TestEnvironment env(&argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
