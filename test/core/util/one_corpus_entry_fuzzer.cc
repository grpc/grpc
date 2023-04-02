//
//
// Copyright 2016 gRPC authors.
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

#include <stdbool.h>

#include <grpc/grpc.h>
#include <grpc/support/log.h>

#include "src/core/lib/gprpp/crash.h"
#include "src/core/lib/iomgr/exec_ctx.h"
#include "src/core/lib/iomgr/load_file.h"

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size);

extern bool squelch;
extern bool leak_check;

int main(int argc, char** argv) {
  grpc_slice buffer;
  squelch = false;
  leak_check = false;
  // TODO(yashkt) Calling grpc_init breaks tests. Fix the tests and replace
  // grpc_core::ExecCtx::GlobalInit with grpc_init and GlobalShutdown with
  // grpc_shutdown
  GPR_ASSERT(argc > 1);  // Make sure that we have a filename argument
  GPR_ASSERT(
      GRPC_LOG_IF_ERROR("load_file", grpc_load_file(argv[1], 0, &buffer)));
  LLVMFuzzerTestOneInput(GRPC_SLICE_START_PTR(buffer),
                         GRPC_SLICE_LENGTH(buffer));
  grpc_core::ExecCtx::GlobalInit();
  grpc_slice_unref(buffer);
  grpc_core::ExecCtx::GlobalShutdown();
  return 0;
}
