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

#include <inttypes.h>
#include <unistd.h>

#include <atomic>
#include <functional>
#include <initializer_list>
#include <memory>

#include "absl/strings/str_format.h"

#include <grpc/grpc.h>
#include <grpc/support/time.h>

#include "src/core/lib/channel/channel_args.h"
#include "test/core/end2end/end2end_tests.h"
#include "test/core/end2end/fixtures/secure_fixture.h"
#include "test/core/util/test_config.h"

static std::atomic<int> unique{1};

// All test configurations
static CoreTestConfiguration configs[] = {
    {"chttp2/fullstack_uds_abstract_namespace",
     FEATURE_MASK_SUPPORTS_DELAYED_CONNECTION |
         FEATURE_MASK_SUPPORTS_CLIENT_CHANNEL |
         FEATURE_MASK_SUPPORTS_AUTHORITY_HEADER,
     nullptr,
     [](const grpc_core::ChannelArgs&, const grpc_core::ChannelArgs&) {
       gpr_timespec now = gpr_now(GPR_CLOCK_REALTIME);
       return std::make_unique<InsecureFixture>(absl::StrFormat(
           "unix-abstract:grpc_fullstack_test.%d.%" PRId64 ".%" PRId32 ".%d",
           getpid(), now.tv_sec, now.tv_nsec,
           unique.fetch_add(1, std::memory_order_relaxed)));
     }},
};

int main(int argc, char** argv) {
  size_t i;

  grpc::testing::TestEnvironment env(&argc, argv);
  grpc_end2end_tests_pre_init();
  grpc_init();

  for (i = 0; i < sizeof(configs) / sizeof(*configs); i++) {
    grpc_end2end_tests(argc, argv, configs[i]);
  }

  grpc_shutdown();

  return 0;
}
