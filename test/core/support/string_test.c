/*
 *
 * Copyright 2015, Google Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *     * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 * copyright notice, this list of conditions and the following disclaimer
 * in the documentation and/or other materials provided with the
 * distribution.
 *     * Neither the name of Google Inc. nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

#include "src/core/lib/support/string.h"

#include <limits.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>
#include <grpc/support/useful.h>
#include "test/core/util/test_config.h"

#define LOG_TEST_NAME(x) gpr_log(GPR_INFO, "%s", x)

static void test_strdup(void) {
  static const char *src1 = "hello world";
  char *dst1;

  LOG_TEST_NAME("test_strdup");

  dst1 = gpr_strdup(src1);
  GPR_ASSERT(0 == strcmp(src1, dst1));
  gpr_free(dst1);

  GPR_ASSERT(NULL == gpr_strdup(NULL));
}

static void expect_dump(const char *buf, size_t len, uint32_t flags,
                        const char *result) {
  char *got = gpr_dump(buf, len, flags);
  GPR_ASSERT(0 == strcmp(got, result));
  gpr_free(got);
}

static void test_dump(void) {
  LOG_TEST_NAME("test_dump");
  expect_dump("\x01", 1, GPR_DUMP_HEX, "01");
  expect_dump("\x01", 1, GPR_DUMP_HEX | GPR_DUMP_ASCII, "01 '.'");
  expect_dump("\x01\x02", 2, GPR_DUMP_HEX, "01 02");
  expect_dump("\x01\x23\x45\x67\x89\xab\xcd\xef", 8, GPR_DUMP_HEX,
              "01 23 45 67 89 ab cd ef");
  expect_dump("ab", 2, GPR_DUMP_HEX | GPR_DUMP_ASCII, "61 62 'ab'");
}

static void test_pu32_fail(const char *s) {
  uint32_t out;
  GPR_ASSERT(!gpr_parse_bytes_to_uint32(s, strlen(s), &out));
}

static void test_pu32_succeed(const char *s, uint32_t want) {
  uint32_t out;
  GPR_ASSERT(gpr_parse_bytes_to_uint32(s, strlen(s), &out));
  GPR_ASSERT(out == want);
}

static void test_parse_uint32(void) {
  LOG_TEST_NAME("test_parse_uint32");

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

static void test_asprintf(void) {
  char *buf;
  int i, j;

  LOG_TEST_NAME("test_asprintf");

  /* Print an empty string. */
  GPR_ASSERT(gpr_asprintf(&buf, "%s", "") == 0);
  GPR_ASSERT(buf[0] == '\0');
  gpr_free(buf);

  /* Print strings of various lengths. */
  for (i = 1; i < 100; i++) {
    GPR_ASSERT(gpr_asprintf(&buf, "%0*d", i, 1) == i);

    /* The buffer should resemble "000001\0". */
    for (j = 0; j < i - 2; j++) {
      GPR_ASSERT(buf[j] == '0');
    }
    GPR_ASSERT(buf[i - 1] == '1');
    GPR_ASSERT(buf[i] == '\0');
    gpr_free(buf);
  }
}

static void test_strjoin(void) {
  const char *parts[4] = {"one", "two", "three", "four"};
  size_t joined_len;
  char *joined;

  LOG_TEST_NAME("test_strjoin");

  joined = gpr_strjoin(parts, 4, &joined_len);
  GPR_ASSERT(0 == strcmp("onetwothreefour", joined));
  gpr_free(joined);

  joined = gpr_strjoin(parts, 0, &joined_len);
  GPR_ASSERT(0 == strcmp("", joined));
  gpr_free(joined);

  joined = gpr_strjoin(parts, 1, &joined_len);
  GPR_ASSERT(0 == strcmp("one", joined));
  gpr_free(joined);
}

static void test_strjoin_sep(void) {
  const char *parts[4] = {"one", "two", "three", "four"};
  size_t joined_len;
  char *joined;

  LOG_TEST_NAME("test_strjoin_sep");

  joined = gpr_strjoin_sep(parts, 4, ", ", &joined_len);
  GPR_ASSERT(0 == strcmp("one, two, three, four", joined));
  gpr_free(joined);

  /* empty separator */
  joined = gpr_strjoin_sep(parts, 4, "", &joined_len);
  GPR_ASSERT(0 == strcmp("onetwothreefour", joined));
  gpr_free(joined);

  /* degenerated case specifying zero input parts */
  joined = gpr_strjoin_sep(parts, 0, ", ", &joined_len);
  GPR_ASSERT(0 == strcmp("", joined));
  gpr_free(joined);

  /* single part should have no separator */
  joined = gpr_strjoin_sep(parts, 1, ", ", &joined_len);
  GPR_ASSERT(0 == strcmp("one", joined));
  gpr_free(joined);
}

static void test_ltoa() {
  char *str;
  char buf[GPR_LTOA_MIN_BUFSIZE];

  LOG_TEST_NAME("test_ltoa");

  /* zero */
  GPR_ASSERT(1 == gpr_ltoa(0, buf));
  GPR_ASSERT(0 == strcmp("0", buf));

  /* positive number */
  GPR_ASSERT(3 == gpr_ltoa(123, buf));
  GPR_ASSERT(0 == strcmp("123", buf));

  /* negative number */
  GPR_ASSERT(6 == gpr_ltoa(-12345, buf));
  GPR_ASSERT(0 == strcmp("-12345", buf));

  /* large negative - we don't know the size of long in advance */
  GPR_ASSERT(gpr_asprintf(&str, "%lld", (long long)LONG_MIN));
  GPR_ASSERT(strlen(str) == (size_t)gpr_ltoa(LONG_MIN, buf));
  GPR_ASSERT(0 == strcmp(str, buf));
  gpr_free(str);
}

static void test_int64toa() {
  char buf[GPR_INT64TOA_MIN_BUFSIZE];

  LOG_TEST_NAME("test_int64toa");

  /* zero */
  GPR_ASSERT(1 == int64_ttoa(0, buf));
  GPR_ASSERT(0 == strcmp("0", buf));

  /* positive */
  GPR_ASSERT(3 == int64_ttoa(123, buf));
  GPR_ASSERT(0 == strcmp("123", buf));

  /* large positive */
  GPR_ASSERT(19 == int64_ttoa(9223372036854775807LL, buf));
  GPR_ASSERT(0 == strcmp("9223372036854775807", buf));

  /* large negative */
  GPR_ASSERT(20 == int64_ttoa(-9223372036854775807LL - 1, buf));
  GPR_ASSERT(0 == strcmp("-9223372036854775808", buf));
}

static void test_leftpad() {
  char *padded;

  LOG_TEST_NAME("test_leftpad");

  padded = gpr_leftpad("foo", ' ', 5);
  GPR_ASSERT(0 == strcmp("  foo", padded));
  gpr_free(padded);

  padded = gpr_leftpad("foo", ' ', 4);
  GPR_ASSERT(0 == strcmp(" foo", padded));
  gpr_free(padded);

  padded = gpr_leftpad("foo", ' ', 3);
  GPR_ASSERT(0 == strcmp("foo", padded));
  gpr_free(padded);

  padded = gpr_leftpad("foo", ' ', 2);
  GPR_ASSERT(0 == strcmp("foo", padded));
  gpr_free(padded);

  padded = gpr_leftpad("foo", ' ', 1);
  GPR_ASSERT(0 == strcmp("foo", padded));
  gpr_free(padded);

  padded = gpr_leftpad("foo", ' ', 0);
  GPR_ASSERT(0 == strcmp("foo", padded));
  gpr_free(padded);

  padded = gpr_leftpad("foo", '0', 5);
  GPR_ASSERT(0 == strcmp("00foo", padded));
  gpr_free(padded);
}

static void test_stricmp(void) {
  LOG_TEST_NAME("test_stricmp");

  GPR_ASSERT(0 == gpr_stricmp("hello", "hello"));
  GPR_ASSERT(0 == gpr_stricmp("HELLO", "hello"));
  GPR_ASSERT(gpr_stricmp("a", "b") < 0);
  GPR_ASSERT(gpr_stricmp("b", "a") > 0);
}

static void test_memrchr(void) {
  LOG_TEST_NAME("test_memrchr");

  GPR_ASSERT(NULL == gpr_memrchr(NULL, 'a', 0));
  GPR_ASSERT(NULL == gpr_memrchr("", 'a', 0));
  GPR_ASSERT(NULL == gpr_memrchr("hello", 'b', 5));
  GPR_ASSERT(0 == strcmp((const char *)gpr_memrchr("hello", 'h', 5), "hello"));
  GPR_ASSERT(0 == strcmp((const char *)gpr_memrchr("hello", 'o', 5), "o"));
  GPR_ASSERT(0 == strcmp((const char *)gpr_memrchr("hello", 'l', 5), "lo"));
}

int main(int argc, char **argv) {
  grpc_test_init(argc, argv);
  test_strdup();
  test_dump();
  test_parse_uint32();
  test_asprintf();
  test_strjoin();
  test_strjoin_sep();
  test_ltoa();
  test_int64toa();
  test_leftpad();
  test_stricmp();
  test_memrchr();
  return 0;
}
