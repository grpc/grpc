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

#include <grpc/support/alloc.h>

#include "src/core/lib/surface/server.h"

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

static void verifier(grpc_server *server, grpc_completion_queue *cq,
                     void *registered_method) {
  while (grpc_server_has_open_connections(server)) {
    GPR_ASSERT(grpc_completion_queue_next(
                   cq, grpc_timeout_milliseconds_to_deadline(20), NULL)
                   .type == GRPC_QUEUE_TIMEOUT);
  }
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
  int i, j;
#define MAX_FRAME_SIZE 16384
#define MESSAGES_PER_FRAME (MAX_FRAME_SIZE / 5)
#define FRAME_SIZE (MESSAGES_PER_FRAME * 5)
#define SEND_SIZE (100 * 1024)
#define NUM_FRAMES (SEND_SIZE / FRAME_SIZE + 1)
  grpc_test_init(argc, argv);

  addbuf(PFX_STR, sizeof(PFX_STR) - 1);
  for (i = 0; i < NUM_FRAMES; i++) {
    uint8_t hdr[9] = {(uint8_t)(FRAME_SIZE >> 16),
                      (uint8_t)(FRAME_SIZE >> 8),
                      (uint8_t)FRAME_SIZE,
                      0,
                      0,
                      0,
                      0,
                      0,
                      1};
    addbuf(hdr, sizeof(hdr));
    for (j = 0; j < MESSAGES_PER_FRAME; j++) {
      uint8_t message[5] = {0, 0, 0, 0, 0};
      addbuf(message, sizeof(message));
    }
  }
  grpc_run_bad_client_test(verifier, NULL, g_buffer, g_count, 0);
  gpr_free(g_buffer);

  return 0;
}
