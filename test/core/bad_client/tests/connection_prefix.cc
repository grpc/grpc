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

  GRPC_RUN_BAD_CLIENT_TEST(verifier, nullptr, "X", 0);
  GRPC_RUN_BAD_CLIENT_TEST(verifier, nullptr, "PX", 0);
  GRPC_RUN_BAD_CLIENT_TEST(verifier, nullptr, "PRX", 0);
  GRPC_RUN_BAD_CLIENT_TEST(verifier, nullptr, "PRIX", 0);
  GRPC_RUN_BAD_CLIENT_TEST(verifier, nullptr, "PRI X", 0);
  GRPC_RUN_BAD_CLIENT_TEST(verifier, nullptr, "PRI *X", 0);
  GRPC_RUN_BAD_CLIENT_TEST(verifier, nullptr, "PRI * X", 0);
  GRPC_RUN_BAD_CLIENT_TEST(verifier, nullptr, "PRI * HX", 0);
  GRPC_RUN_BAD_CLIENT_TEST(verifier, nullptr, "PRI * HTX", 0);
  GRPC_RUN_BAD_CLIENT_TEST(verifier, nullptr, "PRI * HTTX", 0);
  GRPC_RUN_BAD_CLIENT_TEST(verifier, nullptr, "PRI * HTTPX", 0);
  GRPC_RUN_BAD_CLIENT_TEST(verifier, nullptr, "PRI * HTTP/X", 0);
  GRPC_RUN_BAD_CLIENT_TEST(verifier, nullptr, "PRI * HTTP/2X", 0);
  GRPC_RUN_BAD_CLIENT_TEST(verifier, nullptr, "PRI * HTTP/2.X", 0);
  GRPC_RUN_BAD_CLIENT_TEST(verifier, nullptr, "PRI * HTTP/2.0X", 0);
  GRPC_RUN_BAD_CLIENT_TEST(verifier, nullptr, "PRI * HTTP/2.0\rX", 0);
  GRPC_RUN_BAD_CLIENT_TEST(verifier, nullptr, "PRI * HTTP/2.0\r\nX", 0);
  GRPC_RUN_BAD_CLIENT_TEST(verifier, nullptr, "PRI * HTTP/2.0\r\n\rX", 0);
  GRPC_RUN_BAD_CLIENT_TEST(verifier, nullptr, "PRI * HTTP/2.0\r\n\r\nX", 0);
  GRPC_RUN_BAD_CLIENT_TEST(verifier, nullptr, "PRI * HTTP/2.0\r\n\r\nSX", 0);
  GRPC_RUN_BAD_CLIENT_TEST(verifier, nullptr, "PRI * HTTP/2.0\r\n\r\nSMX", 0);
  GRPC_RUN_BAD_CLIENT_TEST(verifier, nullptr, "PRI * HTTP/2.0\r\n\r\nSM\rX", 0);
  GRPC_RUN_BAD_CLIENT_TEST(verifier, nullptr, "PRI * HTTP/2.0\r\n\r\nSM\r\nX",
                           0);
  GRPC_RUN_BAD_CLIENT_TEST(verifier, nullptr, "PRI * HTTP/2.0\r\n\r\nSM\r\n\rX",
                           0);

  grpc_shutdown();
  return 0;
}
