/*
 *
 * Copyright 2021 gRPC authors.
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

#import "test/runner.h"
#import "test/task.h"

// Tests in:
// libuv/test/test-getaddrinfo.c

TEST_DECLARE(getaddrinfo_fail)
TEST_DECLARE(getaddrinfo_fail_sync)
TEST_DECLARE(getaddrinfo_basic)
TEST_DECLARE(getaddrinfo_basic_sync)
TEST_DECLARE(getaddrinfo_concurrent)

#define TASK_LIST_START_LOCAL static task_entry_t TEST_TASKS[] = {
TASK_LIST_START_LOCAL
TEST_ENTRY(getaddrinfo_fail)
TEST_ENTRY(getaddrinfo_fail_sync)
TEST_ENTRY(getaddrinfo_basic)
TEST_ENTRY(getaddrinfo_basic_sync)
TEST_ENTRY(getaddrinfo_concurrent)
TASK_LIST_END

@interface LibuvGetAddrInfoTests : XCTestCase

@end

@implementation LibuvGetAddrInfoTests

- (void)testGetAddrInfoAll {
  for (task_entry_t* task = TEST_TASKS; task->main; task++) {
    task->main();
  }
}

@end
