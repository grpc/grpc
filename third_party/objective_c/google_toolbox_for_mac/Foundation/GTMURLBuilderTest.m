//
//  GTMURLBuilderTest.m
//
//  Copyright 2012 Google Inc.
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
//  License for the specific language governing permissions and limitations
//  under the License.
//

#import "GTMURLBuilder.h"

#import "GTMSenTestCase.h"

@interface GTMURLBuilderTest : GTMTestCase
@end

@implementation GTMURLBuilderTest

- (void)testParseURL {
  GTMURLBuilder *URLBuilder = [[[GTMURLBuilder alloc]
      initWithString:@"http://google.com:8080/pathA/pathB?param=val"]
      autorelease];
  XCTAssertEqualStrings(@"http://google.com:8080/pathA/pathB?param=val",
                        [URLBuilder URLString]);
  XCTAssertEqualStrings(@"val", [URLBuilder valueForParameter:@"param"]);
  URLBuilder = [GTMURLBuilder builderWithString:
      @"http://google.com:8080/pathA/pathB?param=val"];
  XCTAssertEqualStrings(@"http://google.com:8080/pathA/pathB?param=val",
                        [URLBuilder URLString]);
  XCTAssertEqualStrings(@"val", [URLBuilder valueForParameter:@"param"]);

  URLBuilder = [GTMURLBuilder builderWithString:
                @"http://google.com:8080/path%3AA/pathB?param=val"];
  XCTAssertEqualStrings(@"http://google.com:8080/path%3AA/pathB?param=val",
                        [URLBuilder URLString]);
  XCTAssertEqualStrings(@"val", [URLBuilder valueForParameter:@"param"]);

  URLBuilder = [GTMURLBuilder builderWithString:
                @"http://google.com:8080/pathA/pathB%2F?param=val"];
  XCTAssertEqualStrings(@"http://google.com:8080/pathA/pathB%2F?param=val",
                        [URLBuilder URLString]);
  XCTAssertEqualStrings(@"val", [URLBuilder valueForParameter:@"param"]);
}

- (void)testMailToHandling {
  GTMURLBuilder *URLBuilder =
      [GTMURLBuilder builderWithString:@"mailto:ytmapp-ios@google.com"];
  [URLBuilder setValue:@"blah" forParameter:@"subject"];
  XCTAssertEqualStrings(@"mailto:ytmapp-ios@google.com?subject=blah",
                        [URLBuilder URLString]);
}

- (void)testIsEqualTo {
  GTMURLBuilder *URLBuilderA = [GTMURLBuilder
      builderWithString:@"http://google.com/pathA/pathB?a=b&c=d"];
  GTMURLBuilder *URLBuilderB =
      [GTMURLBuilder builderWithString:@"http://google.com/pathA/pathB"];
  [URLBuilderB setValue:@"d" forParameter:@"c"];
  [URLBuilderB setValue:@"b" forParameter:@"a"];
  XCTAssertTrue([URLBuilderA isEqual:URLBuilderB]);
  [URLBuilderB setValue:@"c" forParameter:@"a"];
  XCTAssertFalse([URLBuilderA isEqual:URLBuilderB]);
  [URLBuilderB setValue:@"b" forParameter:@"a"];
  [URLBuilderB setValue:@"f" forParameter:@"e"];
  XCTAssertFalse([URLBuilderA isEqual:URLBuilderB]);
}

- (void)testSetParameters {
  GTMURLBuilder *URLBuilderA =
      [GTMURLBuilder builderWithString:@"http://google.com/"];
  GTMURLBuilder *URLBuilderB =
      [GTMURLBuilder builderWithString:@"http://google.com/?p1=x&p2=b"];
  NSDictionary *params =
      [NSDictionary dictionaryWithObjectsAndKeys:@"a", @"p1", @"b", @"p2", nil];
  [URLBuilderA setParameters:params];
  [URLBuilderA setValue:@"x" forParameter:@"p1"];
  XCTAssertTrue([URLBuilderA isEqual:URLBuilderB]);
}

- (void)testReplaceParameters {
  GTMURLBuilder *URLBuilderA =
      [GTMURLBuilder builderWithString:@"http://google.com/?p1=y"];
  GTMURLBuilder *URLBuilderB =
      [GTMURLBuilder builderWithString:@"http://google.com/?p1=x&p2=b"];
  NSDictionary *params =
      [NSDictionary dictionaryWithObjectsAndKeys:@"a", @"p1", @"b", @"p2", nil];
  [URLBuilderA setParameters:params];
  [URLBuilderA setValue:@"x" forParameter:@"p1"];
  XCTAssertTrue([URLBuilderA isEqual:URLBuilderB]);
}

- (void)testURLPathParsing {
  GTMURLBuilder *URLBuilder =
      [GTMURLBuilder builderWithString:@"http://google.com/"];
  XCTAssertEqualStrings(@"http://google.com/", [URLBuilder URLString]);
  URLBuilder = [GTMURLBuilder builderWithString:@"http://google.com/pA/pB"];
  XCTAssertEqualStrings(@"http://google.com/pA/pB", [URLBuilder URLString]);
  URLBuilder = [GTMURLBuilder builderWithString:@"http://google.com/p%3AA/pB"];
  XCTAssertEqualStrings(@"http://google.com/p%3AA/pB", [URLBuilder URLString]);
}

@end
