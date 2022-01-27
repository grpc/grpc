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

#include <grpc/grpc.h>
#include <grpc/slice_buffer.h>
#include <grpc/support/log.h>

#include "src/core/lib/slice/slice_internal.h"
#include "test/core/util/test_config.h"

void test_slice_buffer_add() {
  grpc_slice_buffer buf;
  grpc_slice aaa = grpc_slice_from_copied_string("aaa");
  grpc_slice bb = grpc_slice_from_copied_string("bb");
  size_t i;

  grpc_slice_buffer_init(&buf);
  for (i = 0; i < 10; i++) {
    grpc_slice_ref(aaa);
    grpc_slice_ref(bb);
    grpc_slice_buffer_add(&buf, aaa);
    grpc_slice_buffer_add(&buf, bb);
  }
  GPR_ASSERT(buf.count > 0);
  GPR_ASSERT(buf.length == 50);
  grpc_slice_buffer_reset_and_unref(&buf);
  GPR_ASSERT(buf.count == 0);
  GPR_ASSERT(buf.length == 0);
  for (i = 0; i < 10; i++) {
    grpc_slice_ref(aaa);
    grpc_slice_ref(bb);
    grpc_slice_buffer_add(&buf, aaa);
    grpc_slice_buffer_add(&buf, bb);
  }
  GPR_ASSERT(buf.count > 0);
  GPR_ASSERT(buf.length == 50);
  for (i = 0; i < 10; i++) {
    grpc_slice_buffer_pop(&buf);
    grpc_slice_unref(aaa);
    grpc_slice_unref(bb);
  }
  GPR_ASSERT(buf.count == 0);
  GPR_ASSERT(buf.length == 0);
  grpc_slice_buffer_destroy(&buf);
}

void test_slice_buffer_move_first() {
  grpc_slice slices[3];
  grpc_slice_buffer src;
  grpc_slice_buffer dst;
  int idx = 0;
  size_t src_len = 0;
  size_t dst_len = 0;

  slices[0] = grpc_slice_from_copied_string("aaa");
  slices[1] = grpc_slice_from_copied_string("bbbb");
  slices[2] = grpc_slice_from_copied_string("ccc");

  grpc_slice_buffer_init(&src);
  grpc_slice_buffer_init(&dst);
  for (idx = 0; idx < 3; idx++) {
    grpc_slice_ref(slices[idx]);
    /* For this test, it is important that we add each slice at a new
       slice index */
    grpc_slice_buffer_add_indexed(&src, slices[idx]);
    grpc_slice_buffer_add_indexed(&dst, slices[idx]);
  }

  /* Case 1: Move more than the first slice's length from src to dst */
  src_len = src.length;
  dst_len = dst.length;
  grpc_slice_buffer_move_first(&src, 4, &dst);
  src_len -= 4;
  dst_len += 4;
  GPR_ASSERT(src.length == src_len);
  GPR_ASSERT(dst.length == dst_len);

  /* src now has two slices ["bbb"] and  ["ccc"] */
  /* Case 2: Move the first slice from src to dst */
  grpc_slice_buffer_move_first(&src, 3, &dst);
  src_len -= 3;
  dst_len += 3;
  GPR_ASSERT(src.length == src_len);
  GPR_ASSERT(dst.length == dst_len);

  /* src now has one slice ["ccc"] */
  /* Case 3: Move less than the first slice's length from src to dst*/
  grpc_slice_buffer_move_first(&src, 2, &dst);
  src_len -= 2;
  dst_len += 2;
  GPR_ASSERT(src.length == src_len);
  GPR_ASSERT(dst.length == dst_len);
}

void test_slice_buffer_first() {
  grpc_slice slices[3];
  slices[0] = grpc_slice_from_copied_string("aaa");
  slices[1] = grpc_slice_from_copied_string("bbbb");
  slices[2] = grpc_slice_from_copied_string("ccccc");

  grpc_slice_buffer buf;
  grpc_slice_buffer_init(&buf);
  for (int idx = 0; idx < 3; ++idx) {
    grpc_slice_ref(slices[idx]);
    grpc_slice_buffer_add_indexed(&buf, slices[idx]);
  }

  grpc_slice* first = grpc_slice_buffer_peek_first(&buf);
  GPR_ASSERT(GPR_SLICE_LENGTH(*first) == GPR_SLICE_LENGTH(slices[0]));
  GPR_ASSERT(buf.count == 3);
  GPR_ASSERT(buf.length == 12);

  grpc_slice_buffer_sub_first(&buf, 1, 2);
  first = grpc_slice_buffer_peek_first(&buf);
  GPR_ASSERT(GPR_SLICE_LENGTH(*first) == 1);
  GPR_ASSERT(buf.count == 3);
  GPR_ASSERT(buf.length == 10);

  grpc_slice_buffer_remove_first(&buf);
  first = grpc_slice_buffer_peek_first(&buf);
  GPR_ASSERT(GPR_SLICE_LENGTH(*first) == GPR_SLICE_LENGTH(slices[1]));
  GPR_ASSERT(buf.count == 2);
  GPR_ASSERT(buf.length == 9);

  grpc_slice_buffer_remove_first(&buf);
  first = grpc_slice_buffer_peek_first(&buf);
  GPR_ASSERT(GPR_SLICE_LENGTH(*first) == GPR_SLICE_LENGTH(slices[2]));
  GPR_ASSERT(buf.count == 1);
  GPR_ASSERT(buf.length == 5);

  grpc_slice_buffer_remove_first(&buf);
  GPR_ASSERT(buf.count == 0);
  GPR_ASSERT(buf.length == 0);
}

int main(int argc, char** argv) {
  grpc::testing::TestEnvironment env(argc, argv);
  grpc_init();

  test_slice_buffer_add();
  test_slice_buffer_move_first();
  test_slice_buffer_first();

  grpc_shutdown();
  return 0;
}
