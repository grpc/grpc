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

#include "test/core/bad_client/bad_client.h"
#include "src/core/surface/server.h"

static void verifier(grpc_server *server, grpc_completion_queue *cq) {
  while (grpc_server_has_open_connections(server)) {
    GPR_ASSERT(grpc_completion_queue_next(
                   cq, GRPC_TIMEOUT_MILLIS_TO_DEADLINE(20)).type ==
               GRPC_QUEUE_TIMEOUT);
  }
}

int main(int argc, char **argv) {
  grpc_test_init(argc, argv);

  grpc_run_bad_client_test("conpfx_1", "X", 1, verifier);
  grpc_run_bad_client_test("conpfx_2", "PX", 2, verifier);
  grpc_run_bad_client_test("conpfx_3", "PRX", 3, verifier);
  grpc_run_bad_client_test("conpfx_4", "PRIX", 4, verifier);
  grpc_run_bad_client_test("conpfx_5", "PRI X", 5, verifier);
  grpc_run_bad_client_test("conpfx_6", "PRI *X", 6, verifier);
  grpc_run_bad_client_test("conpfx_7", "PRI * X", 7, verifier);
  grpc_run_bad_client_test("conpfx_8", "PRI * HX", 8, verifier);
  grpc_run_bad_client_test("conpfx_9", "PRI * HTX", 9, verifier);
  grpc_run_bad_client_test("conpfx_10", "PRI * HTTX", 10, verifier);
  grpc_run_bad_client_test("conpfx_11", "PRI * HTTPX", 11, verifier);
  grpc_run_bad_client_test("conpfx_12", "PRI * HTTP/X", 12, verifier);
  grpc_run_bad_client_test("conpfx_13", "PRI * HTTP/2X", 13, verifier);
  grpc_run_bad_client_test("conpfx_14", "PRI * HTTP/2.X", 14, verifier);
  grpc_run_bad_client_test("conpfx_15", "PRI * HTTP/2.0X", 15, verifier);
  grpc_run_bad_client_test("conpfx_16", "PRI * HTTP/2.0\rX", 16, verifier);
  grpc_run_bad_client_test("conpfx_17", "PRI * HTTP/2.0\r\nX", 17, verifier);
  grpc_run_bad_client_test("conpfx_18", "PRI * HTTP/2.0\r\n\rX", 18, verifier);
  grpc_run_bad_client_test("conpfx_19", "PRI * HTTP/2.0\r\n\r\nX", 19,
                           verifier);
  grpc_run_bad_client_test("conpfx_20", "PRI * HTTP/2.0\r\n\r\nSX", 20,
                           verifier);
  grpc_run_bad_client_test("conpfx_21", "PRI * HTTP/2.0\r\n\r\nSMX", 21,
                           verifier);
  grpc_run_bad_client_test("conpfx_22", "PRI * HTTP/2.0\r\n\r\nSM\rX", 22,
                           verifier);
  grpc_run_bad_client_test("conpfx_23", "PRI * HTTP/2.0\r\n\r\nSM\r\nX", 23,
                           verifier);
  grpc_run_bad_client_test("conpfx_24", "PRI * HTTP/2.0\r\n\r\nSM\r\n\rX", 24,
                           verifier);
  return 0;
}
