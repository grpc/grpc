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

#include <string>

#include <gtest/gtest.h>

#include <grpc/grpc.h>
#include <grpc/support/log.h>

#include "src/core/lib/surface/server.h"
#include "test/core/bad_client/bad_client.h"
#include "test/core/util/test_config.h"

static void verifier(grpc_server* server, grpc_completion_queue* cq,
                     void* /*registered_method*/) {
  while (grpc_core::Server::FromC(server)->HasOpenConnections()) {
    GPR_ASSERT(grpc_completion_queue_next(
                   cq, grpc_timeout_milliseconds_to_deadline(20), nullptr)
                   .type == GRPC_QUEUE_TIMEOUT);
  }
}

#define APPEND_BUFFER(string, to_append) \
  ((string).append((to_append), sizeof(to_append) - 1))

namespace {
TEST(UnknownFrameType, Test) {
  // test that all invalid/unknown frame types are handled
  for (int i = 10; i <= 255; i++) {
    std::string unknown_frame_string;
    APPEND_BUFFER(unknown_frame_string, "\x00\x00\x00");
    char frame_type = static_cast<char>(i);
    unknown_frame_string.append(&frame_type, 1);
    APPEND_BUFFER(unknown_frame_string, "\x00\x00\x00\x00\x01");
    grpc_bad_client_arg args[2];
    args[0] = connection_preface_arg;
    args[1].client_validator = nullptr;
    args[1].client_payload = unknown_frame_string.c_str();
    args[1].client_payload_length = unknown_frame_string.size();
    grpc_run_bad_client_test(verifier, args, 2, GRPC_BAD_CLIENT_DISCONNECT);
  }
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
