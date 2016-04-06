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

/*******************************************************************************
 * NOTE: If this test fails to compile, then the api changes are likely to cause
 *       merge failures downstream. Please pay special attention to reviewing
 *       these changes, and solicit help as appropriate when merging downstream.
 *
 * This test is NOT expected to be run directly.
 ******************************************************************************/

#include "src/core/lib/transport/transport.h"
#include "src/core/lib/transport/transport_impl.h"

static void test_code(void) {
  /* transport_impl.h */
  grpc_transport transport;
  grpc_transport_vtable vtable = {12345,
                                  grpc_transport_init_stream,
                                  grpc_transport_set_pollset,
                                  grpc_transport_perform_stream_op,
                                  grpc_transport_perform_op,
                                  grpc_transport_destroy_stream,
                                  grpc_transport_destroy,
                                  grpc_transport_get_peer};
  transport.vtable = &vtable;

  /* transport.h */
  GRPC_STREAM_REF_INIT(NULL, 0, NULL, NULL, "xyz");
  GPR_ASSERT(0 == grpc_transport_stream_size(NULL));
  GPR_ASSERT(grpc_transport_init_stream(&transport, NULL, NULL, NULL, NULL));
  grpc_transport_set_pollset(&transport, NULL, NULL, NULL);
  grpc_transport_destroy_stream(&transport, NULL, NULL);
  grpc_transport_stream_op_finish_with_failure(NULL, NULL);
  grpc_transport_stream_op_add_cancellation(NULL, GRPC_STATUS_UNAVAILABLE);
  grpc_transport_stream_op_add_close(NULL, GRPC_STATUS_UNAVAILABLE,
                                     grpc_transport_op_string(NULL));
  grpc_transport_perform_stream_op(&transport, NULL, NULL, NULL);
  grpc_transport_perform_op(&transport, NULL, NULL);
  grpc_transport_ping(&transport, NULL);
  grpc_transport_goaway(&transport, GRPC_STATUS_UNAVAILABLE,
                        gpr_slice_malloc(0));
  grpc_transport_close(&transport);
  grpc_transport_destroy(&transport, NULL);
  GPR_ASSERT("xyz" == grpc_transport_get_peer(&transport, NULL));
}

int main(void) {
  if (false) test_code();
  return 0;
}
