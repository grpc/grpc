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

#include <string.h>

#include "src/core/lib/surface/server.h"
#include "test/core/end2end/cq_verifier.h"

#define PFX_STR                      \
  "PRI * HTTP/2.0\r\n\r\nSM\r\n\r\n" \
  "\x00\x00\x00\x04\x00\x00\x00\x00\x00" /* settings frame */

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

  /* invalid content type */
  GRPC_RUN_BAD_CLIENT_TEST(
      verifier, NULL, PFX_STR
      "\x00\x00\xc2\x01\x04\x00\x00\x00\x01"
      "\x10\x05:path\x08/foo/bar"
      "\x10\x07:scheme\x04http"
      "\x10\x07:method\x04POST"
      "\x10\x0a:authority\x09localhost"
      "\x10\x0c"
      "content-type\x09text/html"
      "\x10\x14grpc-accept-encoding\x15identity,deflate,gzip"
      "\x10\x02te\x08trailers"
      "\x10\x0auser-agent\"bad-client grpc-c/0.12.0.0 (linux)",
      GRPC_BAD_CLIENT_DISCONNECT);

  /* invalid te */
  GRPC_RUN_BAD_CLIENT_TEST(
      verifier, NULL, PFX_STR
      "\x00\x00\xcb\x01\x04\x00\x00\x00\x01"
      "\x10\x05:path\x08/foo/bar"
      "\x10\x07:scheme\x04http"
      "\x10\x07:method\x04POST"
      "\x10\x0a:authority\x09localhost"
      "\x10\x0c"
      "content-type\x10"
      "application/grpc"
      "\x10\x14grpc-accept-encoding\x15identity,deflate,gzip"
      "\x10\x02te\x0a"
      "frobnicate"
      "\x10\x0auser-agent\"bad-client grpc-c/0.12.0.0 (linux)",
      GRPC_BAD_CLIENT_DISCONNECT);

  /* two path headers */
  GRPC_RUN_BAD_CLIENT_TEST(
      verifier, NULL, PFX_STR
      "\x00\x00\xd9\x01\x04\x00\x00\x00\x01"
      "\x10\x05:path\x08/foo/bar"
      "\x10\x05:path\x08/foo/bah"
      "\x10\x07:scheme\x04http"
      "\x10\x07:method\x04POST"
      "\x10\x0a:authority\x09localhost"
      "\x10\x0c"
      "content-type\x10"
      "application/grpc"
      "\x10\x14grpc-accept-encoding\x15identity,deflate,gzip"
      "\x10\x02te\x08trailers"
      "\x10\x0auser-agent\"bad-client grpc-c/0.12.0.0 (linux)",
      GRPC_BAD_CLIENT_DISCONNECT);

  /* bad accept-encoding algorithm */
  GRPC_RUN_BAD_CLIENT_TEST(
      verifier, NULL, PFX_STR
      "\x00\x00\xd2\x01\x04\x00\x00\x00\x01"
      "\x10\x05:path\x08/foo/bar"
      "\x10\x07:scheme\x04http"
      "\x10\x07:method\x04POST"
      "\x10\x0a:authority\x09localhost"
      "\x10\x0c"
      "content-type\x10"
      "application/grpc"
      "\x10\x14grpc-accept-encoding\x1enobody-knows-the-trouble-i-see"
      "\x10\x02te\x08trailers"
      "\x10\x0auser-agent\"bad-client grpc-c/0.12.0.0 (linux)",
      GRPC_BAD_CLIENT_DISCONNECT);

  /* bad grpc-encoding algorithm */
  GRPC_RUN_BAD_CLIENT_TEST(
      verifier, NULL, PFX_STR
      "\x00\x00\xf5\x01\x04\x00\x00\x00\x01"
      "\x10\x05:path\x08/foo/bar"
      "\x10\x07:scheme\x04http"
      "\x10\x07:method\x04POST"
      "\x10\x0a:authority\x09localhost"
      "\x10\x0c"
      "content-type\x10"
      "application/grpc"
      "\x10\x14grpc-accept-encoding\x15identity,deflate,gzip"
      "\x10\x0dgrpc-encoding\x1cyou-dont-know-how-to-do-this"
      "\x10\x02te\x08trailers"
      "\x10\x0auser-agent\"bad-client grpc-c/0.12.0.0 (linux)",
      GRPC_BAD_CLIENT_DISCONNECT);

  return 0;
}
