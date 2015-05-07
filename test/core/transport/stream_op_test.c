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

#include "src/core/transport/stream_op.h"

#include <string.h>

#include <grpc/support/log.h>
#include "test/core/util/test_config.h"

static void assert_slices_equal(gpr_slice a, gpr_slice b) {
  GPR_ASSERT(a.refcount == b.refcount);
  if (a.refcount) {
    GPR_ASSERT(a.data.refcounted.bytes == b.data.refcounted.bytes);
    GPR_ASSERT(a.data.refcounted.length == b.data.refcounted.length);
  } else {
    GPR_ASSERT(a.data.inlined.length == b.data.inlined.length);
    GPR_ASSERT(0 == memcmp(a.data.inlined.bytes, b.data.inlined.bytes,
                           a.data.inlined.length));
  }
}

int main(int argc, char **argv) {
  /* some basic test data */
  gpr_slice test_slice_1 = gpr_slice_malloc(1);
  gpr_slice test_slice_2 = gpr_slice_malloc(2);
  gpr_slice test_slice_3 = gpr_slice_malloc(3);
  gpr_slice test_slice_4 = gpr_slice_malloc(4);
  unsigned i;

  grpc_stream_op_buffer buf;
  grpc_stream_op_buffer buf2;

  grpc_test_init(argc, argv);
  /* initialize one of our buffers */
  grpc_sopb_init(&buf);
  /* it should start out empty */
  GPR_ASSERT(buf.nops == 0);

  /* add some data to the buffer */
  grpc_sopb_add_begin_message(&buf, 1, 2);
  grpc_sopb_add_slice(&buf, test_slice_1);
  grpc_sopb_add_slice(&buf, test_slice_2);
  grpc_sopb_add_slice(&buf, test_slice_3);
  grpc_sopb_add_slice(&buf, test_slice_4);
  grpc_sopb_add_no_op(&buf);

  /* verify that the data went in ok */
  GPR_ASSERT(buf.nops == 6);
  GPR_ASSERT(buf.ops[0].type == GRPC_OP_BEGIN_MESSAGE);
  GPR_ASSERT(buf.ops[0].data.begin_message.length == 1);
  GPR_ASSERT(buf.ops[0].data.begin_message.flags == 2);
  GPR_ASSERT(buf.ops[1].type == GRPC_OP_SLICE);
  assert_slices_equal(buf.ops[1].data.slice, test_slice_1);
  GPR_ASSERT(buf.ops[2].type == GRPC_OP_SLICE);
  assert_slices_equal(buf.ops[2].data.slice, test_slice_2);
  GPR_ASSERT(buf.ops[3].type == GRPC_OP_SLICE);
  assert_slices_equal(buf.ops[3].data.slice, test_slice_3);
  GPR_ASSERT(buf.ops[4].type == GRPC_OP_SLICE);
  assert_slices_equal(buf.ops[4].data.slice, test_slice_4);
  GPR_ASSERT(buf.ops[5].type == GRPC_NO_OP);

  /* initialize the second buffer */
  grpc_sopb_init(&buf2);
  /* add a no-op, and then the original buffer */
  grpc_sopb_add_no_op(&buf2);
  grpc_sopb_append(&buf2, buf.ops, buf.nops);
  /* should be one element bigger than the original */
  GPR_ASSERT(buf2.nops == buf.nops + 1);
  GPR_ASSERT(buf2.ops[0].type == GRPC_NO_OP);
  /* and the tail should be the same */
  for (i = 0; i < buf.nops; i++) {
    GPR_ASSERT(buf2.ops[i + 1].type == buf.ops[i].type);
  }

  /* destroy the buffers */
  grpc_sopb_destroy(&buf);
  grpc_sopb_destroy(&buf2);

  gpr_slice_unref(test_slice_1);
  gpr_slice_unref(test_slice_2);
  gpr_slice_unref(test_slice_3);
  gpr_slice_unref(test_slice_4);

  return 0;
}
