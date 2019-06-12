//
//  GTMServiceManagementTest.m
//
//  Copyright 2010 Google Inc.
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

#import "GTMServiceManagement.h"

#if MAC_OS_X_VERSION_MIN_REQUIRED > MAC_OS_X_VERSION_10_4

#import "GTMSenTestCase.h"
#import <servers/bootstrap.h>

#define OUR_JOB_LABEL "com.google.gtm.GTMServiceManagementTest.job"
#define BAD_JOB_LABEL "com.google.gtm.GTMServiceManagementTest.badjob"
#define TEST_HARNESS_LABEL "com.google.gtm.GTMServiceManagementTestHarness"
#define GTM_MACH_PORT_NAME "GTMServiceManagementTestingHarnessMachPort"

static NSString const *kGTMSocketKey
  = @"COM_GOOGLE_GTM_GTMSERVICEMANAGEMENT_TEST_SOCKET";
static NSString const *kGTMSocketName
  = @"GTMServiceManagementTesting";

#pragma clang diagnostic push
// Ignore all of the deprecation warnings for GTMServiceManagement
#pragma clang diagnostic ignored "-Wdeprecated-declarations"

@interface GTMServiceManagementTest : GTMTestCase
@end

@implementation GTMServiceManagementTest

- (void)testDataConversion {
  const char *someData = "someData";
  NSDictionary *subDict
    = [NSDictionary dictionaryWithObjectsAndKeys:
       [NSNumber numberWithBool:1], @"BoolValue",
       [NSNumber numberWithInt:2], @"IntValue",
       [NSNumber numberWithDouble:0.3], @"DoubleValue",
       @"A String", @"StringValue",
       [NSData dataWithBytes:someData length:strlen(someData)], @"DataValue",
       nil];
  NSArray *subArray
    = [NSArray arrayWithObjects:@"1", [NSNumber numberWithInt:2], nil];
  NSDictionary *topDict = [NSDictionary dictionaryWithObjectsAndKeys:
                           subDict, @"SubDict",
                           subArray, @"SubArray",
                           @"Random String", @"RandomString",
                           nil];
  CFErrorRef error = NULL;
  launch_data_t launchDict = GTMLaunchDataCreateFromCFType(topDict, &error);
  XCTAssertNotNULL(launchDict);
  XCTAssertNULL(error, @"Error: %@", error);
  NSDictionary *nsDict
    = GTMCFAutorelease(GTMCFTypeCreateFromLaunchData(launchDict,
                                                     NO,
                                                     &error));
  XCTAssertNotNil(nsDict);
  XCTAssertNULL(error, @"Error: %@", error);
  XCTAssertEqualObjects(nsDict, topDict, @"");

  launch_data_free(launchDict);

  // Test a bad type
  NSURL *url = [NSURL URLWithString:@"http://www.google.com"];
  XCTAssertNotNil(url);
  launchDict = GTMLaunchDataCreateFromCFType(url, &error);
  XCTAssertNULL(launchDict);
  XCTAssertNotNULL(error);
  XCTAssertEqualObjects((id)CFErrorGetDomain(error),
                        (id)kCFErrorDomainPOSIX);
  XCTAssertEqual(CFErrorGetCode(error), (CFIndex)EINVAL);
  if (error) {
    CFRelease(error);
  }


  CFTypeRef cfType = GTMCFTypeCreateFromLaunchData(NULL, YES, &error);
  XCTAssertNULL(cfType);
  XCTAssertNotNULL(error);
  if (error) {
    CFRelease(error);
  }
}

- (void)testJobDictionaries {
  NSDictionary *jobs = GTMCFAutorelease(GTMSMCopyAllJobDictionaries());
  XCTAssertNotNil(jobs);

  // Grab an existing job
  NSString *jobName = [[jobs allKeys] objectAtIndex:0];
  NSDictionary *job
    = GTMCFAutorelease(GTMSMJobCopyDictionary((CFStringRef)jobName));
  XCTAssertNotNil(job);

  // A job that should never be around
  CFTypeRef type = GTMSMJobCopyDictionary(CFSTR(BAD_JOB_LABEL));
  XCTAssertNULL(type);
}

- (void)testLaunching {
  CFErrorRef error = NULL;
  Boolean isGood = GTMSMJobSubmit(NULL, &error);
  XCTAssertFalse(isGood);
  XCTAssertNotNULL(error);
  if (error) {
    CFRelease(error);
  }


  NSDictionary *empty = [NSDictionary dictionary];
  isGood = GTMSMJobSubmit((CFDictionaryRef)empty, &error);
  XCTAssertFalse(isGood);
  XCTAssertNotNULL(error);
  if (error) {
    CFRelease(error);
  }

  // Grab an existing job
  NSDictionary *jobs = GTMCFAutorelease(GTMSMCopyAllJobDictionaries());
  XCTAssertNotNil(jobs);
  NSString *jobName = [[jobs allKeys] objectAtIndex:0];

  NSDictionary *alreadyThere
    = [NSDictionary dictionaryWithObject:jobName
                                  forKey:@LAUNCH_JOBKEY_LABEL];
  isGood = GTMSMJobSubmit((CFDictionaryRef)alreadyThere, &error);
  XCTAssertFalse(isGood);
  XCTAssertEqual([(NSError *)error code], (NSInteger)EEXIST);
  if (error) {
    CFRelease(error);
  }

  NSDictionary *goodJob
    = [NSDictionary dictionaryWithObjectsAndKeys:
       @OUR_JOB_LABEL, @LAUNCH_JOBKEY_LABEL,
       @"/bin/test", @LAUNCH_JOBKEY_PROGRAM,
       nil];
  isGood = GTMSMJobSubmit((CFDictionaryRef)goodJob, &error);
  XCTAssertTrue(isGood);
  XCTAssertNULL(error);

  isGood = GTMSMJobRemove(CFSTR(OUR_JOB_LABEL), &error);
  XCTAssertTrue(isGood,
                @"You may need to run launchctl remove %s", OUR_JOB_LABEL);
  XCTAssertNULL(error);

  isGood = GTMSMJobRemove(CFSTR(OUR_JOB_LABEL), &error);
  XCTAssertFalse(isGood);
  XCTAssertNotNULL(error);
  if (error) {
    CFRelease(error);
  }
}

- (void)testCheckin {
  CFErrorRef error = NULL;
  // Can't check ourselves in
  NSDictionary *badTest
    = GTMCFAutorelease(GTMSMCopyJobCheckInDictionary(&error));
  XCTAssertNil(badTest);
  XCTAssertNotNULL(error);
  if (error) {
    CFRelease(error);
  }


  NSBundle *testBundle = [NSBundle bundleForClass:[self class]];
  XCTAssertNotNil(testBundle);
  NSString *testHarnessPath
    = [testBundle pathForResource:@"GTMServiceManagementTestingHarness"
                           ofType:nil];
  XCTAssertNotNil(testHarnessPath);
  NSDictionary *machServices
    = [NSDictionary dictionaryWithObjectsAndKeys:
        [NSNumber numberWithBool:YES], @GTM_MACH_PORT_NAME,
        nil];

  NSDictionary *socket
    = [NSDictionary dictionaryWithObjectsAndKeys:
       kGTMSocketKey,@LAUNCH_JOBSOCKETKEY_SECUREWITHKEY,
       nil];

  NSDictionary *sockets
    = [NSDictionary dictionaryWithObjectsAndKeys:
       socket, kGTMSocketName,
       nil];

  // LAUNCH_JOBKEY_WAITFORDEBUGGER left commented out
  // so that it can easily be reenabled for debugging.
  NSDictionary *job = [NSDictionary dictionaryWithObjectsAndKeys:
    @TEST_HARNESS_LABEL, @LAUNCH_JOBKEY_LABEL,
    testHarnessPath, @LAUNCH_JOBKEY_PROGRAM,
    [NSNumber numberWithBool:YES], @LAUNCH_JOBKEY_RUNATLOAD,
    [NSNumber numberWithBool:YES], @LAUNCH_JOBKEY_DEBUG,
    //[NSNumber numberWithBool:YES], @LAUNCH_JOBKEY_WAITFORDEBUGGER,
    machServices, @LAUNCH_JOBKEY_MACHSERVICES,
    sockets, @LAUNCH_JOBKEY_SOCKETS,
    nil];

  // This is allowed to fail.
  GTMSMJobRemove(CFSTR(TEST_HARNESS_LABEL), NULL);

  BOOL isGood = GTMSMJobSubmit((CFDictionaryRef)job, &error);
  XCTAssertTrue(isGood, @"Error %@", error);
}

@end

#pragma clang diagnostic pop

#endif //  if MAC_OS_X_VERSION_MIN_REQUIRED > MAC_OS_X_VERSION_10_4
