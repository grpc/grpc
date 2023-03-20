// Copyright 2021 gRPC authors.
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

#include <inttypes.h>
#include <unistd.h>

#include <atomic>
#include <functional>
#include <initializer_list>
#include <memory>

#include "absl/strings/str_format.h"

#include <grpc/grpc.h>
#include <grpc/grpc_security_constants.h>
#include <grpc/support/time.h>

#include "src/core/lib/channel/channel_args.h"
#include "test/core/end2end/end2end_tests.h"
#include "test/core/end2end/fixtures/local_util.h"
#include "test/core/util/test_config.h"

static std::atomic<int> unique{0};

// All test configurations
static CoreTestConfiguration configs[] = {
    {"chttp2/fullstack_local_abstract_uds_percent_encoded",
     FEATURE_MASK_SUPPORTS_DELAYED_CONNECTION |
         FEATURE_MASK_SUPPORTS_CLIENT_CHANNEL |
         FEATURE_MASK_SUPPORTS_AUTHORITY_HEADER |
         FEATURE_MASK_SUPPORTS_PER_CALL_CREDENTIALS,
     nullptr,
     [](const grpc_core::ChannelArgs& /*client_args*/,
        const grpc_core::ChannelArgs& /*server_args*/) {
       gpr_timespec now = gpr_now(GPR_CLOCK_MONOTONIC);
       return std::make_unique<LocalTestFixture>(
           absl::StrFormat("unix-abstract:grpc_fullstack_test.%%00.%d.%" PRId64
                           ".%" PRId32 ".%d",
                           getpid(), now.tv_sec, now.tv_nsec,
                           unique.fetch_add(1, std::memory_order_relaxed)),
           UDS);
     }}};

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
