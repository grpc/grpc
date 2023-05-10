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

#include <functional>
#include <memory>

#include <grpc/grpc.h>

#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/iomgr/port.h"

// This test requires posix wakeup fds
#ifdef GRPC_POSIX_WAKEUP_FD

#include <string.h>

#include "src/core/lib/iomgr/wakeup_fd_posix.h"
#include "test/core/end2end/end2end_tests.h"
#include "test/core/end2end/fixtures/secure_fixture.h"
#include "test/core/util/test_config.h"

// All test configurations
static CoreTestConfiguration configs[] = {
    {"chttp2/fullstack",
     FEATURE_MASK_SUPPORTS_DELAYED_CONNECTION |
         FEATURE_MASK_SUPPORTS_CLIENT_CHANNEL |
         FEATURE_MASK_SUPPORTS_AUTHORITY_HEADER,
     nullptr,
     [](const grpc_core::ChannelArgs& /*client_args*/,
        const grpc_core::ChannelArgs& /*server_args*/) {
       return std::make_unique<InsecureFixture>();
     }},
};

int main(int argc, char** argv) {
  size_t i;

  grpc_allow_specialized_wakeup_fd = 0;

  grpc::testing::TestEnvironment env(&argc, argv);
  grpc_end2end_tests_pre_init();
  grpc_init();

  for (i = 0; i < sizeof(configs) / sizeof(*configs); i++) {
    grpc_end2end_tests(argc, argv, configs[i]);
  }

  grpc_shutdown();

  return 0;
}

#else  // GRPC_POSIX_WAKEUP_FD

int main(int argc, char** argv) { return 1; }

#endif  // GRPC_POSIX_WAKEUP_FD
