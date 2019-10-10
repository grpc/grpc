//
//  GTMGoogleTestRunner.mm
//
//  Copyright 2013 Google Inc.
//
//  Licensed under the Apache License, Version 2.0 (the "License"); you may not
//  use this file except in compliance with the License.  You may obtain a copy
//  of the License at
//
//  http://www.apache.org/licenses/LICENSE-2.0
//
//  Unless required by applicable law or agreed to in writing, software
//  distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
//  WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.  See the
//  License for the specific language governing permissions and limitations under
//  the License.
//

#if !defined(__has_feature) || !__has_feature(objc_arc)
#error "This file requires ARC support."
#endif

// This is a XCTest based unit test that will run all of the GoogleTest
// https://code.google.com/p/googletest/
// based tests in the project, and will report results correctly via XCTest so
// that Xcode can pick them up in it's UI.

// XCTest dynamically creates one XCTest per GoogleTest.
// GoogleTest is set up using a custom event listener (GoogleTestPrinter)
// which knows how to log GoogleTest test results in a manner that XCTest (and
// the Xcode IDE) understand.

// Note that this does not able you to control individual tests from the Xcode
// UI. You can only turn on/off all of the C++ tests. It does however give
// you output that you can click on in the Xcode UI and immediately jump to a
// test failure.

// This class is not compiled as part of the standard Google Toolbox For Mac
// project because of it's dependency on https://code.google.com/p/googletest/

// To use this:
// - Add GTMGoogleTestRunner to your test bundle sources.
// - Add gtest-all.cc from gtest to your test bundle sources.
// - Write some C++ tests and add them to your test bundle sources.
// - Build and run tests. Your C++ tests should just execute.

// NOTE:
// A key difference between how GTMGoogleTestRunner runs tests versus how a
// "standard" unit test package runs tests is that SetUpTestSuite/SetupTestCase
// and TeardownTestSuite/TeardownTestCase are going to be called before/after
// *every* individual test. Unfortunately this is due to restrictions in the
// design of GoogleTest in that the only way to run individual tests is to
// use a filter to focus on a specific test, and then "run" all the tests
// multiple times.
// If you have state that you need maintained across tests (not normally a
// great idea anyhow), using SetUp*, Teardown* is not going to work for you.

#import <XCTest/XCTest.h>
#import <objc/runtime.h>

#include <gtest/gtest.h>

using ::testing::EmptyTestEventListener;
using ::testing::TestCase;
using ::testing::TestEventListener;
using ::testing::TestEventListeners;
using ::testing::TestInfo;
using ::testing::TestPartResult;
using ::testing::TestResult;
using ::testing::UnitTest;

namespace {

// A gtest printer that takes care of reporting gtest results via the
// XCTest interface. Note that a test suite in XCTest == a test case in gtest
// and a test case in XCTest == a test in gtest.
// This will handle fatal and non-fatal gtests properly.
class GoogleTestPrinter : public EmptyTestEventListener {
 public:
  GoogleTestPrinter(XCTestCase *test_case) : test_case_(test_case) {}

  virtual ~GoogleTestPrinter() {}

  virtual void OnTestPartResult(const TestPartResult &test_part_result) {
    if (!test_part_result.passed()) {
      const char *file_name = test_part_result.file_name();
      NSString *file = @(file_name ? file_name : "<file name unavailable>");
      int line = test_part_result.line_number();
      NSString *summary = @(test_part_result.summary());

      // gtest likes to give multi-line summaries. These don't look good in
      // the Xcode UI, so we clean them up.
      NSString *oneLineSummary =
          [summary stringByReplacingOccurrencesOfString:@"\n" withString:@" "];
      BOOL expected = test_part_result.nonfatally_failed();
      [test_case_ recordFailureWithDescription:oneLineSummary
                                        inFile:file
                                        atLine:line
                                      expected:expected];
    }
  }

 private:
  XCTestCase *test_case_;
};

NSString *SelectorNameFromGTestName(NSString *testName) {
  NSRange dot = [testName rangeOfString:@"."];
  return [NSString stringWithFormat:@"%@::%@",
          [testName substringToIndex:dot.location],
          [testName substringFromIndex:dot.location + 1]];
}

}  // namespace

// GTMGoogleTestRunner is a GTMTestCase that makes a sub test suite populated
// with all of the GoogleTest unit tests.
@interface GTMGoogleTestRunner : XCTestCase {
  NSString *testName_;
}

// The name for a test is the GoogleTest name which is "TestCase.Test"
- (id)initWithName:(NSString *)testName;
@end

@implementation GTMGoogleTestRunner

+ (void)initGoogleTest {
  static dispatch_once_t onceToken;
  dispatch_once(&onceToken, ^{
    NSArray *arguments = [NSProcessInfo processInfo].arguments;
    int argc = (int)arguments.count;
    char **argv = static_cast<char **>(alloca((sizeof(char *) * (argc + 1))));
    for (int index = 0; index < argc; index++) {
      argv[index] = const_cast<char *> ([arguments[index] UTF8String]);
    }
    argv[argc] = NULL;

    testing::InitGoogleTest(&argc, argv);
  });
}

+ (id)defaultTestSuite {
  [GTMGoogleTestRunner initGoogleTest];
  XCTestSuite *result = [[XCTestSuite alloc] initWithName:NSStringFromClass(self)];
  UnitTest *test = UnitTest::GetInstance();

  // Walk the GoogleTest tests, adding sub tests and sub suites as appropriate.
  int total_test_case_count = test->total_test_case_count();
  for (int i = 0; i < total_test_case_count; ++i) {
    const TestCase *test_case = test->GetTestCase(i);
    int total_test_count = test_case->total_test_count();
    XCTestSuite *subSuite = [[XCTestSuite alloc] initWithName:@(test_case->name())];
    [result addTest:subSuite];
    for (int j = 0; j < total_test_count; ++j) {
      const TestInfo *test_info = test_case->GetTestInfo(j);
      NSString *testName = [NSString stringWithFormat:@"%s.%s",
                            test_case->name(), test_info->name()];
      XCTestCase *xcTest = [[self alloc] initWithName:testName];
      [subSuite addTest:xcTest];
    }
  }
  return result;
}

- (id)initWithName:(NSString *)testName {
  // Xcode 6.1 started taking the testName from the selector instead of calling
  // -name.
  // So we will add selectors to GTMGoogleTestRunner.
  // They should all be unique because the selectors are named cppclass.method
  // Filed as radar 18798444.
  Class cls = [self class];
  NSString *selectorTestName = SelectorNameFromGTestName(testName);
  SEL selector = sel_registerName([selectorTestName UTF8String]);
  Method method = class_getInstanceMethod(cls, @selector(runGoogleTest));
  IMP implementation = method_getImplementation(method);
  const char *encoding = method_getTypeEncoding(method);
  if (!class_addMethod(cls, selector, implementation, encoding)) {
    // If we can't add a method, we should blow up here.
    [NSException raise:NSInternalInconsistencyException
                format:@"Unable to add %@ to %@.", testName, cls];
  }
  if ((self = [super initWithSelector:selector])) {
    testName_ = testName;
  }
  return self;
}

- (NSString *)name {
  // An XCTest name must be "-[foo bar]" or it won't be parsed properly.
  NSRange dot = [testName_ rangeOfString:@"."];
  return [NSString stringWithFormat:@"-[%@ %@]",
          [testName_ substringToIndex:dot.location],
          [testName_ substringFromIndex:dot.location + 1]];
}

- (void)runGoogleTest {
  [GTMGoogleTestRunner initGoogleTest];

  // Gets hold of the event listener list.
  TestEventListeners& listeners = UnitTest::GetInstance()->listeners();

  // Adds a listener to the end.
  GoogleTestPrinter printer = GoogleTestPrinter(self);
  listeners.Append(&printer);

  // Remove the default printer if it exists.
  TestEventListener *defaultListener = listeners.default_result_printer();
  if (defaultListener) {
    delete listeners.Release(defaultListener);
  }

  // Since there is no way of running a single GoogleTest directly, we use the
  // filter mechanism in GoogleTest to simulate it for us.
  ::testing::GTEST_FLAG(filter) = [testName_ UTF8String];

  // Intentionally ignore return value of RUN_ALL_TESTS. We will be printing
  // the output appropriately, and there is no reason to mark this test as
  // "failed" if RUN_ALL_TESTS returns non-zero.
  (void)RUN_ALL_TESTS();

  // Remove the listener that we added.
  listeners.Release(&printer);
}

@end
