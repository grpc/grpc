/*
 *
 * Copyright 2017, Google Inc.
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

#include "src/core/lib/transport/transport.h"

#include "test/core/util/test_config.h"

#include <grpc/support/log.h>

static void do_nothing(grpc_exec_ctx *exec_ctx, void *arg, grpc_error *error) {}

int main(int argc, char **argv) {
  grpc_test_init(argc, argv);

  uint8_t buffer[] = "abc123";
  grpc_stream_refcount r;
  GRPC_STREAM_REF_INIT(&r, 1, do_nothing, NULL, "test");
  GPR_ASSERT(r.refs.count == 1);
  grpc_slice slice =
      grpc_slice_from_stream_owned_buffer(&r, buffer, sizeof(buffer));
  GPR_ASSERT(GRPC_SLICE_START_PTR(slice) == buffer);
  GPR_ASSERT(GRPC_SLICE_LENGTH(slice) == sizeof(buffer));
  GPR_ASSERT(r.refs.count == 2);
  grpc_slice_unref(slice);
  GPR_ASSERT(r.refs.count == 1);

  return 0;
}
