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

#include <grpc/grpc.h>
#include "test/core/util/test_config.h"

int main(int argc, char **argv) {
  grpc_completion_queue *cq1;
  grpc_completion_queue *cq2;
  grpc_server *server;

  grpc_test_init(argc, argv);
  grpc_init();
  cq1 = grpc_completion_queue_create(NULL);
  cq2 = grpc_completion_queue_create(NULL);
  server = grpc_server_create(NULL, NULL);
  grpc_server_register_completion_queue(server, cq1, NULL);
  grpc_server_add_insecure_http2_port(server, "[::]:0");
  grpc_server_register_completion_queue(server, cq2, NULL);
  grpc_server_start(server);
  grpc_server_shutdown_and_notify(server, cq2, NULL);
  grpc_completion_queue_next(cq2, gpr_inf_future(GPR_CLOCK_REALTIME),
                             NULL); /* cue queue hang */
  grpc_completion_queue_shutdown(cq1);
  grpc_completion_queue_shutdown(cq2);
  grpc_completion_queue_next(cq1, gpr_inf_future(GPR_CLOCK_REALTIME), NULL);
  grpc_completion_queue_next(cq2, gpr_inf_future(GPR_CLOCK_REALTIME), NULL);
  grpc_server_destroy(server);
  grpc_completion_queue_destroy(cq1);
  grpc_completion_queue_destroy(cq2);
  grpc_shutdown();
  return 0;
}
