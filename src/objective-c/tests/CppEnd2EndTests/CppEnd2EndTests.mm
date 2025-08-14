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

#include "gtest/gtest.h"
#include "test/core/test_util/test_config.h"
#include "test/cpp/end2end/connection_attempt_injector.h"

@interface CppEnd2EndTests : XCTestCase
@end

@implementation CppEnd2EndTests

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

  grpc_init();
  grpc::testing::ConnectionAttemptInjector::Init();
  int r = RUN_ALL_TESTS();
  grpc_shutdown();

  XCTAssertEqual(r, 0);
}

@end
