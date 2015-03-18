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

int main(int argc, char **argv) {
  gpr_slice_buffer buf;
  gpr_slice aaa = gpr_slice_from_copied_string("aaa");
  gpr_slice bb = gpr_slice_from_copied_string("bb");
  size_t i;

  grpc_test_init(argc, argv);
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

  return 0;
}
