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

#include <grpc/support/log.h>

static void do_nothing(grpc_exec_ctx *exec_ctx, void *arg, grpc_error *error) {}

int main(int argc, char **argv) {
  grpc_test_init(argc, argv);

  uint8_t buffer[] = "abc123";
  grpc_stream_refcount r;
  GRPC_STREAM_REF_INIT(&r, 1, do_nothing, NULL, "test");
  GPR_ASSERT(r.refs.count == 1);
  grpc_slice slice =
      grpc_slice_from_stream_owned_buffer(&r, buffer, sizeof(buffer));
  GPR_ASSERT(GRPC_SLICE_START_PTR(slice) == buffer);
  GPR_ASSERT(GRPC_SLICE_LENGTH(slice) == sizeof(buffer));
  GPR_ASSERT(r.refs.count == 2);
  grpc_slice_unref(slice);
  GPR_ASSERT(r.refs.count == 1);

  return 0;
}
