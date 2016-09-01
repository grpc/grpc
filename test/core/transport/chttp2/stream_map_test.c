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

#include "src/core/ext/transport/chttp2/transport/stream_map.h"
#include <grpc/support/log.h>
#include "test/core/util/test_config.h"

#define LOG_TEST(x) gpr_log(GPR_INFO, "%s", x)

/* test creation & destruction */
static void test_no_op(void) {
  grpc_chttp2_stream_map map;

  LOG_TEST("test_no_op");

  grpc_chttp2_stream_map_init(&map, 8);
  grpc_chttp2_stream_map_destroy(&map);
}

/* test lookup on an empty map */
static void test_empty_find(void) {
  grpc_chttp2_stream_map map;

  LOG_TEST("test_empty_find");

  grpc_chttp2_stream_map_init(&map, 8);
  GPR_ASSERT(NULL == grpc_chttp2_stream_map_find(&map, 39128));
  grpc_chttp2_stream_map_destroy(&map);
}

/* test it's safe to delete twice */
static void test_double_deletion(void) {
  grpc_chttp2_stream_map map;

  LOG_TEST("test_double_deletion");

  grpc_chttp2_stream_map_init(&map, 8);
  GPR_ASSERT(0 == grpc_chttp2_stream_map_size(&map));
  grpc_chttp2_stream_map_add(&map, 1, (void *)1);
  GPR_ASSERT((void *)1 == grpc_chttp2_stream_map_find(&map, 1));
  GPR_ASSERT(1 == grpc_chttp2_stream_map_size(&map));
  GPR_ASSERT((void *)1 == grpc_chttp2_stream_map_delete(&map, 1));
  GPR_ASSERT(0 == grpc_chttp2_stream_map_size(&map));
  GPR_ASSERT(NULL == grpc_chttp2_stream_map_find(&map, 1));
  GPR_ASSERT(NULL == grpc_chttp2_stream_map_delete(&map, 1));
  GPR_ASSERT(NULL == grpc_chttp2_stream_map_find(&map, 1));
  GPR_ASSERT(NULL == grpc_chttp2_stream_map_delete(&map, 1));
  GPR_ASSERT(NULL == grpc_chttp2_stream_map_find(&map, 1));
  GPR_ASSERT(NULL == grpc_chttp2_stream_map_delete(&map, 1));
  GPR_ASSERT(NULL == grpc_chttp2_stream_map_find(&map, 1));
  grpc_chttp2_stream_map_destroy(&map);
}

/* test add & lookup */
static void test_basic_add_find(uint32_t n) {
  grpc_chttp2_stream_map map;
  uint32_t i;
  size_t got;

  LOG_TEST("test_basic_add_find");
  gpr_log(GPR_INFO, "n = %d", n);

  grpc_chttp2_stream_map_init(&map, 8);
  GPR_ASSERT(0 == grpc_chttp2_stream_map_size(&map));
  for (i = 1; i <= n; i++) {
    grpc_chttp2_stream_map_add(&map, i, (void *)(uintptr_t)i);
  }
  GPR_ASSERT(n == grpc_chttp2_stream_map_size(&map));
  GPR_ASSERT(NULL == grpc_chttp2_stream_map_find(&map, 0));
  GPR_ASSERT(NULL == grpc_chttp2_stream_map_find(&map, n + 1));
  for (i = 1; i <= n; i++) {
    got = (uintptr_t)grpc_chttp2_stream_map_find(&map, i);
    GPR_ASSERT(i == got);
  }
  grpc_chttp2_stream_map_destroy(&map);
}

/* verify that for_each gets the right values during test_delete_evens_XXX */
static void verify_for_each(void *user_data, uint32_t stream_id, void *ptr) {
  uint32_t *for_each_check = user_data;
  GPR_ASSERT(ptr);
  GPR_ASSERT(*for_each_check == stream_id);
  *for_each_check += 2;
}

static void check_delete_evens(grpc_chttp2_stream_map *map, uint32_t n) {
  uint32_t for_each_check = 1;
  uint32_t i;
  size_t got;

  GPR_ASSERT(NULL == grpc_chttp2_stream_map_find(map, 0));
  GPR_ASSERT(NULL == grpc_chttp2_stream_map_find(map, n + 1));
  for (i = 1; i <= n; i++) {
    if (i & 1) {
      got = (uintptr_t)grpc_chttp2_stream_map_find(map, i);
      GPR_ASSERT(i == got);
    } else {
      GPR_ASSERT(NULL == grpc_chttp2_stream_map_find(map, i));
    }
  }

  grpc_chttp2_stream_map_for_each(map, verify_for_each, &for_each_check);
  if (n & 1) {
    GPR_ASSERT(for_each_check == n + 2);
  } else {
    GPR_ASSERT(for_each_check == n + 1);
  }
}

/* add a bunch of keys, delete the even ones, and make sure the map is
   consistent */
static void test_delete_evens_sweep(uint32_t n) {
  grpc_chttp2_stream_map map;
  uint32_t i;

  LOG_TEST("test_delete_evens_sweep");
  gpr_log(GPR_INFO, "n = %d", n);

  grpc_chttp2_stream_map_init(&map, 8);
  for (i = 1; i <= n; i++) {
    grpc_chttp2_stream_map_add(&map, i, (void *)(uintptr_t)i);
  }
  for (i = 1; i <= n; i++) {
    if ((i & 1) == 0) {
      GPR_ASSERT((void *)(uintptr_t)i ==
                 grpc_chttp2_stream_map_delete(&map, i));
    }
  }
  check_delete_evens(&map, n);
  grpc_chttp2_stream_map_destroy(&map);
}

/* add a bunch of keys, delete the even ones immediately, and make sure the map
   is consistent */
static void test_delete_evens_incremental(uint32_t n) {
  grpc_chttp2_stream_map map;
  uint32_t i;

  LOG_TEST("test_delete_evens_incremental");
  gpr_log(GPR_INFO, "n = %d", n);

  grpc_chttp2_stream_map_init(&map, 8);
  for (i = 1; i <= n; i++) {
    grpc_chttp2_stream_map_add(&map, i, (void *)(uintptr_t)i);
    if ((i & 1) == 0) {
      grpc_chttp2_stream_map_delete(&map, i);
    }
  }
  check_delete_evens(&map, n);
  grpc_chttp2_stream_map_destroy(&map);
}

/* add a bunch of keys, delete old ones after some time, ensure the
   backing array does not grow */
static void test_periodic_compaction(uint32_t n) {
  grpc_chttp2_stream_map map;
  uint32_t i;
  uint32_t del;

  LOG_TEST("test_periodic_compaction");
  gpr_log(GPR_INFO, "n = %d", n);

  grpc_chttp2_stream_map_init(&map, 16);
  GPR_ASSERT(map.capacity == 16);
  for (i = 1; i <= n; i++) {
    grpc_chttp2_stream_map_add(&map, i, (void *)(uintptr_t)i);
    if (i > 8) {
      del = i - 8;
      GPR_ASSERT((void *)(uintptr_t)del ==
                 grpc_chttp2_stream_map_delete(&map, del));
    }
  }
  GPR_ASSERT(map.capacity == 16);
  grpc_chttp2_stream_map_destroy(&map);
}

int main(int argc, char **argv) {
  uint32_t n = 1;
  uint32_t prev = 1;
  uint32_t tmp;

  grpc_test_init(argc, argv);

  test_no_op();
  test_empty_find();
  test_double_deletion();

  while (n < 100000) {
    test_basic_add_find(n);
    test_delete_evens_sweep(n);
    test_delete_evens_incremental(n);
    test_periodic_compaction(n);

    tmp = n;
    n += prev;
    prev = tmp;
  }

  return 0;
}
