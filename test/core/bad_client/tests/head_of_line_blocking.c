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

#include <grpc/support/alloc.h>

#include "src/core/lib/surface/server.h"
#include "test/core/end2end/cq_verifier.h"

static const char prefix[] =
    "PRI * HTTP/2.0\r\n\r\nSM\r\n\r\n"
    // settings frame
    "\x00\x00\x00\x04\x00\x00\x00\x00\x00"
    // stream 1 headers: generated from server_registered_method.headers in this
    // directory
    "\x00\x00\xd0\x01\x04\x00\x00\x00\x01"
    "\x10\x05:path\x0f/registered/bar"
    "\x10\x07:scheme\x04http"
    "\x10\x07:method\x04POST"
    "\x10\x0a:authority\x09localhost"
    "\x10\x0c"
    "content-type\x10"
    "application/grpc"
    "\x10\x14grpc-accept-encoding\x15identity,deflate,gzip"
    "\x10\x02te\x08trailers"
    "\x10\x0auser-agent\"bad-client grpc-c/0.12.0.0 (linux)"
    // data frame for stream 1: advertise a 10000 byte payload (that we won't
    // fulfill)
    "\x00\x00\x05\x00\x00\x00\x00\x00\x01"
    "\x01\x00\x00\x27\x10"
    // stream 3 headers: generated from server_registered_method.headers in this
    // directory
    "\x00\x00\xd0\x01\x04\x00\x00\x00\x03"
    "\x10\x05:path\x0f/registered/bar"
    "\x10\x07:scheme\x04http"
    "\x10\x07:method\x04POST"
    "\x10\x0a:authority\x09localhost"
    "\x10\x0c"
    "content-type\x10"
    "application/grpc"
    "\x10\x14grpc-accept-encoding\x15identity,deflate,gzip"
    "\x10\x02te\x08trailers"
    "\x10\x0auser-agent\"bad-client grpc-c/0.12.0.0 (linux)"
    // data frame for stream 3: advertise a 10000 byte payload (that we will
    // fulfill)
    "\x00\x00\x05\x00\x00\x00\x00\x00\x03"
    "\x01\x00\x00\x27\x10"
    "";

static void *tag(intptr_t t) { return (void *)t; }

static void verifier(grpc_server *server, grpc_completion_queue *cq,
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
  grpc_call_unref(s);
  grpc_byte_buffer_destroy(payload);
  cq_verifier_destroy(cqv);
}

char *g_buffer;
size_t g_cap = 0;
size_t g_count = 0;

static void addbuf(const void *data, size_t len) {
  if (g_count + len > g_cap) {
    g_cap = GPR_MAX(g_count + len, g_cap * 2);
    g_buffer = gpr_realloc(g_buffer, g_cap);
  }
  memcpy(g_buffer + g_count, data, len);
  g_count += len;
}

int main(int argc, char **argv) {
  int i;
  grpc_test_init(argc, argv);

#define NUM_FRAMES 10
#define FRAME_SIZE 1000

  addbuf(prefix, sizeof(prefix) - 1);
  for (i = 0; i < NUM_FRAMES; i++) {
    uint8_t hdr[9] = {(uint8_t)(FRAME_SIZE >> 16),
                      (uint8_t)(FRAME_SIZE >> 8),
                      (uint8_t)FRAME_SIZE,
                      0,
                      0,
                      0,
                      0,
                      0,
                      3};
    uint8_t msg[FRAME_SIZE];
    memset(msg, 'a', sizeof(msg));
    addbuf(hdr, sizeof(hdr));
    addbuf(msg, FRAME_SIZE);
  }
  grpc_run_bad_client_test(verifier, NULL, g_buffer, g_count, 0);
  gpr_free(g_buffer);

  return 0;
}
