//
//
// Copyright 2019 gRPC authors.
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

#include <gtest/gtest.h>

#include <grpc/grpc.h>
#include <grpc/support/log.h>

#include "src/core/lib/surface/server.h"
#include "test/core/bad_client/bad_client.h"
#include "test/core/util/test_config.h"

#define HEADER_FRAME_ID_1                                                  \
  "\x00\x00\xc9\x01\x05\x00\x00\x00\x01" /* headers: generated from        \
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

#define HEADER_FRAME_ID_2                                                  \
  "\x00\x00\xc9\x01\x05\x00\x00\x00\x02" /* headers: generated from        \
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

#define HEADER_FRAME_ID_3                                                  \
  "\x00\x00\xc9\x01\x05\x00\x00\x00\x03" /* headers: generated from        \
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

namespace {

void verifier(grpc_server* server, grpc_completion_queue* cq,
              void* /*registered_method*/) {
  while (grpc_core::Server::FromC(server)->HasOpenConnections()) {
    GPR_ASSERT(grpc_completion_queue_next(
                   cq, grpc_timeout_milliseconds_to_deadline(20), nullptr)
                   .type == GRPC_QUEUE_TIMEOUT);
  }
}

TEST(BadStreamingId, RegularHeader) {
  grpc_bad_client_arg args[2];
  args[0] = connection_preface_arg;
  args[1].client_validator = nullptr;
  args[1].client_payload = HEADER_FRAME_ID_1;
  args[1].client_payload_length = sizeof(HEADER_FRAME_ID_1) - 1;
  grpc_run_bad_client_test(verifier, args, 2, GRPC_BAD_CLIENT_DISCONNECT);
}

TEST(BadStreamingId, NonClientStreamId) {
  grpc_bad_client_arg args[2];
  args[0] = connection_preface_arg;
  // send a header frame with non-client stream id 2
  args[1].client_validator = nullptr;
  args[1].client_payload = HEADER_FRAME_ID_2;
  args[1].client_payload_length = sizeof(HEADER_FRAME_ID_2) - 1;
  grpc_run_bad_client_test(verifier, args, 2, GRPC_BAD_CLIENT_DISCONNECT);
}

TEST(BadStreamingId, ClosedStreamId) {
  grpc_bad_client_arg args[4];
  args[0] = connection_preface_arg;
  // send a header frame with stream id 1
  args[1].client_validator = nullptr;
  args[1].client_payload = HEADER_FRAME_ID_1;
  args[1].client_payload_length = sizeof(HEADER_FRAME_ID_1) - 1;
  // send a header frame with stream id 3
  args[2].client_validator = nullptr;
  args[2].client_payload = HEADER_FRAME_ID_3;
  args[2].client_payload_length = sizeof(HEADER_FRAME_ID_3) - 1;
  // send a header frame with closed stream id 1 again
  args[3].client_validator = nullptr;
  args[3].client_payload = HEADER_FRAME_ID_1;
  args[3].client_payload_length = sizeof(HEADER_FRAME_ID_1) - 1;
  grpc_run_bad_client_test(verifier, args, 4, GRPC_BAD_CLIENT_DISCONNECT);
}

}  // namespace

int main(int argc, char** argv) {
  grpc::testing::TestEnvironment env(&argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  grpc_init();
  int retval = RUN_ALL_TESTS();
  grpc_shutdown();
  return retval;
}
