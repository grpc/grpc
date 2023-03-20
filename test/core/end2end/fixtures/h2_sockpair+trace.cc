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
#include <string>

#include "absl/types/optional.h"

#include <grpc/grpc.h>
#include <grpc/support/log.h>

#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/config/config_vars.h"
#include "src/core/lib/iomgr/port.h"
#include "test/core/end2end/end2end_tests.h"
#include "test/core/end2end/fixtures/sockpair_fixture.h"
#include "test/core/util/test_config.h"

#ifdef GRPC_POSIX_SOCKET
#include <unistd.h>
#endif

// All test configurations
static CoreTestConfiguration configs[] = {
    {"chttp2/socketpair", FEATURE_MASK_SUPPORTS_AUTHORITY_HEADER, nullptr,
     [](const grpc_core::ChannelArgs&, const grpc_core::ChannelArgs&) {
       return std::make_unique<SockpairFixture>(grpc_core::ChannelArgs());
     }},
};

int main(int argc, char** argv) {
  size_t i;

  // force tracing on, with a value to force many
  // code paths in trace.cc to be taken
  grpc_core::ConfigVars::Overrides overrides;
  overrides.trace = "doesnt-exist,http,all";
  grpc_core::ConfigVars::SetOverrides(overrides);

#ifdef GRPC_POSIX_SOCKET
  g_fixture_slowdown_factor = isatty(STDOUT_FILENO) ? 10 : 1;
#else
  g_fixture_slowdown_factor = 10;
#endif

#ifdef GPR_WINDOWS
  // on Windows, writing logs to stderr is very slow
  // when stderr is redirected to a disk file.
  // The "trace" tests fixtures generates large amount
  // of logs, so setting a buffer for stderr prevents certain
  // test cases from timing out.
  setvbuf(stderr, NULL, _IOLBF, 1024);
#endif

  grpc::testing::TestEnvironment env(&argc, argv);
  grpc_end2end_tests_pre_init();
  grpc_init();

  GPR_ASSERT(0 == grpc_tracer_set_enabled("also-doesnt-exist", 0));
  GPR_ASSERT(1 == grpc_tracer_set_enabled("http", 1));
  GPR_ASSERT(1 == grpc_tracer_set_enabled("all", 1));

  for (i = 0; i < sizeof(configs) / sizeof(*configs); i++) {
    grpc_end2end_tests(argc, argv, configs[i]);
  }

  grpc_shutdown();

  return 0;
}
