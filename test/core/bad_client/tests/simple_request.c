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

#define PFX_STR                                                            \
  "PRI * HTTP/2.0\r\n\r\nSM\r\n\r\n"                                       \
  "\x00\x00\x00\x04\x00\x00\x00\x00\x00" /* settings frame */              \
  "\x00\x00\xc9\x01\x04\x00\x00\x00\x01" /* headers: generated from        \
                                            simple_request.headers in this \
                                            directory */                   \
  "\x10\x05:path\x08/foo/bar"                                              \
  "\x10\x07:scheme\x04http"                                                \
  "\x10\x07:method\x04POST"                                                \
  "\x10\x0a:authority\x09localhost"                                        \
  "\x10\x0c"                                                               \
  "content-type\x10"                                                       \
  "application/grpc"                                                       \
  "\x10\x14grpc-accept-encoding\x15"                                       \
  "deflate,identity,gzip"                                                  \
  "\x10\x02te\x08trailers"                                                 \
  "\x10\x0auser-agent\"bad-client grpc-c/0.12.0.0 (linux)"

#define PFX_STR_UNUSUAL                                                    \
  "PRI * HTTP/2.0\r\n\r\nSM\r\n\r\n"                                       \
  "\x00\x00\x00\x04\x00\x00\x00\x00\x00" /* settings frame */              \
  "\x00\x00\xf4\x01\x04\x00\x00\x00\x01" /* headers: generated from        \
                                            simple_request_unusual.headers \
                                            in this directory */           \
  "\x10\x05:path\x08/foo/bar"                                              \
  "\x10\x07:scheme\x04http"                                                \
  "\x10\x07:method\x04POST"                                                \
  "\x10\x04host\x09localhost"                                              \
  "\x10\x0c"                                                               \
  "content-type\x1e"                                                       \
  "application/grpc+this-is-valid"                                         \
  "\x10\x14grpc-accept-encoding\x15identity,deflate,gzip"                  \
  "\x10\x02te\x08trailers"                                                 \
  "\x10\x0auser-agent\"bad-client grpc-c/0.12.0.0 (linux)"                 \
  "\x10\x0cgrpc-timeout\x03"                                               \
  "10S"                                                                    \
  "\x10\x0cgrpc-timeout\x02"                                               \
  "5S"

#define PFX_STR_UNUSUAL2                                                    \
  "PRI * HTTP/2.0\r\n\r\nSM\r\n\r\n"                                        \
  "\x00\x00\x00\x04\x00\x00\x00\x00\x00" /* settings frame */               \
  "\x00\x00\xf4\x01\x04\x00\x00\x00\x01" /* headers: generated from         \
                                            simple_request_unusual2.headers \
                                            in this directory */            \
  "\x10\x05:path\x08/foo/bar"                                               \
  "\x10\x07:scheme\x04http"                                                 \
  "\x10\x07:method\x04POST"                                                 \
  "\x10\x04host\x09localhost"                                               \
  "\x10\x0c"                                                                \
  "content-type\x1e"                                                        \
  "application/grpc;this-is-valid"                                          \
  "\x10\x14grpc-accept-encoding\x15identity,deflate,gzip"                   \
  "\x10\x02te\x08trailers"                                                  \
  "\x10\x0auser-agent\"bad-client grpc-c/0.12.0.0 (linux)"                  \
  "\x10\x0cgrpc-timeout\x03"                                                \
  "10S"                                                                     \
  "\x10\x0cgrpc-timeout\x02"                                                \
  "5S"

static void *tag(intptr_t t) { return (void *)t; }

static void verifier(grpc_server *server, grpc_completion_queue *cq,
                     void *registered_method) {
  grpc_call_error error;
  grpc_call *s;
  grpc_call_details call_details;
  cq_verifier *cqv = cq_verifier_create(cq);
  grpc_metadata_array request_metadata_recv;

  grpc_call_details_init(&call_details);
  grpc_metadata_array_init(&request_metadata_recv);

  error = grpc_server_request_call(server, &s, &call_details,
                                   &request_metadata_recv, cq, cq, tag(101));
  GPR_ASSERT(GRPC_CALL_OK == error);
  CQ_EXPECT_COMPLETION(cqv, tag(101), 1);
  cq_verify(cqv);

  GPR_ASSERT(0 == grpc_slice_str_cmp(call_details.host, "localhost"));
  GPR_ASSERT(0 == grpc_slice_str_cmp(call_details.method, "/foo/bar"));

  grpc_metadata_array_destroy(&request_metadata_recv);
  grpc_call_details_destroy(&call_details);
  grpc_call_destroy(s);
  cq_verifier_destroy(cqv);
}

static void failure_verifier(grpc_server *server, grpc_completion_queue *cq,
                             void *registered_method) {
  while (grpc_server_has_open_connections(server)) {
    GPR_ASSERT(grpc_completion_queue_next(
                   cq, grpc_timeout_milliseconds_to_deadline(20), NULL)
                   .type == GRPC_QUEUE_TIMEOUT);
  }
}

int main(int argc, char **argv) {
  grpc_test_init(argc, argv);

  /* basic request: check that things are working */
  GRPC_RUN_BAD_CLIENT_TEST(verifier, NULL, PFX_STR, 0);
  GRPC_RUN_BAD_CLIENT_TEST(verifier, NULL, PFX_STR_UNUSUAL, 0);
  GRPC_RUN_BAD_CLIENT_TEST(verifier, NULL, PFX_STR_UNUSUAL2, 0);

  /* push an illegal data frame */
  GRPC_RUN_BAD_CLIENT_TEST(verifier, NULL, PFX_STR
                           "\x00\x00\x05\x00\x00\x00\x00\x00\x01"
                           "\x34\x00\x00\x00\x00",
                           0);

  /* push a data frame with bad flags */
  GRPC_RUN_BAD_CLIENT_TEST(verifier, NULL,
                           PFX_STR "\x00\x00\x00\x00\x02\x00\x00\x00\x01", 0);
  /* push a window update with a bad length */
  GRPC_RUN_BAD_CLIENT_TEST(failure_verifier, NULL,
                           PFX_STR "\x00\x00\x01\x08\x00\x00\x00\x00\x01", 0);
  /* push a window update with bad flags */
  GRPC_RUN_BAD_CLIENT_TEST(failure_verifier, NULL,
                           PFX_STR "\x00\x00\x00\x08\x10\x00\x00\x00\x01", 0);
  /* push a window update with bad data */
  GRPC_RUN_BAD_CLIENT_TEST(failure_verifier, NULL, PFX_STR
                           "\x00\x00\x04\x08\x00\x00\x00\x00\x01"
                           "\xff\xff\xff\xff",
                           0);
  /* push a short goaway */
  GRPC_RUN_BAD_CLIENT_TEST(failure_verifier, NULL,
                           PFX_STR "\x00\x00\x04\x07\x00\x00\x00\x00\x00", 0);
  /* disconnect before sending goaway */
  GRPC_RUN_BAD_CLIENT_TEST(failure_verifier, NULL,
                           PFX_STR "\x00\x01\x12\x07\x00\x00\x00\x00\x00",
                           GRPC_BAD_CLIENT_DISCONNECT);
  /* push a rst_stream with a bad length */
  GRPC_RUN_BAD_CLIENT_TEST(failure_verifier, NULL,
                           PFX_STR "\x00\x00\x01\x03\x00\x00\x00\x00\x01", 0);
  /* push a rst_stream with bad flags */
  GRPC_RUN_BAD_CLIENT_TEST(failure_verifier, NULL,
                           PFX_STR "\x00\x00\x00\x03\x10\x00\x00\x00\x01", 0);

  return 0;
}
