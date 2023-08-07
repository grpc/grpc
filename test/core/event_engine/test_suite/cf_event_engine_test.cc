// Copyright 2022 The gRPC Authors
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
#include <grpc/support/port_platform.h>

#ifdef GPR_APPLE

#include <grpc/grpc.h>

#include "src/core/lib/event_engine/cf_engine/cf_engine.h"
#include "test/core/event_engine/test_suite/event_engine_test_framework.h"
#include "test/core/event_engine/test_suite/posix/oracle_event_engine_posix.h"
#include "test/core/event_engine/test_suite/tests/client_test.h"
#include "test/core/event_engine/test_suite/tests/timer_test.h"
#include "test/core/util/test_config.h"

int main(int argc, char** argv) {
  testing::InitGoogleTest(&argc, argv);
  grpc::testing::TestEnvironment env(&argc, argv);
  auto factory = []() {
    return std::make_unique<grpc_event_engine::experimental::CFEventEngine>();
  };
  auto oracle_factory = []() {
    return std::make_unique<
        grpc_event_engine::experimental::PosixOracleEventEngine>();
  };
  SetEventEngineFactories(factory, oracle_factory);
  grpc_event_engine::experimental::InitTimerTests();
  grpc_event_engine::experimental::InitClientTests();
  // TODO(ctiller): EventEngine temporarily needs grpc to be initialized first
  // until we clear out the iomgr shutdown code.
  grpc_init();
  int r = RUN_ALL_TESTS();
  grpc_shutdown();
  return r;
}

#else  // GPR_APPLE

int main(int /* argc */, char** /* argv */) { return 0; }

#endif  // GPR_APPLE
