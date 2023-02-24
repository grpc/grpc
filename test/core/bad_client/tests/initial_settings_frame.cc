//
//
// Copyright 2015 gRPC authors.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
//

#include <grpc/grpc.h>
#include <grpc/support/log.h>

#include "src/core/lib/surface/server.h"
#include "test/core/bad_client/bad_client.h"
#include "test/core/util/test_config.h"

#define PFX_STR "PRI * HTTP/2.0\r\n\r\nSM\r\n\r\n"
#define ONE_SETTING_HDR "\x00\x00\x06\x04\x00\x00\x00\x00\x00"

static void verifier(grpc_server* server, grpc_completion_queue* cq,
                     void* /*registered_method*/) {
  while (grpc_core::Server::FromC(server)->HasOpenConnections()) {
    GPR_ASSERT(grpc_completion_queue_next(
                   cq, grpc_timeout_milliseconds_to_deadline(20), nullptr)
                   .type == GRPC_QUEUE_TIMEOUT);
  }
}

int main(int argc, char** argv) {
  grpc::testing::TestEnvironment env(&argc, argv);
  grpc_init();

  // various partial prefixes
  GRPC_RUN_BAD_CLIENT_TEST(verifier, nullptr, PFX_STR "\x00",
                           GRPC_BAD_CLIENT_DISCONNECT);
  GRPC_RUN_BAD_CLIENT_TEST(verifier, nullptr, PFX_STR "\x00\x00",
                           GRPC_BAD_CLIENT_DISCONNECT);
  GRPC_RUN_BAD_CLIENT_TEST(verifier, nullptr, PFX_STR "\x00\x00\x00",
                           GRPC_BAD_CLIENT_DISCONNECT);
  GRPC_RUN_BAD_CLIENT_TEST(verifier, nullptr, PFX_STR "\x06",
                           GRPC_BAD_CLIENT_DISCONNECT);
  GRPC_RUN_BAD_CLIENT_TEST(verifier, nullptr, PFX_STR "\x00\x06",
                           GRPC_BAD_CLIENT_DISCONNECT);
  GRPC_RUN_BAD_CLIENT_TEST(verifier, nullptr, PFX_STR "\x00\x00\x06",
                           GRPC_BAD_CLIENT_DISCONNECT);
  GRPC_RUN_BAD_CLIENT_TEST(verifier, nullptr, PFX_STR "\x00\x00\x00\x04",
                           GRPC_BAD_CLIENT_DISCONNECT);
  GRPC_RUN_BAD_CLIENT_TEST(verifier, nullptr, PFX_STR "\x00\x00\x00\x04\x00",
                           GRPC_BAD_CLIENT_DISCONNECT);
  GRPC_RUN_BAD_CLIENT_TEST(verifier, nullptr, PFX_STR "\x00\x00\x00\x04\x01",
                           GRPC_BAD_CLIENT_DISCONNECT);
  GRPC_RUN_BAD_CLIENT_TEST(verifier, nullptr, PFX_STR "\x00\x00\x00\x04\xff",
                           GRPC_BAD_CLIENT_DISCONNECT);
  GRPC_RUN_BAD_CLIENT_TEST(verifier, nullptr,
                           PFX_STR "\x00\x00\x00\x04\x00\x00",
                           GRPC_BAD_CLIENT_DISCONNECT);
  GRPC_RUN_BAD_CLIENT_TEST(verifier, nullptr,
                           PFX_STR "\x00\x00\x00\x04\x00\x00\x00",
                           GRPC_BAD_CLIENT_DISCONNECT);
  GRPC_RUN_BAD_CLIENT_TEST(verifier, nullptr,
                           PFX_STR "\x00\x00\x00\x04\x00\x00\x00\x00",
                           GRPC_BAD_CLIENT_DISCONNECT);
  // must not send frames with stream id != 0
  GRPC_RUN_BAD_CLIENT_TEST(verifier, nullptr,
                           PFX_STR "\x00\x00\x00\x04\x00\x00\x00\x00\x01", 0);
  GRPC_RUN_BAD_CLIENT_TEST(verifier, nullptr,
                           PFX_STR "\x00\x00\x00\x04\x00\x40\x00\x00\x00", 0);
  // settings frame must be a multiple of six bytes long
  GRPC_RUN_BAD_CLIENT_TEST(verifier, nullptr,
                           PFX_STR "\x00\x00\x01\x04\x00\x00\x00\x00\x00", 0);
  GRPC_RUN_BAD_CLIENT_TEST(verifier, nullptr,
                           PFX_STR "\x00\x00\x02\x04\x00\x00\x00\x00\x00", 0);
  GRPC_RUN_BAD_CLIENT_TEST(verifier, nullptr,
                           PFX_STR "\x00\x00\x03\x04\x00\x00\x00\x00\x00", 0);
  GRPC_RUN_BAD_CLIENT_TEST(verifier, nullptr,
                           PFX_STR "\x00\x00\x04\x04\x00\x00\x00\x00\x00", 0);
  GRPC_RUN_BAD_CLIENT_TEST(verifier, nullptr,
                           PFX_STR "\x00\x00\x05\x04\x00\x00\x00\x00\x00", 0);
  // some settings values are illegal
  // max frame size = 0
  GRPC_RUN_BAD_CLIENT_TEST(verifier, nullptr,
                           PFX_STR ONE_SETTING_HDR "\x00\x05\x00\x00\x00\x00",
                           GRPC_BAD_CLIENT_DISCONNECT);
  GRPC_RUN_BAD_CLIENT_TEST(verifier, nullptr,
                           PFX_STR ONE_SETTING_HDR "\x00\x06\xff\xff\xff\xff",
                           GRPC_BAD_CLIENT_DISCONNECT);
  // update intiial window size
  GRPC_RUN_BAD_CLIENT_TEST(verifier, nullptr,
                           PFX_STR ONE_SETTING_HDR "\x00\x04\x00\x01\x00\x00",
                           GRPC_BAD_CLIENT_DISCONNECT);
  // ack with data
  GRPC_RUN_BAD_CLIENT_TEST(verifier, nullptr,
                           PFX_STR
                           "\x00\x00\x00\x04\x00\x00\x00\x00\x00"
                           "\x00\x00\x01\x04\x01\x00\x00\x00\x00",
                           0);
  // settings frame with invalid flags
  GRPC_RUN_BAD_CLIENT_TEST(verifier, nullptr,
                           PFX_STR "\x00\x00\x00\x04\x10\x00\x00\x00\x00", 0);
  // unknown settings should be ignored
  GRPC_RUN_BAD_CLIENT_TEST(verifier, nullptr,
                           PFX_STR ONE_SETTING_HDR "\x00\x99\x00\x00\x00\x00",
                           GRPC_BAD_CLIENT_DISCONNECT);

  grpc_shutdown();
  return 0;
}
