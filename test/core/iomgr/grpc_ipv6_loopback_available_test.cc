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

#include "src/core/lib/iomgr/port.h"

// grpc_ipv6_loopback_available isn't currently available on UV.
#ifndef GRPC_UV

#include <grpc/grpc.h>
#include <grpc/support/log.h>
#include "test/core/util/test_config.h"

#ifdef GPR_WINDOWS
#include "src/core/lib/iomgr/socket_windows.h"
#else
#include "src/core/lib/iomgr/socket_utils_posix.h"
#endif

int main(int argc, char** argv) {
  grpc::testing::TestEnvironment env(argc, argv);
  grpc_init();
  // This test assumes that the ipv6 loopback is available
  // in all environments in which grpc tests run in.
  GPR_ASSERT(grpc_ipv6_loopback_available());
  grpc_shutdown();
  return 0;
}

#else

int main(int argc, char** argv) { return 0; }

#endif /* GRPC_UV */
