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

#define PFX_STR "PRI * HTTP/2.0\r\n\r\nSM\r\n\r\n"
#define ONE_SETTING_HDR "\x00\x00\x06\x04\x00\x00\x00\x00\x00"

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

  /* various partial prefixes */
  GRPC_RUN_BAD_CLIENT_TEST(verifier, NULL, PFX_STR "\x00",
                           GRPC_BAD_CLIENT_DISCONNECT);
  GRPC_RUN_BAD_CLIENT_TEST(verifier, NULL, PFX_STR "\x00\x00",
                           GRPC_BAD_CLIENT_DISCONNECT);
  GRPC_RUN_BAD_CLIENT_TEST(verifier, NULL, PFX_STR "\x00\x00\x00",
                           GRPC_BAD_CLIENT_DISCONNECT);
  GRPC_RUN_BAD_CLIENT_TEST(verifier, NULL, PFX_STR "\x06",
                           GRPC_BAD_CLIENT_DISCONNECT);
  GRPC_RUN_BAD_CLIENT_TEST(verifier, NULL, PFX_STR "\x00\x06",
                           GRPC_BAD_CLIENT_DISCONNECT);
  GRPC_RUN_BAD_CLIENT_TEST(verifier, NULL, PFX_STR "\x00\x00\x06",
                           GRPC_BAD_CLIENT_DISCONNECT);
  GRPC_RUN_BAD_CLIENT_TEST(verifier, NULL, PFX_STR "\x00\x00\x00\x04",
                           GRPC_BAD_CLIENT_DISCONNECT);
  GRPC_RUN_BAD_CLIENT_TEST(verifier, NULL, PFX_STR "\x00\x00\x00\x04\x00",
                           GRPC_BAD_CLIENT_DISCONNECT);
  GRPC_RUN_BAD_CLIENT_TEST(verifier, NULL, PFX_STR "\x00\x00\x00\x04\x01",
                           GRPC_BAD_CLIENT_DISCONNECT);
  GRPC_RUN_BAD_CLIENT_TEST(verifier, NULL, PFX_STR "\x00\x00\x00\x04\xff",
                           GRPC_BAD_CLIENT_DISCONNECT);
  GRPC_RUN_BAD_CLIENT_TEST(verifier, NULL, PFX_STR "\x00\x00\x00\x04\x00\x00",
                           GRPC_BAD_CLIENT_DISCONNECT);
  GRPC_RUN_BAD_CLIENT_TEST(verifier, NULL,
                           PFX_STR "\x00\x00\x00\x04\x00\x00\x00",
                           GRPC_BAD_CLIENT_DISCONNECT);
  GRPC_RUN_BAD_CLIENT_TEST(verifier, NULL,
                           PFX_STR "\x00\x00\x00\x04\x00\x00\x00\x00",
                           GRPC_BAD_CLIENT_DISCONNECT);
  /* must not send frames with stream id != 0 */
  GRPC_RUN_BAD_CLIENT_TEST(verifier, NULL,
                           PFX_STR "\x00\x00\x00\x04\x00\x00\x00\x00\x01", 0);
  GRPC_RUN_BAD_CLIENT_TEST(verifier, NULL,
                           PFX_STR "\x00\x00\x00\x04\x00\x40\x00\x00\x00", 0);
  /* settings frame must be a multiple of six bytes long */
  GRPC_RUN_BAD_CLIENT_TEST(verifier, NULL,
                           PFX_STR "\x00\x00\x01\x04\x00\x00\x00\x00\x00", 0);
  GRPC_RUN_BAD_CLIENT_TEST(verifier, NULL,
                           PFX_STR "\x00\x00\x02\x04\x00\x00\x00\x00\x00", 0);
  GRPC_RUN_BAD_CLIENT_TEST(verifier, NULL,
                           PFX_STR "\x00\x00\x03\x04\x00\x00\x00\x00\x00", 0);
  GRPC_RUN_BAD_CLIENT_TEST(verifier, NULL,
                           PFX_STR "\x00\x00\x04\x04\x00\x00\x00\x00\x00", 0);
  GRPC_RUN_BAD_CLIENT_TEST(verifier, NULL,
                           PFX_STR "\x00\x00\x05\x04\x00\x00\x00\x00\x00", 0);
  /* some settings values are illegal */
  /* max frame size = 0 */
  GRPC_RUN_BAD_CLIENT_TEST(verifier, NULL,
                           PFX_STR ONE_SETTING_HDR "\x00\x05\x00\x00\x00\x00",
                           GRPC_BAD_CLIENT_DISCONNECT);
  GRPC_RUN_BAD_CLIENT_TEST(verifier, NULL,
                           PFX_STR ONE_SETTING_HDR "\x00\x06\xff\xff\xff\xff",
                           GRPC_BAD_CLIENT_DISCONNECT);
  /* update intiial window size */
  GRPC_RUN_BAD_CLIENT_TEST(verifier, NULL,
                           PFX_STR ONE_SETTING_HDR "\x00\x04\x00\x01\x00\x00",
                           GRPC_BAD_CLIENT_DISCONNECT);
  /* ack with data */
  GRPC_RUN_BAD_CLIENT_TEST(verifier, NULL, PFX_STR
                           "\x00\x00\x00\x04\x00\x00\x00\x00\x00"
                           "\x00\x00\x01\x04\x01\x00\x00\x00\x00",
                           0);
  /* settings frame with invalid flags */
  GRPC_RUN_BAD_CLIENT_TEST(verifier, NULL,
                           PFX_STR "\x00\x00\x00\x04\x10\x00\x00\x00\x00", 0);
  /* unknown settings should be ignored */
  GRPC_RUN_BAD_CLIENT_TEST(verifier, NULL,
                           PFX_STR ONE_SETTING_HDR "\x00\x99\x00\x00\x00\x00",
                           GRPC_BAD_CLIENT_DISCONNECT);

  return 0;
}
