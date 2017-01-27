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

#include <grpc/slice.h>

#include <string.h>

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include "test/core/util/test_config.h"

#define LOG_TEST_NAME(x) gpr_log(GPR_INFO, "%s", x);

static void test_slice_malloc_returns_something_sensible(void) {
  /* Calls grpc_slice_create for various lengths and verifies the internals for
     consistency. */
  size_t length;
  size_t i;
  grpc_slice slice;

  LOG_TEST_NAME("test_slice_malloc_returns_something_sensible");

  for (length = 0; length <= 1024; length++) {
    slice = grpc_slice_malloc(length);
    /* If there is a length, slice.data must be non-NULL. If length is zero
       we don't care. */
    if (length) {
      GPR_ASSERT(GRPC_SLICE_START_PTR(slice));
    }
    /* Returned slice length must be what was requested. */
    GPR_ASSERT(GRPC_SLICE_LENGTH(slice) == length);
    /* If the slice has a refcount, it must be destroyable. */
    if (slice.refcount) {
      GPR_ASSERT(slice.refcount->ref != NULL);
      GPR_ASSERT(slice.refcount->unref != NULL);
    }
    /* We must be able to write to every byte of the data */
    for (i = 0; i < length; i++) {
      GRPC_SLICE_START_PTR(slice)[i] = (uint8_t)i;
    }
    /* And finally we must succeed in destroying the slice */
    grpc_slice_unref(slice);
  }
}

static void do_nothing(void *ignored) {}

static void test_slice_new_returns_something_sensible(void) {
  uint8_t x;

  grpc_slice slice = grpc_slice_new(&x, 1, do_nothing);
  GPR_ASSERT(slice.refcount);
  GPR_ASSERT(slice.data.refcounted.bytes == &x);
  GPR_ASSERT(slice.data.refcounted.length == 1);
  grpc_slice_unref(slice);
}

/* destroy function that sets a mark to indicate it was called. */
static void set_mark(void *p) { *((int *)p) = 1; }

static void test_slice_new_with_user_data(void) {
  int marker = 0;
  uint8_t buf[2];
  grpc_slice slice;

  buf[0] = 0;
  buf[1] = 1;
  slice = grpc_slice_new_with_user_data(buf, 2, set_mark, &marker);
  GPR_ASSERT(marker == 0);
  GPR_ASSERT(GRPC_SLICE_LENGTH(slice) == 2);
  GPR_ASSERT(GRPC_SLICE_START_PTR(slice)[0] == 0);
  GPR_ASSERT(GRPC_SLICE_START_PTR(slice)[1] == 1);

  /* unref should cause destroy function to run. */
  grpc_slice_unref(slice);
  GPR_ASSERT(marker == 1);
}

static int do_nothing_with_len_1_calls = 0;

static void do_nothing_with_len_1(void *ignored, size_t len) {
  GPR_ASSERT(len == 1);
  do_nothing_with_len_1_calls++;
}

static void test_slice_new_with_len_returns_something_sensible(void) {
  uint8_t x;
  int num_refs = 5; /* To test adding/removing an arbitrary number of refs */
  int i;

  grpc_slice slice = grpc_slice_new_with_len(&x, 1, do_nothing_with_len_1);
  GPR_ASSERT(slice.refcount); /* ref count is initialized to 1 at this point */
  GPR_ASSERT(slice.data.refcounted.bytes == &x);
  GPR_ASSERT(slice.data.refcounted.length == 1);
  GPR_ASSERT(do_nothing_with_len_1_calls == 0);

  /* Add an arbitrary number of refs to the slice and remoe the refs. This is to
     make sure that that the destroy callback (i.e do_nothing_with_len_1()) is
     not called until the last unref operation */
  for (i = 0; i < num_refs; i++) {
    grpc_slice_ref(slice);
  }
  for (i = 0; i < num_refs; i++) {
    grpc_slice_unref(slice);
  }
  GPR_ASSERT(do_nothing_with_len_1_calls == 0); /* Shouldn't be called yet */

  /* last unref */
  grpc_slice_unref(slice);
  GPR_ASSERT(do_nothing_with_len_1_calls == 1);
}

static void test_slice_sub_works(unsigned length) {
  grpc_slice slice;
  grpc_slice sub;
  unsigned i, j, k;

  LOG_TEST_NAME("test_slice_sub_works");
  gpr_log(GPR_INFO, "length=%d", length);

  /* Create a slice in which each byte is equal to the distance from it to the
     beginning of the slice. */
  slice = grpc_slice_malloc(length);
  for (i = 0; i < length; i++) {
    GRPC_SLICE_START_PTR(slice)[i] = (uint8_t)i;
  }

  /* Ensure that for all subsets length is correct and that we start on the
     correct byte. Additionally check that no copies were made. */
  for (i = 0; i < length; i++) {
    for (j = i; j < length; j++) {
      sub = grpc_slice_sub(slice, i, j);
      GPR_ASSERT(GRPC_SLICE_LENGTH(sub) == j - i);
      for (k = 0; k < j - i; k++) {
        GPR_ASSERT(GRPC_SLICE_START_PTR(sub)[k] == (uint8_t)(i + k));
      }
      grpc_slice_unref(sub);
    }
  }
  grpc_slice_unref(slice);
}

static void check_head_tail(grpc_slice slice, grpc_slice head,
                            grpc_slice tail) {
  GPR_ASSERT(GRPC_SLICE_LENGTH(slice) ==
             GRPC_SLICE_LENGTH(head) + GRPC_SLICE_LENGTH(tail));
  GPR_ASSERT(0 == memcmp(GRPC_SLICE_START_PTR(slice),
                         GRPC_SLICE_START_PTR(head), GRPC_SLICE_LENGTH(head)));
  GPR_ASSERT(0 == memcmp(GRPC_SLICE_START_PTR(slice) + GRPC_SLICE_LENGTH(head),
                         GRPC_SLICE_START_PTR(tail), GRPC_SLICE_LENGTH(tail)));
}

static void test_slice_split_head_works(size_t length) {
  grpc_slice slice;
  grpc_slice head, tail;
  size_t i;

  LOG_TEST_NAME("test_slice_split_head_works");
  gpr_log(GPR_INFO, "length=%" PRIuPTR, length);

  /* Create a slice in which each byte is equal to the distance from it to the
     beginning of the slice. */
  slice = grpc_slice_malloc(length);
  for (i = 0; i < length; i++) {
    GRPC_SLICE_START_PTR(slice)[i] = (uint8_t)i;
  }

  /* Ensure that for all subsets length is correct and that we start on the
     correct byte. Additionally check that no copies were made. */
  for (i = 0; i < length; i++) {
    tail = grpc_slice_ref(slice);
    head = grpc_slice_split_head(&tail, i);
    check_head_tail(slice, head, tail);
    grpc_slice_unref(tail);
    grpc_slice_unref(head);
  }

  grpc_slice_unref(slice);
}

static void test_slice_split_tail_works(size_t length) {
  grpc_slice slice;
  grpc_slice head, tail;
  size_t i;

  LOG_TEST_NAME("test_slice_split_tail_works");
  gpr_log(GPR_INFO, "length=%" PRIuPTR, length);

  /* Create a slice in which each byte is equal to the distance from it to the
     beginning of the slice. */
  slice = grpc_slice_malloc(length);
  for (i = 0; i < length; i++) {
    GRPC_SLICE_START_PTR(slice)[i] = (uint8_t)i;
  }

  /* Ensure that for all subsets length is correct and that we start on the
     correct byte. Additionally check that no copies were made. */
  for (i = 0; i < length; i++) {
    head = grpc_slice_ref(slice);
    tail = grpc_slice_split_tail(&head, i);
    check_head_tail(slice, head, tail);
    grpc_slice_unref(tail);
    grpc_slice_unref(head);
  }

  grpc_slice_unref(slice);
}

static void test_slice_from_copied_string_works(void) {
  static const char *text = "HELLO WORLD!";
  grpc_slice slice;

  LOG_TEST_NAME("test_slice_from_copied_string_works");

  slice = grpc_slice_from_copied_string(text);
  GPR_ASSERT(strlen(text) == GRPC_SLICE_LENGTH(slice));
  GPR_ASSERT(
      0 == memcmp(text, GRPC_SLICE_START_PTR(slice), GRPC_SLICE_LENGTH(slice)));
  grpc_slice_unref(slice);
}

int main(int argc, char **argv) {
  unsigned length;
  grpc_test_init(argc, argv);
  test_slice_malloc_returns_something_sensible();
  test_slice_new_returns_something_sensible();
  test_slice_new_with_user_data();
  test_slice_new_with_len_returns_something_sensible();
  for (length = 0; length < 128; length++) {
    test_slice_sub_works(length);
    test_slice_split_head_works(length);
    test_slice_split_tail_works(length);
  }
  test_slice_from_copied_string_works();
  return 0;
}
