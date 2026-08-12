/**
 * Copyright 2024 gRPC authors.
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
 */

#import <Foundation/Foundation.h>
#import <XCTest/XCTest.h>

@interface GTestStyleReporter : NSObject <XCTestObservation>
@end

@implementation GTestStyleReporter {
  NSUInteger _totalTests;
  NSMutableArray<NSString *> *_failedTestNames;
  NSTimeInterval _suiteStartTime;
}

+ (void)load {
  static dispatch_once_t onceToken;
  dispatch_once(&onceToken, ^{
    [[XCTestObservationCenter sharedTestObservationCenter] addTestObserver:[[self alloc] init]];
  });
}

- (instancetype)init {
  self = [super init];
  if (self) {
    _totalTests = 0;
    _failedTestNames = [[NSMutableArray alloc] init];
  }
  return self;
}

- (void)testBundleWillStart:(NSBundle *)testBundle {
  _suiteStartTime = [NSDate timeIntervalSinceReferenceDate];
}

- (void)testCaseWillStart:(XCTestCase *)testCase {
  _totalTests++;
  printf("[ RUN      ] %s\n", testCase.name.UTF8String);
  fflush(stdout);
}

#if defined(__IPHONE_14_0) || defined(__MAC_11_0)
- (void)testCase:(XCTestCase *)testCase didRecordIssue:(XCTIssue *)issue {
  NSString *filePath = issue.sourceCodeContext.location.fileURL.path ?: @"unknown";
  NSUInteger lineNumber = issue.sourceCodeContext.location.lineNumber;
  printf("%s:%lu: Failure\n%s\n", filePath.UTF8String, (unsigned long)lineNumber,
         issue.compactDescription.UTF8String);
  fflush(stdout);
}
#endif

- (void)testCase:(XCTestCase *)testCase
    didFailWithDescription:(NSString *)description
                    inFile:(nullable NSString *)filePath
                    atLine:(NSUInteger)lineNumber {
  printf("%s:%lu: Failure\n%s\n", filePath ? filePath.UTF8String : "unknown",
         (unsigned long)lineNumber, description.UTF8String);
  fflush(stdout);
}

- (void)testCaseDidFinish:(XCTestCase *)testCase {
  NSTimeInterval durationMs = testCase.testRun.totalDuration * 1000.0;
  if (testCase.testRun.hasSucceeded) {
    printf("[       OK ] %s (%.0f ms)\n", testCase.name.UTF8String, durationMs);
  } else {
    [_failedTestNames addObject:testCase.name];
    printf("[  FAILED  ] %s (%.0f ms)\n", testCase.name.UTF8String, durationMs);
  }
  fflush(stdout);
}

- (void)testBundleDidFinish:(NSBundle *)testBundle {
  NSTimeInterval totalDurationMs =
      ([NSDate timeIntervalSinceReferenceDate] - _suiteStartTime) * 1000.0;
  NSUInteger failedCount = _failedTestNames.count;
  NSUInteger passedCount = _totalTests >= failedCount ? (_totalTests - failedCount) : 0;

  printf("[==========] %lu tests ran. (%.0f ms total)\n", (unsigned long)_totalTests,
         totalDurationMs);
  printf("[  PASSED  ] %lu tests.\n", (unsigned long)passedCount);

  if (failedCount > 0) {
    printf("[  FAILED  ] %lu test%s, listed below:\n", (unsigned long)failedCount,
           failedCount == 1 ? "" : "s");
    for (NSString *failedTest in _failedTestNames) {
      printf("[  FAILED  ] %s\n", failedTest.UTF8String);
    }
    printf("\n %lu FAILED TEST%s\n", (unsigned long)failedCount, failedCount == 1 ? "" : "S");
  }
  fflush(stdout);
}

@end
