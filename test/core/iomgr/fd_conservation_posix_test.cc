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
#include <sys/resource.h>

#include "absl/log/check.h"
#include "src/core/lib/iomgr/endpoint_pair.h"
#include "src/core/lib/iomgr/iomgr.h"
#include "src/core/util/crash.h"
#include "test/core/test_util/test_config.h"

int main(int argc, char** argv) {
  int i;
  struct rlimit rlim;
  grpc_endpoint_pair p;

  grpc::testing::TestEnvironment env(&argc, argv);
  grpc_init();
  {
    grpc_core::ExecCtx exec_ctx;

    // set max # of file descriptors to a low value, and
    // verify we can create and destroy many more than this number
    // of descriptors
    rlim.rlim_cur = rlim.rlim_max = 1000;
    CHECK_EQ(setrlimit(RLIMIT_NOFILE, &rlim), 0);
    for (i = 0; i < 10000; i++) {
      p = grpc_iomgr_create_endpoint_pair("test", nullptr);
      grpc_endpoint_destroy(p.client);
      grpc_endpoint_destroy(p.server);
      grpc_core::ExecCtx::Get()->Flush();
    }
  }

  grpc_shutdown();
  return 0;
}
