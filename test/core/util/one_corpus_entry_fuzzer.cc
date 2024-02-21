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
#include "test/core/util/tls_utils.h"

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size);

extern bool squelch;
extern bool leak_check;

int main(int argc, char** argv) {
  squelch = false;
  leak_check = false;
  GPR_ASSERT(argc > 1);  // Make sure that we have a filename argument
  std::string buffer = grpc_core::testing::GetFileContents(argv[1]);
  LLVMFuzzerTestOneInput(buffer.data(), buffer.size());
  return 0;
}
