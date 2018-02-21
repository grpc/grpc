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

#include <grpc/grpc.h>
#include <grpc/support/alloc.h>

#include "src/core/lib/surface/server.h"

#define PFX_STR                                                            \
  "\x00\x00\x00\x04\x01\x00\x00\x00\x00"                                   \
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

static void verifier(grpc_server* server, grpc_completion_queue* cq,
                     void* registered_method) {
  while (grpc_server_has_open_connections(server)) {
    GPR_ASSERT(grpc_completion_queue_next(
                   cq, grpc_timeout_milliseconds_to_deadline(20), nullptr)
                   .type == GRPC_QUEUE_TIMEOUT);
  }
}

char* g_buffer;
size_t g_cap = 0;
size_t g_count = 0;

static void addbuf(const void* data, size_t len) {
  if (g_count + len > g_cap) {
    g_cap = GPR_MAX(g_count + len, g_cap * 2);
    g_buffer = static_cast<char*>(gpr_realloc(g_buffer, g_cap));
  }
  memcpy(g_buffer + g_count, data, len);
  g_count += len;
}

int main(int argc, char** argv) {
  int i, j;
#define MAX_FRAME_SIZE 16384
#define MESSAGES_PER_FRAME (MAX_FRAME_SIZE / 5)
#define FRAME_SIZE (MESSAGES_PER_FRAME * 5)
#define SEND_SIZE (4 * 1024 * 1024)
#define NUM_FRAMES (SEND_SIZE / FRAME_SIZE + 1)
  grpc_test_init(argc, argv);
  grpc_init();

  addbuf(PFX_STR, sizeof(PFX_STR) - 1);
  for (i = 0; i < NUM_FRAMES; i++) {
    uint8_t hdr[9] = {static_cast<uint8_t>(FRAME_SIZE >> 16),
                      static_cast<uint8_t>(FRAME_SIZE >> 8),
                      static_cast<uint8_t>
                          FRAME_SIZE,
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
  grpc_bad_client_arg bca[2];
  bca[0] = connection_preface_arg;
  bca[1] = {rst_stream_client_validator, nullptr, g_buffer, g_count};
  grpc_run_bad_client_test(verifier, bca, 2, GRPC_BAD_CLIENT_LARGE_REQUEST);
  gpr_free(g_buffer);
  grpc_shutdown();

  return 0;
}
