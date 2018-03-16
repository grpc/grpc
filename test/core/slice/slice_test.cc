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

#include <grpc/support/port_platform.h>

#include <grpc/slice.h>

#include <inttypes.h>
#include <string.h>

#include <grpc/grpc.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>

#include "src/core/lib/slice/slice_internal.h"
#include "src/core/lib/transport/static_metadata.h"
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
    if (length > GRPC_SLICE_INLINED_SIZE) {
      GPR_ASSERT(slice.data.refcounted.bytes);
    }
    /* Returned slice length must be what was requested. */
    GPR_ASSERT(GRPC_SLICE_LENGTH(slice) == length);
    /* If the slice has a refcount, it must be destroyable. */
    if (slice.refcount) {
      GPR_ASSERT(slice.refcount->vtable != nullptr);
      GPR_ASSERT(slice.refcount->vtable->ref != nullptr);
      GPR_ASSERT(slice.refcount->vtable->unref != nullptr);
      GPR_ASSERT(slice.refcount->vtable->hash != nullptr);
    }
    /* We must be able to write to every byte of the data */
    for (i = 0; i < length; i++) {
      GRPC_SLICE_START_PTR(slice)[i] = static_cast<uint8_t>(i);
    }
    /* And finally we must succeed in destroying the slice */
    grpc_slice_unref(slice);
  }
}

static void do_nothing(void* ignored) {}

static void test_slice_new_returns_something_sensible(void) {
  uint8_t x;

  grpc_slice slice = grpc_slice_new(&x, 1, do_nothing);
  GPR_ASSERT(slice.refcount);
  GPR_ASSERT(slice.data.refcounted.bytes == &x);
  GPR_ASSERT(slice.data.refcounted.length == 1);
  grpc_slice_unref(slice);
}

/* destroy function that sets a mark to indicate it was called. */
static void set_mark(void* p) { *(static_cast<int*>(p)) = 1; }

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

static void do_nothing_with_len_1(void* ignored, size_t len) {
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
    GRPC_SLICE_START_PTR(slice)[i] = static_cast<uint8_t>(i);
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
    GRPC_SLICE_START_PTR(slice)[i] = static_cast<uint8_t>(i);
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
    GRPC_SLICE_START_PTR(slice)[i] = static_cast<uint8_t>(i);
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
  static const char* text = "HELLO WORLD!";
  grpc_slice slice;

  LOG_TEST_NAME("test_slice_from_copied_string_works");

  slice = grpc_slice_from_copied_string(text);
  GPR_ASSERT(strlen(text) == GRPC_SLICE_LENGTH(slice));
  GPR_ASSERT(
      0 == memcmp(text, GRPC_SLICE_START_PTR(slice), GRPC_SLICE_LENGTH(slice)));
  grpc_slice_unref(slice);
}

static void test_slice_interning(void) {
  LOG_TEST_NAME("test_slice_interning");

  grpc_init();
  grpc_slice src1 = grpc_slice_from_copied_string("hello123456789123456789");
  grpc_slice src2 = grpc_slice_from_copied_string("hello123456789123456789");
  GPR_ASSERT(GRPC_SLICE_START_PTR(src1) != GRPC_SLICE_START_PTR(src2));
  grpc_slice interned1 = grpc_slice_intern(src1);
  grpc_slice interned2 = grpc_slice_intern(src2);
  GPR_ASSERT(GRPC_SLICE_START_PTR(interned1) ==
             GRPC_SLICE_START_PTR(interned2));
  GPR_ASSERT(GRPC_SLICE_START_PTR(interned1) != GRPC_SLICE_START_PTR(src1));
  GPR_ASSERT(GRPC_SLICE_START_PTR(interned2) != GRPC_SLICE_START_PTR(src2));
  grpc_slice_unref(src1);
  grpc_slice_unref(src2);
  grpc_slice_unref(interned1);
  grpc_slice_unref(interned2);
  grpc_shutdown();
}

static void test_static_slice_interning(void) {
  LOG_TEST_NAME("test_static_slice_interning");

  // grpc_init/grpc_shutdown deliberately omitted: they should not be necessary
  // to intern a static slice

  for (size_t i = 0; i < GRPC_STATIC_MDSTR_COUNT; i++) {
    GPR_ASSERT(grpc_slice_is_equivalent(
        grpc_static_slice_table[i],
        grpc_slice_intern(grpc_static_slice_table[i])));
  }
}

static void test_static_slice_copy_interning(void) {
  LOG_TEST_NAME("test_static_slice_copy_interning");

  grpc_init();

  for (size_t i = 0; i < GRPC_STATIC_MDSTR_COUNT; i++) {
    grpc_slice copy = grpc_slice_dup(grpc_static_slice_table[i]);
    GPR_ASSERT(grpc_static_slice_table[i].refcount != copy.refcount);
    GPR_ASSERT(grpc_static_slice_table[i].refcount ==
               grpc_slice_intern(copy).refcount);
    grpc_slice_unref(copy);
  }

  grpc_shutdown();
}

int main(int argc, char** argv) {
  unsigned length;
  grpc_test_init(argc, argv);
  grpc_init();
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
  test_slice_interning();
  test_static_slice_interning();
  test_static_slice_copy_interning();
  grpc_shutdown();
  return 0;
}
