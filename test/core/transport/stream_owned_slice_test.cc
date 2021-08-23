/*
 *
 * Copyright 2017 gRPC authors.
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

#include "src/core/lib/transport/transport.h"

#include "test/core/util/test_config.h"

#include <grpc/grpc.h>
#include <grpc/support/log.h>

static void do_nothing(void* /*arg*/, grpc_error_handle /*error*/) {}

int main(int argc, char** argv) {
  grpc::testing::TestEnvironment env(argc, argv);
  grpc_init();

  uint8_t buffer[] = "abc123";
  grpc_stream_refcount r;
  GRPC_STREAM_REF_INIT(&r, 1, do_nothing, nullptr, "test");
  grpc_slice slice =
      grpc_slice_from_stream_owned_buffer(&r, buffer, sizeof(buffer));
  GPR_ASSERT(GRPC_SLICE_START_PTR(slice) == buffer);
  GPR_ASSERT(GRPC_SLICE_LENGTH(slice) == sizeof(buffer));
  grpc_slice_unref(slice);

  grpc_shutdown();
  return 0;
}
