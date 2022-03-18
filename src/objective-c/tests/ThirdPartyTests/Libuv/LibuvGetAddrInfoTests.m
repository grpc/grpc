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

@interface LibuvGetAddrInfoTests2 : XCTestCase

@end

@implementation LibuvGetAddrInfoTests2

- (void)testGetAddrInfoAll {
  for (task_entry_t* task = TEST_TASKS; task->main; task++) {
    task->main();
  }
}

@end
