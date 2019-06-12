//
//  GTMNSString+URLArgumentsTest.m
//
//  Copyright 2006-2008 Google Inc.
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
#import "GTMNSString+URLArguments.h"

#pragma clang diagnostic push
// Ignore all of the deprecation warnings for GTMNSString+URLArguments
#pragma clang diagnostic ignored "-Wdeprecated-declarations"

@interface GTMNSString_URLArgumentsTest : GTMTestCase
@end

@implementation GTMNSString_URLArgumentsTest


- (void)testEscaping {
  // should be done already by the basic code
  XCTAssertEqualObjects([@"this that" gtm_stringByEscapingForURLArgument], @"this%20that", @"- space should be escaped");
  XCTAssertEqualObjects([@"this\"that" gtm_stringByEscapingForURLArgument], @"this%22that", @"- double quote should be escaped");
  // make sure our additions are handled
  XCTAssertEqualObjects([@"this!that" gtm_stringByEscapingForURLArgument], @"this%21that", @"- exclamation mark should be escaped");
  XCTAssertEqualObjects([@"this*that" gtm_stringByEscapingForURLArgument], @"this%2Athat", @"- asterisk should be escaped");
  XCTAssertEqualObjects([@"this'that" gtm_stringByEscapingForURLArgument], @"this%27that", @"- single quote should be escaped");
  XCTAssertEqualObjects([@"this(that" gtm_stringByEscapingForURLArgument], @"this%28that", @"- left paren should be escaped");
  XCTAssertEqualObjects([@"this)that" gtm_stringByEscapingForURLArgument], @"this%29that", @"- right paren should be escaped");
  XCTAssertEqualObjects([@"this;that" gtm_stringByEscapingForURLArgument], @"this%3Bthat", @"- semi-colon should be escaped");
  XCTAssertEqualObjects([@"this:that" gtm_stringByEscapingForURLArgument], @"this%3Athat", @"- colon should be escaped");
  XCTAssertEqualObjects([@"this@that" gtm_stringByEscapingForURLArgument], @"this%40that", @"- at sign should be escaped");
  XCTAssertEqualObjects([@"this&that" gtm_stringByEscapingForURLArgument], @"this%26that", @"- ampersand should be escaped");
  XCTAssertEqualObjects([@"this=that" gtm_stringByEscapingForURLArgument], @"this%3Dthat", @"- equals should be escaped");
  XCTAssertEqualObjects([@"this+that" gtm_stringByEscapingForURLArgument], @"this%2Bthat", @"- plus should be escaped");
  XCTAssertEqualObjects([@"this$that" gtm_stringByEscapingForURLArgument], @"this%24that", @"- dollar-sign should be escaped");
  XCTAssertEqualObjects([@"this,that" gtm_stringByEscapingForURLArgument], @"this%2Cthat", @"- comma should be escaped");
  XCTAssertEqualObjects([@"this/that" gtm_stringByEscapingForURLArgument], @"this%2Fthat", @"- slash should be escaped");
  XCTAssertEqualObjects([@"this?that" gtm_stringByEscapingForURLArgument], @"this%3Fthat", @"- question mark should be escaped");
  XCTAssertEqualObjects([@"this%that" gtm_stringByEscapingForURLArgument], @"this%25that", @"- percent should be escaped");
  XCTAssertEqualObjects([@"this#that" gtm_stringByEscapingForURLArgument], @"this%23that", @"- pound should be escaped");
  XCTAssertEqualObjects([@"this[that" gtm_stringByEscapingForURLArgument], @"this%5Bthat", @"- left bracket should be escaped");
  XCTAssertEqualObjects([@"this]that" gtm_stringByEscapingForURLArgument], @"this%5Dthat", @"- right bracket should be escaped");
  // make sure plus and space are handled in the right order
  XCTAssertEqualObjects([@"this that+the other" gtm_stringByEscapingForURLArgument], @"this%20that%2Bthe%20other", @"- pluses and spaces should be different");
  // high char test
  NSString *tester = [NSString stringWithUTF8String:"caf\xC3\xA9"];
  XCTAssertNotNil(tester, @"failed to create from utf8 run");
  XCTAssertEqualObjects([tester gtm_stringByEscapingForURLArgument], @"caf%C3%A9", @"- high chars should work");
}

- (void)testUnescaping {
  // should be done already by the basic code
  XCTAssertEqualObjects([@"this%20that" gtm_stringByUnescapingFromURLArgument], @"this that", @"- space should be unescaped");
  XCTAssertEqualObjects([@"this%22that" gtm_stringByUnescapingFromURLArgument], @"this\"that", @"- double quote should be unescaped");
  // make sure our additions are handled
  XCTAssertEqualObjects([@"this%21that" gtm_stringByUnescapingFromURLArgument], @"this!that", @"- exclamation mark should be unescaped");
  XCTAssertEqualObjects([@"this%2Athat" gtm_stringByUnescapingFromURLArgument], @"this*that", @"- asterisk should be unescaped");
  XCTAssertEqualObjects([@"this%27that" gtm_stringByUnescapingFromURLArgument], @"this'that", @"- single quote should be unescaped");
  XCTAssertEqualObjects([@"this%28that" gtm_stringByUnescapingFromURLArgument], @"this(that", @"- left paren should be unescaped");
  XCTAssertEqualObjects([@"this%29that" gtm_stringByUnescapingFromURLArgument], @"this)that", @"- right paren should be unescaped");
  XCTAssertEqualObjects([@"this%3Bthat" gtm_stringByUnescapingFromURLArgument], @"this;that", @"- semi-colon should be unescaped");
  XCTAssertEqualObjects([@"this%3Athat" gtm_stringByUnescapingFromURLArgument], @"this:that", @"- colon should be unescaped");
  XCTAssertEqualObjects([@"this%40that" gtm_stringByUnescapingFromURLArgument], @"this@that", @"- at sign should be unescaped");
  XCTAssertEqualObjects([@"this%26that" gtm_stringByUnescapingFromURLArgument], @"this&that", @"- ampersand should be unescaped");
  XCTAssertEqualObjects([@"this%3Dthat" gtm_stringByUnescapingFromURLArgument], @"this=that", @"- equals should be unescaped");
  XCTAssertEqualObjects([@"this%2Bthat" gtm_stringByUnescapingFromURLArgument], @"this+that", @"- plus should be unescaped");
  XCTAssertEqualObjects([@"this%24that" gtm_stringByUnescapingFromURLArgument], @"this$that", @"- dollar-sign should be unescaped");
  XCTAssertEqualObjects([@"this%2Cthat" gtm_stringByUnescapingFromURLArgument], @"this,that", @"- comma should be unescaped");
  XCTAssertEqualObjects([@"this%2Fthat" gtm_stringByUnescapingFromURLArgument], @"this/that", @"- slash should be unescaped");
  XCTAssertEqualObjects([@"this%3Fthat" gtm_stringByUnescapingFromURLArgument], @"this?that", @"- question mark should be unescaped");
  XCTAssertEqualObjects([@"this%25that" gtm_stringByUnescapingFromURLArgument], @"this%that", @"- percent should be unescaped");
  XCTAssertEqualObjects([@"this%23that" gtm_stringByUnescapingFromURLArgument], @"this#that", @"- pound should be unescaped");
  XCTAssertEqualObjects([@"this%5Bthat" gtm_stringByUnescapingFromURLArgument], @"this[that", @"- left bracket should be unescaped");
  XCTAssertEqualObjects([@"this%5Dthat" gtm_stringByUnescapingFromURLArgument], @"this]that", @"- right bracket should be unescaped");
  // make sure a plus come back out as a space
  XCTAssertEqualObjects([@"this+that" gtm_stringByUnescapingFromURLArgument], @"this that", @"- plus should be unescaped");
  // make sure plus and %2B are handled in the right order
  XCTAssertEqualObjects([@"this+that%2Bthe%20other" gtm_stringByUnescapingFromURLArgument], @"this that+the other", @"- pluses and spaces should be different");
  // high char test
  NSString *tester = [NSString stringWithUTF8String:"caf\xC3\xA9"];
  XCTAssertNotNil(tester, @"failed to create from utf8 run");
  XCTAssertEqualObjects([@"caf%C3%A9" gtm_stringByUnescapingFromURLArgument], tester, @"- high chars should work");
}

@end

#pragma clang diagnostic pop
