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

#include "src/core/transport/chttp2/hpack_table.h"

#include <string.h>
#include <stdio.h>

#include "src/core/support/string.h"
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>
#include "test/core/util/test_config.h"

#define LOG_TEST(x) gpr_log(GPR_INFO, "%s", x)

static void assert_str(const grpc_chttp2_hptbl *tbl, grpc_mdstr *mdstr,
                       const char *str) {
  GPR_ASSERT(gpr_slice_str_cmp(mdstr->slice, str) == 0);
}

static void assert_index(const grpc_chttp2_hptbl *tbl, gpr_uint32 idx,
                         const char *key, const char *value) {
  grpc_mdelem *md = grpc_chttp2_hptbl_lookup(tbl, idx);
  assert_str(tbl, md->key, key);
  assert_str(tbl, md->value, value);
}

static void test_static_lookup(void) {
  grpc_chttp2_hptbl tbl;
  grpc_mdctx *mdctx;

  mdctx = grpc_mdctx_create();
  grpc_chttp2_hptbl_init(&tbl, mdctx);

  LOG_TEST("test_static_lookup");
  assert_index(&tbl, 1, ":authority", "");
  assert_index(&tbl, 2, ":method", "GET");
  assert_index(&tbl, 3, ":method", "POST");
  assert_index(&tbl, 4, ":path", "/");
  assert_index(&tbl, 5, ":path", "/index.html");
  assert_index(&tbl, 6, ":scheme", "http");
  assert_index(&tbl, 7, ":scheme", "https");
  assert_index(&tbl, 8, ":status", "200");
  assert_index(&tbl, 9, ":status", "204");
  assert_index(&tbl, 10, ":status", "206");
  assert_index(&tbl, 11, ":status", "304");
  assert_index(&tbl, 12, ":status", "400");
  assert_index(&tbl, 13, ":status", "404");
  assert_index(&tbl, 14, ":status", "500");
  assert_index(&tbl, 15, "accept-charset", "");
  assert_index(&tbl, 16, "accept-encoding", "gzip, deflate");
  assert_index(&tbl, 17, "accept-language", "");
  assert_index(&tbl, 18, "accept-ranges", "");
  assert_index(&tbl, 19, "accept", "");
  assert_index(&tbl, 20, "access-control-allow-origin", "");
  assert_index(&tbl, 21, "age", "");
  assert_index(&tbl, 22, "allow", "");
  assert_index(&tbl, 23, "authorization", "");
  assert_index(&tbl, 24, "cache-control", "");
  assert_index(&tbl, 25, "content-disposition", "");
  assert_index(&tbl, 26, "content-encoding", "");
  assert_index(&tbl, 27, "content-language", "");
  assert_index(&tbl, 28, "content-length", "");
  assert_index(&tbl, 29, "content-location", "");
  assert_index(&tbl, 30, "content-range", "");
  assert_index(&tbl, 31, "content-type", "");
  assert_index(&tbl, 32, "cookie", "");
  assert_index(&tbl, 33, "date", "");
  assert_index(&tbl, 34, "etag", "");
  assert_index(&tbl, 35, "expect", "");
  assert_index(&tbl, 36, "expires", "");
  assert_index(&tbl, 37, "from", "");
  assert_index(&tbl, 38, "host", "");
  assert_index(&tbl, 39, "if-match", "");
  assert_index(&tbl, 40, "if-modified-since", "");
  assert_index(&tbl, 41, "if-none-match", "");
  assert_index(&tbl, 42, "if-range", "");
  assert_index(&tbl, 43, "if-unmodified-since", "");
  assert_index(&tbl, 44, "last-modified", "");
  assert_index(&tbl, 45, "link", "");
  assert_index(&tbl, 46, "location", "");
  assert_index(&tbl, 47, "max-forwards", "");
  assert_index(&tbl, 48, "proxy-authenticate", "");
  assert_index(&tbl, 49, "proxy-authorization", "");
  assert_index(&tbl, 50, "range", "");
  assert_index(&tbl, 51, "referer", "");
  assert_index(&tbl, 52, "refresh", "");
  assert_index(&tbl, 53, "retry-after", "");
  assert_index(&tbl, 54, "server", "");
  assert_index(&tbl, 55, "set-cookie", "");
  assert_index(&tbl, 56, "strict-transport-security", "");
  assert_index(&tbl, 57, "transfer-encoding", "");
  assert_index(&tbl, 58, "user-agent", "");
  assert_index(&tbl, 59, "vary", "");
  assert_index(&tbl, 60, "via", "");
  assert_index(&tbl, 61, "www-authenticate", "");

  grpc_chttp2_hptbl_destroy(&tbl);
  grpc_mdctx_unref(mdctx);
}

static void test_many_additions(void) {
  grpc_chttp2_hptbl tbl;
  int i;
  char *key;
  char *value;
  grpc_mdctx *mdctx;

  LOG_TEST("test_many_additions");

  mdctx = grpc_mdctx_create();
  grpc_chttp2_hptbl_init(&tbl, mdctx);

  for (i = 0; i < 1000000; i++) {
    grpc_mdelem *elem;
    gpr_asprintf(&key, "K:%d", i);
    gpr_asprintf(&value, "VALUE:%d", i);
    elem = grpc_mdelem_from_strings(mdctx, key, value);
    GPR_ASSERT(grpc_chttp2_hptbl_add(&tbl, elem));
    GRPC_MDELEM_UNREF(elem);
    assert_index(&tbl, 1 + GRPC_CHTTP2_LAST_STATIC_ENTRY, key, value);
    gpr_free(key);
    gpr_free(value);
    if (i) {
      gpr_asprintf(&key, "K:%d", i - 1);
      gpr_asprintf(&value, "VALUE:%d", i - 1);
      assert_index(&tbl, 2 + GRPC_CHTTP2_LAST_STATIC_ENTRY, key, value);
      gpr_free(key);
      gpr_free(value);
    }
  }

  grpc_chttp2_hptbl_destroy(&tbl);
  grpc_mdctx_unref(mdctx);
}

static grpc_chttp2_hptbl_find_result find_simple(grpc_chttp2_hptbl *tbl,
                                                 const char *key,
                                                 const char *value) {
  grpc_mdelem *md = grpc_mdelem_from_strings(tbl->mdctx, key, value);
  grpc_chttp2_hptbl_find_result r = grpc_chttp2_hptbl_find(tbl, md);
  GRPC_MDELEM_UNREF(md);
  return r;
}

static void test_find(void) {
  grpc_chttp2_hptbl tbl;
  gpr_uint32 i;
  char buffer[32];
  grpc_mdctx *mdctx;
  grpc_mdelem *elem;
  grpc_chttp2_hptbl_find_result r;

  LOG_TEST("test_find");

  mdctx = grpc_mdctx_create();
  grpc_chttp2_hptbl_init(&tbl, mdctx);
  elem = grpc_mdelem_from_strings(mdctx, "abc", "xyz");
  GPR_ASSERT(grpc_chttp2_hptbl_add(&tbl, elem));
  GRPC_MDELEM_UNREF(elem);
  elem = grpc_mdelem_from_strings(mdctx, "abc", "123");
  GPR_ASSERT(grpc_chttp2_hptbl_add(&tbl, elem));
  GRPC_MDELEM_UNREF(elem);
  elem = grpc_mdelem_from_strings(mdctx, "x", "1");
  GPR_ASSERT(grpc_chttp2_hptbl_add(&tbl, elem));
  GRPC_MDELEM_UNREF(elem);

  r = find_simple(&tbl, "abc", "123");
  GPR_ASSERT(r.index == 2 + GRPC_CHTTP2_LAST_STATIC_ENTRY);
  GPR_ASSERT(r.has_value == 1);

  r = find_simple(&tbl, "abc", "xyz");
  GPR_ASSERT(r.index == 3 + GRPC_CHTTP2_LAST_STATIC_ENTRY);
  GPR_ASSERT(r.has_value == 1);

  r = find_simple(&tbl, "x", "1");
  GPR_ASSERT(r.index == 1 + GRPC_CHTTP2_LAST_STATIC_ENTRY);
  GPR_ASSERT(r.has_value == 1);

  r = find_simple(&tbl, "x", "2");
  GPR_ASSERT(r.index == 1 + GRPC_CHTTP2_LAST_STATIC_ENTRY);
  GPR_ASSERT(r.has_value == 0);

  r = find_simple(&tbl, "vary", "some-vary-arg");
  GPR_ASSERT(r.index == 59);
  GPR_ASSERT(r.has_value == 0);

  r = find_simple(&tbl, "accept-encoding", "gzip, deflate");
  GPR_ASSERT(r.index == 16);
  GPR_ASSERT(r.has_value == 1);

  r = find_simple(&tbl, "accept-encoding", "gzip");
  GPR_ASSERT(r.index == 16);
  GPR_ASSERT(r.has_value == 0);

  r = find_simple(&tbl, ":method", "GET");
  GPR_ASSERT(r.index == 2);
  GPR_ASSERT(r.has_value == 1);

  r = find_simple(&tbl, ":method", "POST");
  GPR_ASSERT(r.index == 3);
  GPR_ASSERT(r.has_value == 1);

  r = find_simple(&tbl, ":method", "PUT");
  GPR_ASSERT(r.index == 2 || r.index == 3);
  GPR_ASSERT(r.has_value == 0);

  r = find_simple(&tbl, "this-does-not-exist", "");
  GPR_ASSERT(r.index == 0);
  GPR_ASSERT(r.has_value == 0);

  /* overflow the string buffer, check find still works */
  for (i = 0; i < 10000; i++) {
    gpr_ltoa(i, buffer);
    elem = grpc_mdelem_from_strings(mdctx, "test", buffer);
    GPR_ASSERT(grpc_chttp2_hptbl_add(&tbl, elem));
    GRPC_MDELEM_UNREF(elem);
  }

  r = find_simple(&tbl, "abc", "123");
  GPR_ASSERT(r.index == 0);
  GPR_ASSERT(r.has_value == 0);

  r = find_simple(&tbl, "test", "9999");
  GPR_ASSERT(r.index == 1 + GRPC_CHTTP2_LAST_STATIC_ENTRY);
  GPR_ASSERT(r.has_value == 1);

  r = find_simple(&tbl, "test", "9998");
  GPR_ASSERT(r.index == 2 + GRPC_CHTTP2_LAST_STATIC_ENTRY);
  GPR_ASSERT(r.has_value == 1);

  for (i = 0; i < tbl.num_ents; i++) {
    gpr_uint32 expect = 9999 - i;
    gpr_ltoa(expect, buffer);

    r = find_simple(&tbl, "test", buffer);
    GPR_ASSERT(r.index == i + 1 + GRPC_CHTTP2_LAST_STATIC_ENTRY);
    GPR_ASSERT(r.has_value == 1);
  }

  r = find_simple(&tbl, "test", "10000");
  GPR_ASSERT(r.index != 0);
  GPR_ASSERT(r.has_value == 0);

  grpc_chttp2_hptbl_destroy(&tbl);
  grpc_mdctx_unref(mdctx);
}

int main(int argc, char **argv) {
  grpc_test_init(argc, argv);
  test_static_lookup();
  test_many_additions();
  test_find();
  return 0;
}
