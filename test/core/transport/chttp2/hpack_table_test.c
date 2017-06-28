/*
 *
 * Copyright 2015 gRPC authors.
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

#include "src/core/ext/transport/chttp2/transport/hpack_table.h"

#include <stdio.h>
#include <string.h>

#include <grpc/grpc.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>

#include "src/core/lib/support/string.h"
#include "test/core/util/test_config.h"

#define LOG_TEST(x) gpr_log(GPR_INFO, "%s", x)

static void assert_str(const grpc_chttp2_hptbl *tbl, grpc_slice mdstr,
                       const char *str) {
  GPR_ASSERT(grpc_slice_str_cmp(mdstr, str) == 0);
}

static void assert_index(const grpc_chttp2_hptbl *tbl, uint32_t idx,
                         const char *key, const char *value) {
  grpc_mdelem md = grpc_chttp2_hptbl_lookup(tbl, idx);
  assert_str(tbl, GRPC_MDKEY(md), key);
  assert_str(tbl, GRPC_MDVALUE(md), value);
}

static void test_static_lookup(void) {
  grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
  grpc_chttp2_hptbl tbl;

  grpc_chttp2_hptbl_init(&exec_ctx, &tbl);

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

  grpc_chttp2_hptbl_destroy(&exec_ctx, &tbl);
  grpc_exec_ctx_finish(&exec_ctx);
}

static void test_many_additions(void) {
  grpc_chttp2_hptbl tbl;
  int i;
  char *key;
  char *value;

  LOG_TEST("test_many_additions");

  grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
  grpc_chttp2_hptbl_init(&exec_ctx, &tbl);

  for (i = 0; i < 100000; i++) {
    grpc_mdelem elem;
    gpr_asprintf(&key, "K:%d", i);
    gpr_asprintf(&value, "VALUE:%d", i);
    elem =
        grpc_mdelem_from_slices(&exec_ctx, grpc_slice_from_copied_string(key),
                                grpc_slice_from_copied_string(value));
    GPR_ASSERT(grpc_chttp2_hptbl_add(&exec_ctx, &tbl, elem) == GRPC_ERROR_NONE);
    GRPC_MDELEM_UNREF(&exec_ctx, elem);
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

  grpc_chttp2_hptbl_destroy(&exec_ctx, &tbl);
  grpc_exec_ctx_finish(&exec_ctx);
}

static grpc_chttp2_hptbl_find_result find_simple(grpc_chttp2_hptbl *tbl,
                                                 const char *key,
                                                 const char *value) {
  grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
  grpc_mdelem md =
      grpc_mdelem_from_slices(&exec_ctx, grpc_slice_from_copied_string(key),
                              grpc_slice_from_copied_string(value));
  grpc_chttp2_hptbl_find_result r = grpc_chttp2_hptbl_find(tbl, md);
  GRPC_MDELEM_UNREF(&exec_ctx, md);
  grpc_exec_ctx_finish(&exec_ctx);
  return r;
}

static void test_find(void) {
  grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
  grpc_chttp2_hptbl tbl;
  uint32_t i;
  char buffer[32];
  grpc_mdelem elem;
  grpc_chttp2_hptbl_find_result r;

  LOG_TEST("test_find");

  grpc_chttp2_hptbl_init(&exec_ctx, &tbl);
  elem =
      grpc_mdelem_from_slices(&exec_ctx, grpc_slice_from_static_string("abc"),
                              grpc_slice_from_static_string("xyz"));
  GPR_ASSERT(grpc_chttp2_hptbl_add(&exec_ctx, &tbl, elem) == GRPC_ERROR_NONE);
  GRPC_MDELEM_UNREF(&exec_ctx, elem);
  elem =
      grpc_mdelem_from_slices(&exec_ctx, grpc_slice_from_static_string("abc"),
                              grpc_slice_from_static_string("123"));
  GPR_ASSERT(grpc_chttp2_hptbl_add(&exec_ctx, &tbl, elem) == GRPC_ERROR_NONE);
  GRPC_MDELEM_UNREF(&exec_ctx, elem);
  elem = grpc_mdelem_from_slices(&exec_ctx, grpc_slice_from_static_string("x"),
                                 grpc_slice_from_static_string("1"));
  GPR_ASSERT(grpc_chttp2_hptbl_add(&exec_ctx, &tbl, elem) == GRPC_ERROR_NONE);
  GRPC_MDELEM_UNREF(&exec_ctx, elem);

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
    int64_ttoa(i, buffer);
    elem = grpc_mdelem_from_slices(&exec_ctx,
                                   grpc_slice_from_static_string("test"),
                                   grpc_slice_from_copied_string(buffer));
    GPR_ASSERT(grpc_chttp2_hptbl_add(&exec_ctx, &tbl, elem) == GRPC_ERROR_NONE);
    GRPC_MDELEM_UNREF(&exec_ctx, elem);
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
    uint32_t expect = 9999 - i;
    int64_ttoa(expect, buffer);

    r = find_simple(&tbl, "test", buffer);
    GPR_ASSERT(r.index == i + 1 + GRPC_CHTTP2_LAST_STATIC_ENTRY);
    GPR_ASSERT(r.has_value == 1);
  }

  r = find_simple(&tbl, "test", "10000");
  GPR_ASSERT(r.index != 0);
  GPR_ASSERT(r.has_value == 0);

  grpc_chttp2_hptbl_destroy(&exec_ctx, &tbl);
  grpc_exec_ctx_finish(&exec_ctx);
}

int main(int argc, char **argv) {
  grpc_test_init(argc, argv);
  grpc_init();
  test_static_lookup();
  test_many_additions();
  test_find();
  grpc_shutdown();
  return 0;
}
