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

#define PFX_STR                                               \
  "PRI * HTTP/2.0\r\n\r\nSM\r\n\r\n"                          \
  "\x00\x00\x00\x04\x00\x00\x00\x00\x00" /* settings frame */ \
  "\x00\x00\xd0\x01\x04\x00\x00\x00\x01"                      \
  "\x10\x05:path\x0f/registered/bar"                          \
  "\x10\x07:scheme\x04http"                                   \
  "\x10\x07:method\x04POST"                                   \
  "\x10\x0a:authority\x09localhost"                           \
  "\x10\x0c"                                                  \
  "content-type\x10"                                          \
  "application/grpc"                                          \
  "\x10\x14grpc-accept-encoding\x15identity,deflate,gzip"     \
  "\x10\x02te\x08trailers"                                    \
  "\x10\x0auser-agent\"bad-client grpc-c/0.12.0.0 (linux)"

static void *tag(intptr_t t) { return (void *)t; }

static void verifier_succeeds(grpc_server *server, grpc_completion_queue *cq,
                              void *registered_method) {
  grpc_call_error error;
  grpc_call *s;
  cq_verifier *cqv = cq_verifier_create(cq);
  grpc_metadata_array request_metadata_recv;
  gpr_timespec deadline;
  grpc_byte_buffer *payload = NULL;

  grpc_metadata_array_init(&request_metadata_recv);

  error = grpc_server_request_registered_call(server, registered_method, &s,
                                              &deadline, &request_metadata_recv,
                                              &payload, cq, cq, tag(101));
  GPR_ASSERT(GRPC_CALL_OK == error);
  CQ_EXPECT_COMPLETION(cqv, tag(101), 1);
  cq_verify(cqv);

  GPR_ASSERT(payload != NULL);

  grpc_metadata_array_destroy(&request_metadata_recv);
  grpc_call_destroy(s);
  grpc_byte_buffer_destroy(payload);
  cq_verifier_destroy(cqv);
}

static void verifier_fails(grpc_server *server, grpc_completion_queue *cq,
                           void *registered_method) {
  grpc_call_error error;
  grpc_call *s;
  cq_verifier *cqv = cq_verifier_create(cq);
  grpc_metadata_array request_metadata_recv;
  gpr_timespec deadline;
  grpc_byte_buffer *payload = NULL;

  grpc_metadata_array_init(&request_metadata_recv);

  error = grpc_server_request_registered_call(server, registered_method, &s,
                                              &deadline, &request_metadata_recv,
                                              &payload, cq, cq, tag(101));
  GPR_ASSERT(GRPC_CALL_OK == error);
  CQ_EXPECT_COMPLETION(cqv, tag(101), 1);
  cq_verify(cqv);

  GPR_ASSERT(payload == NULL);

  grpc_metadata_array_destroy(&request_metadata_recv);
  grpc_call_destroy(s);
  cq_verifier_destroy(cqv);
}

int main(int argc, char **argv) {
  grpc_test_init(argc, argv);

  /* body generated with
   * tools/codegen/core/gen_server_registered_method_bad_client_test_body.py */
  GRPC_RUN_BAD_CLIENT_TEST(verifier_fails, NULL,
                           PFX_STR "\x00\x00\x00\x00\x00\x00\x00\x00\x01",
                           GRPC_BAD_CLIENT_DISCONNECT);
  GRPC_RUN_BAD_CLIENT_TEST(verifier_fails, NULL,
                           PFX_STR "\x00\x00\x01\x00\x00\x00\x00\x00\x01\x00",
                           GRPC_BAD_CLIENT_DISCONNECT);
  GRPC_RUN_BAD_CLIENT_TEST(verifier_fails, NULL, PFX_STR
                           "\x00\x00\x02\x00\x00\x00\x00\x00\x01\x00\x00",
                           GRPC_BAD_CLIENT_DISCONNECT);
  GRPC_RUN_BAD_CLIENT_TEST(verifier_fails, NULL, PFX_STR
                           "\x00\x00\x03\x00\x00\x00\x00\x00\x01\x00\x00\x00",
                           GRPC_BAD_CLIENT_DISCONNECT);
  GRPC_RUN_BAD_CLIENT_TEST(
      verifier_fails, NULL,
      PFX_STR "\x00\x00\x04\x00\x00\x00\x00\x00\x01\x00\x00\x00\x00",
      GRPC_BAD_CLIENT_DISCONNECT);
  GRPC_RUN_BAD_CLIENT_TEST(
      verifier_succeeds, NULL,
      PFX_STR "\x00\x00\x05\x00\x01\x00\x00\x00\x01\x00\x00\x00\x00\x00", 0);
  GRPC_RUN_BAD_CLIENT_TEST(
      verifier_fails, NULL,
      PFX_STR "\x00\x00\x05\x00\x00\x00\x00\x00\x01\x00\x00\x00\x00\x01",
      GRPC_BAD_CLIENT_DISCONNECT);
  GRPC_RUN_BAD_CLIENT_TEST(
      verifier_succeeds, NULL,
      PFX_STR "\x00\x00\x06\x00\x01\x00\x00\x00\x01\x00\x00\x00\x00\x01\x00",
      0);
  GRPC_RUN_BAD_CLIENT_TEST(
      verifier_fails, NULL,
      PFX_STR "\x00\x00\x05\x00\x00\x00\x00\x00\x01\x00\x00\x00\x00\x02",
      GRPC_BAD_CLIENT_DISCONNECT);
  GRPC_RUN_BAD_CLIENT_TEST(
      verifier_fails, NULL,
      PFX_STR "\x00\x00\x06\x00\x00\x00\x00\x00\x01\x00\x00\x00\x00\x02\x00",
      GRPC_BAD_CLIENT_DISCONNECT);
  GRPC_RUN_BAD_CLIENT_TEST(
      verifier_succeeds, NULL, PFX_STR
      "\x00\x00\x07\x00\x01\x00\x00\x00\x01\x00\x00\x00\x00\x02\x00\x00",
      0);

  return 0;
}
