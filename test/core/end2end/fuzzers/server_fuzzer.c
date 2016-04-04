/*
 *
 * Copyright 2016, Google Inc.
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

#include <grpc/grpc.h>

#include "src/core/ext/transport/chttp2/transport/chttp2_transport.h"
#include "src/core/lib/surface/server.h"
#include "test/core/util/mock_endpoint.h"

static void discard_write(gpr_slice slice) {}

static void *tag(int n) { return (void *)(uintptr_t)n; }
static int detag(void *p) { return (int)(uintptr_t)p; }

static void dont_log(gpr_log_func_args *args) {}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
  gpr_set_log_function(dont_log);
  grpc_init();
  grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;

  grpc_endpoint *mock_endpoint = grpc_mock_endpoint_create(discard_write);
  grpc_mock_endpoint_put_read(
      &exec_ctx, mock_endpoint,
      gpr_slice_from_copied_buffer((const char *)data, size));

  grpc_server *server = grpc_server_create(NULL, NULL);
  grpc_completion_queue *cq = grpc_completion_queue_create(NULL);
  grpc_server_register_completion_queue(server, cq, NULL);
  // TODO(ctiller): add registered methods (one for POST, one for PUT)
  // void *registered_method =
  //    grpc_server_register_method(server, "/reg", NULL, 0);
  grpc_server_start(server);
  grpc_transport *transport =
      grpc_create_chttp2_transport(&exec_ctx, NULL, mock_endpoint, 0);
  grpc_server_setup_transport(&exec_ctx, server, transport, NULL);
  grpc_chttp2_transport_start_reading(&exec_ctx, transport, NULL, 0);

  grpc_call *call1;
  grpc_call_details call_details1;
  grpc_metadata_array request_metadata1;
  grpc_server_request_call(server, &call1, &call_details1, &request_metadata1,
                           cq, cq, tag(1));

  while (1) {
    grpc_exec_ctx_flush(&exec_ctx);
    grpc_event ev =
        grpc_completion_queue_next(cq, gpr_inf_past(GPR_CLOCK_REALTIME), NULL);
    switch (ev.type) {
      case GRPC_QUEUE_TIMEOUT:
        goto done;
      case GRPC_QUEUE_SHUTDOWN:
        break;
      case GRPC_OP_COMPLETE:
        switch (detag(ev.tag)) {
          case 1:
            abort();
        }
    }
  }
done:
  grpc_shutdown();
  return 0;
}
