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

#include <grpc/support/log.h>
#include <grpc/support/slice_buffer.h>
#include "test/core/util/test_config.h"

void test_slice_buffer_add() {
  gpr_slice_buffer buf;
  gpr_slice aaa = gpr_slice_from_copied_string("aaa");
  gpr_slice bb = gpr_slice_from_copied_string("bb");
  size_t i;

  gpr_slice_buffer_init(&buf);
  for (i = 0; i < 10; i++) {
    gpr_slice_ref(aaa);
    gpr_slice_ref(bb);
    gpr_slice_buffer_add(&buf, aaa);
    gpr_slice_buffer_add(&buf, bb);
  }
  GPR_ASSERT(buf.count > 0);
  GPR_ASSERT(buf.length == 50);
  gpr_slice_buffer_reset_and_unref(&buf);
  GPR_ASSERT(buf.count == 0);
  GPR_ASSERT(buf.length == 0);
  for (i = 0; i < 10; i++) {
    gpr_slice_ref(aaa);
    gpr_slice_ref(bb);
    gpr_slice_buffer_add(&buf, aaa);
    gpr_slice_buffer_add(&buf, bb);
  }
  GPR_ASSERT(buf.count > 0);
  GPR_ASSERT(buf.length == 50);
  for (i = 0; i < 10; i++) {
    gpr_slice_buffer_pop(&buf);
    gpr_slice_unref(aaa);
    gpr_slice_unref(bb);
  }
  GPR_ASSERT(buf.count == 0);
  GPR_ASSERT(buf.length == 0);
  gpr_slice_buffer_destroy(&buf);
}

void test_slice_buffer_move_first() {
  gpr_slice slices[3];
  gpr_slice_buffer src;
  gpr_slice_buffer dst;
  int idx = 0;
  size_t src_len = 0;
  size_t dst_len = 0;

  slices[0] = gpr_slice_from_copied_string("aaa");
  slices[1] = gpr_slice_from_copied_string("bbbb");
  slices[2] = gpr_slice_from_copied_string("ccc");

  gpr_slice_buffer_init(&src);
  gpr_slice_buffer_init(&dst);
  for (idx = 0; idx < 3; idx++) {
    gpr_slice_ref(slices[idx]);
    /* For this test, it is important that we add each slice at a new
       slice index */
    gpr_slice_buffer_add_indexed(&src, slices[idx]);
    gpr_slice_buffer_add_indexed(&dst, slices[idx]);
  }

  /* Case 1: Move more than the first slice's length from src to dst */
  src_len = src.length;
  dst_len = dst.length;
  gpr_slice_buffer_move_first(&src, 4, &dst);
  src_len -= 4;
  dst_len += 4;
  GPR_ASSERT(src.length == src_len);
  GPR_ASSERT(dst.length == dst_len);

  /* src now has two slices ["bbb"] and  ["ccc"] */
  /* Case 2: Move the first slice from src to dst */
  gpr_slice_buffer_move_first(&src, 3, &dst);
  src_len -= 3;
  dst_len += 3;
  GPR_ASSERT(src.length == src_len);
  GPR_ASSERT(dst.length == dst_len);

  /* src now has one slice ["ccc"] */
  /* Case 3: Move less than the first slice's length from src to dst*/
  gpr_slice_buffer_move_first(&src, 2, &dst);
  src_len -= 2;
  dst_len += 2;
  GPR_ASSERT(src.length == src.length);
  GPR_ASSERT(dst.length == dst.length);
}

int main(int argc, char **argv) {
  grpc_test_init(argc, argv);

  test_slice_buffer_add();
  test_slice_buffer_move_first();

  return 0;
}
