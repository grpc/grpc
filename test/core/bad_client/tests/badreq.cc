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

#include "src/core/lib/surface/server.h"
#include "test/core/end2end/cq_verifier.h"

#define PFX_STR                      \
  "PRI * HTTP/2.0\r\n\r\nSM\r\n\r\n" \
  "\x00\x00\x00\x04\x00\x00\x00\x00\x00" /* settings frame */

static void verifier(grpc_server* server, grpc_completion_queue* cq,
                     void* /*registered_method*/) {
  while (server->core_server->HasOpenConnections()) {
    GPR_ASSERT(grpc_completion_queue_next(
                   cq, grpc_timeout_milliseconds_to_deadline(20), nullptr)
                   .type == GRPC_QUEUE_TIMEOUT);
  }
}

int main(int argc, char** argv) {
  grpc::testing::TestEnvironment env(argc, argv);
  grpc_init();

  /* invalid content type */
  GRPC_RUN_BAD_CLIENT_TEST(
      verifier, nullptr,
      PFX_STR
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
      verifier, nullptr,
      PFX_STR
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
      verifier, nullptr,
      PFX_STR
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
      verifier, nullptr,
      PFX_STR
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
      verifier, nullptr,
      PFX_STR
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

  grpc_shutdown();
  return 0;
}
