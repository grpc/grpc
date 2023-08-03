/*
 *
 * Copyright 2023 gRPC authors.
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

#import <XCTest/XCTest.h>

#include <grpc/grpc.h>

#include "src/core/lib/event_engine/cf_engine/cf_engine.h"
#include "test/core/event_engine/test_suite/event_engine_test_framework.h"
#include "test/core/event_engine/test_suite/posix/oracle_event_engine_posix.h"
#include "test/core/event_engine/test_suite/tests/client_test.h"
#include "test/core/util/test_config.h"

@interface EventEngineTimerTests : XCTestCase
@end

@implementation EventEngineTimerTests

- (void)testAll {
  NSArray *arguments = [NSProcessInfo processInfo].arguments;
  int argc = (int)arguments.count;
  char **argv = static_cast<char **>(alloca((sizeof(char *) * (argc + 1))));
  for (int index = 0; index < argc; index++) {
    argv[index] = const_cast<char *>([arguments[index] UTF8String]);
  }
  argv[argc] = NULL;

  testing::InitGoogleTest(&argc, (char **)argv);
  grpc::testing::TestEnvironment env(&argc, (char **)argv);

  auto factory = []() {
    return std::make_unique<grpc_event_engine::experimental::CFEventEngine>();
  };
  auto oracle_factory = []() {
    return std::make_unique<grpc_event_engine::experimental::PosixOracleEventEngine>();
  };
  SetEventEngineFactories(factory, oracle_factory);
  grpc_event_engine::experimental::InitClientTests();
  // TODO(ctiller): EventEngine temporarily needs grpc to be initialized first
  // until we clear out the iomgr shutdown code.
  grpc_init();
  int r = RUN_ALL_TESTS();
  grpc_shutdown();

  XCTAssertEqual(r, 0);
}

@end
