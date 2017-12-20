/*
 *
 * Copyright 2015 gRPC authors.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
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

static void* tag(intptr_t t) { return (void*)t; }

static void verifier_succeeds(grpc_server* server, grpc_completion_queue* cq,
                              void* registered_method) {
  grpc_call_error error;
  grpc_call* s;
  cq_verifier* cqv = cq_verifier_create(cq);
  grpc_metadata_array request_metadata_recv;
  gpr_timespec deadline;
  grpc_byte_buffer* payload = nullptr;

  grpc_metadata_array_init(&request_metadata_recv);

  error = grpc_server_request_registered_call(server, registered_method, &s,
                                              &deadline, &request_metadata_recv,
                                              &payload, cq, cq, tag(101));
  GPR_ASSERT(GRPC_CALL_OK == error);
  CQ_EXPECT_COMPLETION(cqv, tag(101), 1);
  cq_verify(cqv);

  GPR_ASSERT(payload != nullptr);

  grpc_metadata_array_destroy(&request_metadata_recv);
  grpc_call_unref(s);
  grpc_byte_buffer_destroy(payload);
  cq_verifier_destroy(cqv);
}

static void verifier_fails(grpc_server* server, grpc_completion_queue* cq,
                           void* registered_method) {
  while (grpc_server_has_open_connections(server)) {
    GPR_ASSERT(grpc_completion_queue_next(
                   cq, grpc_timeout_milliseconds_to_deadline(20), nullptr)
                   .type == GRPC_QUEUE_TIMEOUT);
  }
}

int main(int argc, char** argv) {
  grpc_test_init(argc, argv);
  grpc_init();

  /* body generated with
   * tools/codegen/core/gen_server_registered_method_bad_client_test_body.py */
  GRPC_RUN_BAD_CLIENT_TEST(verifier_fails, nullptr,
                           PFX_STR "\x00\x00\x00\x00\x00\x00\x00\x00\x01",
                           GRPC_BAD_CLIENT_DISCONNECT);
  GRPC_RUN_BAD_CLIENT_TEST(verifier_fails, nullptr,
                           PFX_STR "\x00\x00\x01\x00\x00\x00\x00\x00\x01\x00",
                           GRPC_BAD_CLIENT_DISCONNECT);
  GRPC_RUN_BAD_CLIENT_TEST(verifier_fails, nullptr,
                           PFX_STR
                           "\x00\x00\x02\x00\x00\x00\x00\x00\x01\x00\x00",
                           GRPC_BAD_CLIENT_DISCONNECT);
  GRPC_RUN_BAD_CLIENT_TEST(verifier_fails, nullptr,
                           PFX_STR
                           "\x00\x00\x03\x00\x00\x00\x00\x00\x01\x00\x00\x00",
                           GRPC_BAD_CLIENT_DISCONNECT);
  GRPC_RUN_BAD_CLIENT_TEST(
      verifier_fails, nullptr,
      PFX_STR "\x00\x00\x04\x00\x00\x00\x00\x00\x01\x00\x00\x00\x00",
      GRPC_BAD_CLIENT_DISCONNECT);
  GRPC_RUN_BAD_CLIENT_TEST(
      verifier_succeeds, nullptr,
      PFX_STR "\x00\x00\x05\x00\x01\x00\x00\x00\x01\x00\x00\x00\x00\x00", 0);
  GRPC_RUN_BAD_CLIENT_TEST(
      verifier_fails, nullptr,
      PFX_STR "\x00\x00\x05\x00\x00\x00\x00\x00\x01\x00\x00\x00\x00\x01",
      GRPC_BAD_CLIENT_DISCONNECT);
  GRPC_RUN_BAD_CLIENT_TEST(
      verifier_succeeds, nullptr,
      PFX_STR "\x00\x00\x06\x00\x01\x00\x00\x00\x01\x00\x00\x00\x00\x01\x00",
      0);
  GRPC_RUN_BAD_CLIENT_TEST(
      verifier_fails, nullptr,
      PFX_STR "\x00\x00\x05\x00\x00\x00\x00\x00\x01\x00\x00\x00\x00\x02",
      GRPC_BAD_CLIENT_DISCONNECT);
  GRPC_RUN_BAD_CLIENT_TEST(
      verifier_fails, nullptr,
      PFX_STR "\x00\x00\x06\x00\x00\x00\x00\x00\x01\x00\x00\x00\x00\x02\x00",
      GRPC_BAD_CLIENT_DISCONNECT);
  GRPC_RUN_BAD_CLIENT_TEST(
      verifier_succeeds, nullptr,
      PFX_STR
      "\x00\x00\x07\x00\x01\x00\x00\x00\x01\x00\x00\x00\x00\x02\x00\x00",
      0);

  grpc_shutdown();
  return 0;
}
