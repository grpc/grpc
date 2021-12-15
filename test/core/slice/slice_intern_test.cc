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

#include <grpc/support/port_platform.h>

#include <inttypes.h>
#include <string.h>

#include <grpc/grpc.h>
#include <grpc/slice.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>

#include "src/core/lib/gprpp/memory.h"
#include "src/core/lib/slice/slice_internal.h"
#include "test/core/util/test_config.h"

#define LOG_TEST_NAME(x) gpr_log(GPR_INFO, "%s", x);

static void test_slice_interning(void) {
  LOG_TEST_NAME("test_slice_interning");

  grpc_init();
  grpc_slice src1 = grpc_slice_from_copied_string("hello123456789123456789");
  grpc_slice src2 = grpc_slice_from_copied_string("hello123456789123456789");

  // Explicitly checking that the slices are at different addresses prevents
  // failure with windows opt 64bit build.
  // See https://github.com/grpc/grpc/issues/20519
  GPR_ASSERT(&src1 != &src2);
  GPR_ASSERT(GRPC_SLICE_START_PTR(src1) != GRPC_SLICE_START_PTR(src2));

  grpc_slice interned1 = grpc_slice_intern(src1);
  grpc_slice interned2 = grpc_slice_intern(src2);
  GPR_ASSERT(GRPC_SLICE_START_PTR(interned1) ==
             GRPC_SLICE_START_PTR(interned2));
  GPR_ASSERT(GRPC_SLICE_START_PTR(interned1) != GRPC_SLICE_START_PTR(src1));
  GPR_ASSERT(GRPC_SLICE_START_PTR(interned2) != GRPC_SLICE_START_PTR(src2));
  grpc_slice_unref(src1);
  grpc_slice_unref(src2);
  grpc_slice_unref(interned1);
  grpc_slice_unref(interned2);
  grpc_shutdown();
}

int main(int argc, char** argv) {
  grpc::testing::TestEnvironment env(argc, argv);
  grpc_init();
  test_slice_interning();
  grpc_shutdown();
  return 0;
}
