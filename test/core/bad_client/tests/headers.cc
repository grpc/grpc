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

#include "src/core/lib/surface/server.h"
#include "test/core/bad_client/bad_client.h"

#define PFX_STR                      \
  "PRI * HTTP/2.0\r\n\r\nSM\r\n\r\n" \
  "\x00\x00\x00\x04\x00\x00\x00\x00\x00"

static void verifier(grpc_server* server, grpc_completion_queue* cq,
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

  /* partial http2 header prefixes */
  GRPC_RUN_BAD_CLIENT_TEST(verifier, nullptr, PFX_STR "\x00",
                           GRPC_BAD_CLIENT_DISCONNECT);
  GRPC_RUN_BAD_CLIENT_TEST(verifier, nullptr, PFX_STR "\x00\x00",
                           GRPC_BAD_CLIENT_DISCONNECT);
  GRPC_RUN_BAD_CLIENT_TEST(verifier, nullptr, PFX_STR "\x00\x00\x00",
                           GRPC_BAD_CLIENT_DISCONNECT);
  GRPC_RUN_BAD_CLIENT_TEST(verifier, nullptr, PFX_STR "\x00\x00\x00\x01",
                           GRPC_BAD_CLIENT_DISCONNECT);
  GRPC_RUN_BAD_CLIENT_TEST(verifier, nullptr, PFX_STR "\x00\x00\x00\x01\x00",
                           GRPC_BAD_CLIENT_DISCONNECT);
  GRPC_RUN_BAD_CLIENT_TEST(verifier, nullptr, PFX_STR "\x00\x00\x00\x01\x04",
                           GRPC_BAD_CLIENT_DISCONNECT);
  GRPC_RUN_BAD_CLIENT_TEST(verifier, nullptr, PFX_STR "\x00\x00\x00\x01\x05",
                           GRPC_BAD_CLIENT_DISCONNECT);
  GRPC_RUN_BAD_CLIENT_TEST(verifier, nullptr,
                           PFX_STR "\x00\x00\x00\x01\x04\x00",
                           GRPC_BAD_CLIENT_DISCONNECT);
  GRPC_RUN_BAD_CLIENT_TEST(verifier, nullptr,
                           PFX_STR "\x00\x00\x00\x01\x04\x00\x00",
                           GRPC_BAD_CLIENT_DISCONNECT);
  GRPC_RUN_BAD_CLIENT_TEST(verifier, nullptr,
                           PFX_STR "\x00\x00\x00\x01\x04\x00\x00\x00",
                           GRPC_BAD_CLIENT_DISCONNECT);
  GRPC_RUN_BAD_CLIENT_TEST(verifier, nullptr,
                           PFX_STR "\x00\x00\x00\x01\x04\x00\x00\x00\x00",
                           GRPC_BAD_CLIENT_DISCONNECT);
  GRPC_RUN_BAD_CLIENT_TEST(verifier, nullptr,
                           PFX_STR "\x00\x00\x00\x01\x04\x00\x00\x00\x01",
                           GRPC_BAD_CLIENT_DISCONNECT);

  /* test adding prioritization data */
  GRPC_RUN_BAD_CLIENT_TEST(verifier, nullptr,
                           PFX_STR
                           "\x00\x00\x01\x01\x24\x00\x00\x00\x01"
                           "\x00",
                           0);
  GRPC_RUN_BAD_CLIENT_TEST(verifier, nullptr,
                           PFX_STR
                           "\x00\x00\x02\x01\x24\x00\x00\x00\x01"
                           "\x00\x00",
                           0);
  GRPC_RUN_BAD_CLIENT_TEST(verifier, nullptr,
                           PFX_STR
                           "\x00\x00\x03\x01\x24\x00\x00\x00\x01"
                           "\x00\x00\x00",
                           0);
  GRPC_RUN_BAD_CLIENT_TEST(verifier, nullptr,
                           PFX_STR
                           "\x00\x00\x04\x01\x24\x00\x00\x00\x01"
                           "\x00\x00\x00\x00",
                           0);
  GRPC_RUN_BAD_CLIENT_TEST(verifier, nullptr,
                           PFX_STR
                           "\x00\x00\x05\x01\x24\x00\x00\x00\x01"
                           "",
                           GRPC_BAD_CLIENT_DISCONNECT);
  GRPC_RUN_BAD_CLIENT_TEST(verifier, nullptr,
                           PFX_STR
                           "\x00\x00\x05\x01\x24\x00\x00\x00\x01"
                           "\x00",
                           GRPC_BAD_CLIENT_DISCONNECT);
  GRPC_RUN_BAD_CLIENT_TEST(verifier, nullptr,
                           PFX_STR
                           "\x00\x00\x05\x01\x24\x00\x00\x00\x01"
                           "\x00\x00",
                           GRPC_BAD_CLIENT_DISCONNECT);
  GRPC_RUN_BAD_CLIENT_TEST(verifier, nullptr,
                           PFX_STR
                           "\x00\x00\x05\x01\x24\x00\x00\x00\x01"
                           "\x00\x00\x00",
                           GRPC_BAD_CLIENT_DISCONNECT);
  GRPC_RUN_BAD_CLIENT_TEST(verifier, nullptr,
                           PFX_STR
                           "\x00\x00\x05\x01\x24\x00\x00\x00\x01"
                           "\x00\x00\x00\x00",
                           GRPC_BAD_CLIENT_DISCONNECT);
  GRPC_RUN_BAD_CLIENT_TEST(verifier, nullptr,
                           PFX_STR
                           "\x00\x00\x05\x01\x24\x00\x00\x00\x01"
                           "\x00\x00\x00\x00\x00",
                           GRPC_BAD_CLIENT_DISCONNECT);

  /* test looking up an invalid index */
  GRPC_RUN_BAD_CLIENT_TEST(verifier, nullptr,
                           PFX_STR
                           "\x00\x00\x01\x01\x04\x00\x00\x00\x01"
                           "\xfe",
                           0);
  GRPC_RUN_BAD_CLIENT_TEST(verifier, nullptr,
                           PFX_STR
                           "\x00\x00\x04\x01\x04\x00\x00\x00\x01"
                           "\x7f\x7f\x01"
                           "a",
                           0);
  GRPC_RUN_BAD_CLIENT_TEST(verifier, nullptr,
                           PFX_STR
                           "\x00\x00\x04\x01\x04\x00\x00\x00\x01"
                           "\x0f\x7f\x01"
                           "a",
                           0);
  GRPC_RUN_BAD_CLIENT_TEST(verifier, nullptr,
                           PFX_STR
                           "\x00\x00\x04\x01\x04\x00\x00\x00\x01"
                           "\x1f\x7f\x01"
                           "a",
                           0);
  /* test nvr, not indexed in static table */
  GRPC_RUN_BAD_CLIENT_TEST(verifier, nullptr,
                           PFX_STR
                           "\x00\x00\x03\x01\x04\x00\x00\x00\x01"
                           "\x01\x01"
                           "a",
                           GRPC_BAD_CLIENT_DISCONNECT);
  GRPC_RUN_BAD_CLIENT_TEST(verifier, nullptr,
                           PFX_STR
                           "\x00\x00\x03\x01\x04\x00\x00\x00\x01"
                           "\x11\x01"
                           "a",
                           GRPC_BAD_CLIENT_DISCONNECT);
  /* illegal op code */
  GRPC_RUN_BAD_CLIENT_TEST(verifier, nullptr,
                           PFX_STR
                           "\x00\x00\x01\x01\x04\x00\x00\x00\x01"
                           "\x80",
                           0);
  /* parse some long indices */
  GRPC_RUN_BAD_CLIENT_TEST(verifier, nullptr,
                           PFX_STR
                           "\x00\x00\x02\x01\x04\x00\x00\x00\x01"
                           "\xff\x00",
                           0);
  GRPC_RUN_BAD_CLIENT_TEST(verifier, nullptr,
                           PFX_STR
                           "\x00\x00\x03\x01\x04\x00\x00\x00\x01"
                           "\xff\x80\x00",
                           0);
  GRPC_RUN_BAD_CLIENT_TEST(verifier, nullptr,
                           PFX_STR
                           "\x00\x00\x04\x01\x04\x00\x00\x00\x01"
                           "\xff\x80\x80\x00",
                           0);
  GRPC_RUN_BAD_CLIENT_TEST(verifier, nullptr,
                           PFX_STR
                           "\x00\x00\x05\x01\x04\x00\x00\x00\x01"
                           "\xff\x80\x80\x80\x00",
                           0);
  GRPC_RUN_BAD_CLIENT_TEST(verifier, nullptr,
                           PFX_STR
                           "\x00\x00\x06\x01\x04\x00\x00\x00\x01"
                           "\xff\x80\x80\x80\x80\x00",
                           0);
  GRPC_RUN_BAD_CLIENT_TEST(verifier, nullptr,
                           PFX_STR
                           "\x00\x00\x07\x01\x04\x00\x00\x00\x01"
                           "\xff\x80\x80\x80\x80\x80\x00",
                           0);
  GRPC_RUN_BAD_CLIENT_TEST(verifier, nullptr,
                           PFX_STR
                           "\x00\x00\x08\x01\x04\x00\x00\x00\x01"
                           "\xff",
                           GRPC_BAD_CLIENT_DISCONNECT);
  GRPC_RUN_BAD_CLIENT_TEST(verifier, nullptr,
                           PFX_STR
                           "\x00\x00\x08\x01\x04\x00\x00\x00\x01"
                           "\xff\x80",
                           GRPC_BAD_CLIENT_DISCONNECT);
  GRPC_RUN_BAD_CLIENT_TEST(verifier, nullptr,
                           PFX_STR
                           "\x00\x00\x08\x01\x04\x00\x00\x00\x01"
                           "\xff\x80\x80",
                           GRPC_BAD_CLIENT_DISCONNECT);
  GRPC_RUN_BAD_CLIENT_TEST(verifier, nullptr,
                           PFX_STR
                           "\x00\x00\x08\x01\x04\x00\x00\x00\x01"
                           "\xff\x80\x80\x80",
                           GRPC_BAD_CLIENT_DISCONNECT);
  GRPC_RUN_BAD_CLIENT_TEST(verifier, nullptr,
                           PFX_STR
                           "\x00\x00\x08\x01\x04\x00\x00\x00\x01"
                           "\xff\x80\x80\x80\x80",
                           GRPC_BAD_CLIENT_DISCONNECT);
  GRPC_RUN_BAD_CLIENT_TEST(verifier, nullptr,
                           PFX_STR
                           "\x00\x00\x08\x01\x04\x00\x00\x00\x01"
                           "\xff\x80\x80\x80\x80\x80",
                           GRPC_BAD_CLIENT_DISCONNECT);
  GRPC_RUN_BAD_CLIENT_TEST(verifier, nullptr,
                           PFX_STR
                           "\x00\x00\x08\x01\x04\x00\x00\x00\x01"
                           "\xff\x80\x80\x80\x80\x80\x80",
                           GRPC_BAD_CLIENT_DISCONNECT);
  GRPC_RUN_BAD_CLIENT_TEST(verifier, nullptr,
                           PFX_STR
                           "\x00\x00\x08\x01\x04\x00\x00\x00\x01"
                           "\xff\x80\x80\x80\x80\x80\x80\x00",
                           0);
  /* overflow on byte 4 */
  GRPC_RUN_BAD_CLIENT_TEST(verifier, nullptr,
                           PFX_STR
                           "\x00\x00\x06\x01\x04\x00\x00\x00\x01"
                           "\xff\x80\x80\x80\x80\x7f",
                           GRPC_BAD_CLIENT_DISCONNECT);
  GRPC_RUN_BAD_CLIENT_TEST(verifier, nullptr,
                           PFX_STR
                           "\x00\x00\x06\x01\x04\x00\x00\x00\x01"
                           "\xff\xff\xff\xff\xff\x0f",
                           GRPC_BAD_CLIENT_DISCONNECT);
  /* overflow after byte 4 */
  GRPC_RUN_BAD_CLIENT_TEST(verifier, nullptr,
                           PFX_STR
                           "\x00\x00\x08\x01\x04\x00\x00\x00\x01"
                           "\xff\x80\x80\x80\x80\x80\x80\x02",
                           0);
  /* end of headers mid-opcode */
  GRPC_RUN_BAD_CLIENT_TEST(verifier, nullptr,
                           PFX_STR
                           "\x00\x00\x01\x01\x04\x00\x00\x00\x01"
                           "\x01",
                           GRPC_BAD_CLIENT_DISCONNECT);

  /* dynamic table size update: set to default */
  GRPC_RUN_BAD_CLIENT_TEST(verifier, nullptr,
                           PFX_STR
                           "\x00\x00\x03\x01\x04\x00\x00\x00\x01"
                           "\x3f\xe1\x1f",
                           GRPC_BAD_CLIENT_DISCONNECT);
  /* dynamic table size update: set too large */
  GRPC_RUN_BAD_CLIENT_TEST(verifier, nullptr,
                           PFX_STR
                           "\x00\x00\x03\x01\x04\x00\x00\x00\x01"
                           "\x3f\xf1\x1f",
                           0);
  /* dynamic table size update: set twice */
  GRPC_RUN_BAD_CLIENT_TEST(verifier, nullptr,
                           PFX_STR
                           "\x00\x00\x04\x01\x04\x00\x00\x00\x01"
                           "\x20\x3f\xe1\x1f",
                           GRPC_BAD_CLIENT_DISCONNECT);
  /* dynamic table size update: set thrice */
  GRPC_RUN_BAD_CLIENT_TEST(verifier, nullptr,
                           PFX_STR
                           "\x00\x00\x03\x01\x04\x00\x00\x00\x01"
                           "\x20\x20\x20",
                           0);

  /* non-ending header followed by continuation frame */
  GRPC_RUN_BAD_CLIENT_TEST(verifier, nullptr,
                           PFX_STR
                           "\x00\x00\x00\x01\x00\x00\x00\x00\x01"
                           "\x00\x00\x00\x09\x04\x00\x00\x00\x01",
                           GRPC_BAD_CLIENT_DISCONNECT);
  /* non-ending header followed by non-continuation frame */
  GRPC_RUN_BAD_CLIENT_TEST(verifier, nullptr,
                           PFX_STR
                           "\x00\x00\x00\x01\x00\x00\x00\x00\x01"
                           "\x00\x00\x00\x00\x04\x00\x00\x00\x01",
                           0);
  /* non-ending header followed by a continuation frame for a different stream
   */
  GRPC_RUN_BAD_CLIENT_TEST(verifier, nullptr,
                           PFX_STR
                           "\x00\x00\x00\x01\x04\x00\x00\x00\x01"
                           "\x00\x00\x00\x01\x00\x00\x00\x00\x03"
                           "\x00\x00\x00\x09\x04\x00\x00\x00\x01",
                           0);
  /* opening with a continuation frame */
  GRPC_RUN_BAD_CLIENT_TEST(verifier, nullptr,
                           PFX_STR "\x00\x00\x00\x09\x04\x00\x00\x00\x01", 0);
  /* three header frames */
  GRPC_RUN_BAD_CLIENT_TEST(verifier, nullptr,
                           PFX_STR
                           "\x00\x00\x00\x01\x04\x00\x00\x00\x01"
                           "\x00\x00\x00\x01\x04\x00\x00\x00\x01"
                           "\x00\x00\x00\x01\x04\x00\x00\x00\x01",
                           GRPC_BAD_CLIENT_DISCONNECT);

  /* an invalid header found with fuzzing */
  GRPC_RUN_BAD_CLIENT_TEST(verifier, nullptr,
                           PFX_STR "\x00\x00\x00\x01\x39\x67\xed\x1d\x64",
                           GRPC_BAD_CLIENT_DISCONNECT);

  /* a badly encoded timeout value */
  GRPC_RUN_BAD_CLIENT_TEST(verifier, nullptr,
                           PFX_STR
                           "\x00\x00\x19\x01\x04\x00\x00\x00\x01"
                           "\x10\x0cgrpc-timeout\x0a"
                           "15 seconds",
                           GRPC_BAD_CLIENT_DISCONNECT);
  /* a badly encoded timeout value: twice (catches caching) */
  GRPC_RUN_BAD_CLIENT_TEST(verifier, nullptr,
                           PFX_STR
                           "\x00\x00\x19\x01\x04\x00\x00\x00\x01"
                           "\x10\x0cgrpc-timeout\x0a"
                           "15 seconds"
                           "\x00\x00\x19\x01\x04\x00\x00\x00\x03"
                           "\x10\x0cgrpc-timeout\x0a"
                           "15 seconds",
                           GRPC_BAD_CLIENT_DISCONNECT);

  grpc_shutdown();
  return 0;
}
