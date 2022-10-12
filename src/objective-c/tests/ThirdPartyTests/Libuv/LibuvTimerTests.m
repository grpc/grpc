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
// libuv/test/test-timer.c
// libuv/test/test-timer-again.c
// libuv/test/test-timer-from-check.c

TEST_DECLARE(timer)
TEST_DECLARE(timer_init)
TEST_DECLARE(timer_again)
// TEST_DECLARE(timer_start_twice) //withhold until libuv patched
TEST_DECLARE(timer_order)
TEST_DECLARE(timer_huge_timeout)
TEST_DECLARE(timer_huge_repeat)
TEST_DECLARE(timer_run_once)
TEST_DECLARE(timer_from_check)
TEST_DECLARE(timer_is_closing)
TEST_DECLARE(timer_null_callback)
TEST_DECLARE(timer_early_check)

#define TASK_LIST_START_LOCAL static task_entry_t TEST_TASKS[] = {
TASK_LIST_START_LOCAL
TEST_ENTRY(timer)
TEST_ENTRY(timer_init)
TEST_ENTRY(timer_again)
// TEST_ENTRY(timer_start_twice)  //withhold until libuv patched
TEST_ENTRY(timer_order)
TEST_ENTRY(timer_huge_timeout)
TEST_ENTRY(timer_huge_repeat)
TEST_ENTRY(timer_run_once)
TEST_ENTRY(timer_from_check)
TEST_ENTRY(timer_is_closing)
TEST_ENTRY(timer_null_callback)
TEST_ENTRY(timer_early_check)
TASK_LIST_END

@interface LibuvTimerTests : XCTestCase

@end

@implementation LibuvTimerTests

- (void)testTimerAll {
  for (task_entry_t* task = TEST_TASKS; task->main; task++) {
    task->main();
  }
}

@end
