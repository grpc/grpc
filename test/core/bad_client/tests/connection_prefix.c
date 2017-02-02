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

#include "src/core/lib/surface/server.h"
#include "test/core/bad_client/bad_client.h"

static void verifier(grpc_server *server, grpc_completion_queue *cq,
                     void *registered_method) {
  while (grpc_server_has_open_connections(server)) {
    GPR_ASSERT(grpc_completion_queue_next(
                   cq, grpc_timeout_milliseconds_to_deadline(20), NULL)
                   .type == GRPC_QUEUE_TIMEOUT);
  }
}

int main(int argc, char **argv) {
  grpc_test_init(argc, argv);

  GRPC_RUN_BAD_CLIENT_TEST(verifier, NULL, "X", 0);
  GRPC_RUN_BAD_CLIENT_TEST(verifier, NULL, "PX", 0);
  GRPC_RUN_BAD_CLIENT_TEST(verifier, NULL, "PRX", 0);
  GRPC_RUN_BAD_CLIENT_TEST(verifier, NULL, "PRIX", 0);
  GRPC_RUN_BAD_CLIENT_TEST(verifier, NULL, "PRI X", 0);
  GRPC_RUN_BAD_CLIENT_TEST(verifier, NULL, "PRI *X", 0);
  GRPC_RUN_BAD_CLIENT_TEST(verifier, NULL, "PRI * X", 0);
  GRPC_RUN_BAD_CLIENT_TEST(verifier, NULL, "PRI * HX", 0);
  GRPC_RUN_BAD_CLIENT_TEST(verifier, NULL, "PRI * HTX", 0);
  GRPC_RUN_BAD_CLIENT_TEST(verifier, NULL, "PRI * HTTX", 0);
  GRPC_RUN_BAD_CLIENT_TEST(verifier, NULL, "PRI * HTTPX", 0);
  GRPC_RUN_BAD_CLIENT_TEST(verifier, NULL, "PRI * HTTP/X", 0);
  GRPC_RUN_BAD_CLIENT_TEST(verifier, NULL, "PRI * HTTP/2X", 0);
  GRPC_RUN_BAD_CLIENT_TEST(verifier, NULL, "PRI * HTTP/2.X", 0);
  GRPC_RUN_BAD_CLIENT_TEST(verifier, NULL, "PRI * HTTP/2.0X", 0);
  GRPC_RUN_BAD_CLIENT_TEST(verifier, NULL, "PRI * HTTP/2.0\rX", 0);
  GRPC_RUN_BAD_CLIENT_TEST(verifier, NULL, "PRI * HTTP/2.0\r\nX", 0);
  GRPC_RUN_BAD_CLIENT_TEST(verifier, NULL, "PRI * HTTP/2.0\r\n\rX", 0);
  GRPC_RUN_BAD_CLIENT_TEST(verifier, NULL, "PRI * HTTP/2.0\r\n\r\nX", 0);
  GRPC_RUN_BAD_CLIENT_TEST(verifier, NULL, "PRI * HTTP/2.0\r\n\r\nSX", 0);
  GRPC_RUN_BAD_CLIENT_TEST(verifier, NULL, "PRI * HTTP/2.0\r\n\r\nSMX", 0);
  GRPC_RUN_BAD_CLIENT_TEST(verifier, NULL, "PRI * HTTP/2.0\r\n\r\nSM\rX", 0);
  GRPC_RUN_BAD_CLIENT_TEST(verifier, NULL, "PRI * HTTP/2.0\r\n\r\nSM\r\nX", 0);
  GRPC_RUN_BAD_CLIENT_TEST(verifier, NULL, "PRI * HTTP/2.0\r\n\r\nSM\r\n\rX",
                           0);
  return 0;
}
