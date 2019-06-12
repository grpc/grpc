//
//  GTMSystemVersionTest.m
//
//  Copyright 2007-2008 Google Inc.
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

#import "GTMSenTestCase.h"
#import "GTMSystemVersion.h"

#pragma clang diagnostic push
// Ignore all of the deprecation warnings for GTMSystemVersion
#pragma clang diagnostic ignored "-Wdeprecated-declarations"

@interface GTMSystemVersionTest : GTMTestCase
@end

@implementation GTMSystemVersionTest
- (void)testBasics {
  SInt32 major;
  SInt32 minor;
  SInt32 bugFix;

  [GTMSystemVersion getMajor:NULL minor:NULL bugFix:NULL];
  [GTMSystemVersion getMajor:&major minor:NULL bugFix:NULL];
  [GTMSystemVersion getMajor:NULL minor:&minor bugFix:NULL];
  [GTMSystemVersion getMajor:NULL minor:NULL bugFix:&bugFix];
  [GTMSystemVersion getMajor:&major minor:&minor bugFix:&bugFix];
#if GTM_IPHONE_SDK
  XCTAssertTrue(major >= 2 && minor >= 0 && bugFix >= 0);
#else
  XCTAssertTrue(major >= 10 && minor >= 3 && bugFix >= 0);
  BOOL isPanther = (major == 10) && (minor == 3);
  BOOL isTiger = (major == 10) && (minor == 4);
  BOOL isLeopard = (major == 10) && (minor == 5);
  BOOL isSnowLeopard = (major == 10) && (minor == 6);

  BOOL isLater = (major > 10) || ((major == 10) && (minor > 6));
  XCTAssertEqual([GTMSystemVersion isPanther], isPanther);
  XCTAssertEqual([GTMSystemVersion isPantherOrGreater],
                 (BOOL)(isPanther || isTiger
                        || isLeopard || isSnowLeopard || isLater));
  XCTAssertEqual([GTMSystemVersion isTiger], isTiger);
  XCTAssertEqual([GTMSystemVersion isTigerOrGreater],
                 (BOOL)(isTiger || isLeopard || isSnowLeopard || isLater));
  XCTAssertEqual([GTMSystemVersion isLeopard], isLeopard);
  XCTAssertEqual([GTMSystemVersion isLeopardOrGreater],
                 (BOOL)(isLeopard || isSnowLeopard || isLater));
  XCTAssertEqual([GTMSystemVersion isSnowLeopard], isSnowLeopard);
  XCTAssertEqual([GTMSystemVersion isSnowLeopardOrGreater],
                 (BOOL)(isSnowLeopard || isLater));
#endif
}

- (void)testRuntimeArchitecture {
  // Not sure how to test this short of recoding it and verifying.
  // This at least executes the code for me.
  XCTAssertNotNil([GTMSystemVersion runtimeArchitecture]);
}

- (void)testBuild {
  // Not sure how to test this short of coding up a large fragile table.
  // This at least executes the code for me.
  NSString *systemVersion = [GTMSystemVersion build];
  XCTAssertNotEqual([systemVersion length], (NSUInteger)0);

  NSString *smallVersion = @"1A00";
  NSString *largeVersion = @"100Z100";
  XCTAssertTrue([GTMSystemVersion isBuildGreaterThan:smallVersion]);
  XCTAssertFalse([GTMSystemVersion isBuildGreaterThan:systemVersion]);
  XCTAssertFalse([GTMSystemVersion isBuildGreaterThan:largeVersion]);
  XCTAssertTrue([GTMSystemVersion isBuildGreaterThanOrEqualTo:smallVersion]);
  XCTAssertTrue([GTMSystemVersion isBuildGreaterThanOrEqualTo:systemVersion]);
  XCTAssertFalse([GTMSystemVersion isBuildGreaterThanOrEqualTo:largeVersion]);
  XCTAssertFalse([GTMSystemVersion isBuildEqualTo:smallVersion]);
  XCTAssertTrue([GTMSystemVersion isBuildEqualTo:systemVersion]);
  XCTAssertFalse([GTMSystemVersion isBuildEqualTo:largeVersion]);
  XCTAssertFalse([GTMSystemVersion isBuildLessThanOrEqualTo:smallVersion]);
  XCTAssertTrue([GTMSystemVersion isBuildLessThanOrEqualTo:systemVersion]);
  XCTAssertTrue([GTMSystemVersion isBuildLessThanOrEqualTo:largeVersion]);
  XCTAssertFalse([GTMSystemVersion isBuildLessThan:smallVersion]);
  XCTAssertFalse([GTMSystemVersion isBuildLessThan:systemVersion]);
  XCTAssertTrue([GTMSystemVersion isBuildLessThan:largeVersion]);

}

#if GTM_MACOS_SDK
- (void)testMacOSVersion {
  SInt32 major = -1;
  SInt32 minor = -1;
  SInt32 bugfix = -1;

  [GTMSystemVersion getMajor:&major minor:&minor bugFix:&bugfix];
  NSDictionary *versionPlistContents =
      [NSDictionary dictionaryWithContentsOfFile:
          @"/System/Library/CoreServices/SystemVersion.plist"];
  XCTAssertNotNil(versionPlistContents);
  NSString *version =
      [versionPlistContents objectForKey:@"ProductVersion"];
  XCTAssertNotNil(version);
  NSArray *pieces = [version componentsSeparatedByString:@"."];
  XCTAssertTrue([pieces count] > 2);
  XCTAssertEqual(major, (SInt32)[[pieces objectAtIndex:0] integerValue]);
  XCTAssertEqual(minor, [[pieces objectAtIndex:1] integerValue]);
  if ([pieces count] > 2) {
    XCTAssertEqual(bugfix, [[pieces objectAtIndex:2] integerValue]);
  } else {
    XCTAssertEqual(bugfix, 0, @"possible beta OS");
  }
}
#endif

@end

#pragma clang diagnostic pop
