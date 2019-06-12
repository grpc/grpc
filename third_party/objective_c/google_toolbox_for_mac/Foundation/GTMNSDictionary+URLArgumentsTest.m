//
//  GTMNSDictionary+URLArgumentsTest.m
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
#import "GTMNSDictionary+URLArguments.h"
#import "GTMDefines.h"

#pragma clang diagnostic push
// Ignore all of the deprecation warnings for GTMNSDictionary+URLArguments
#pragma clang diagnostic ignored "-Wdeprecated-declarations"

@interface GTMNSDictionary_URLArgumentsTest : GTMTestCase
@end

@implementation GTMNSDictionary_URLArgumentsTest

- (void)testFromArgumentsString {
  XCTAssertEqualObjects([NSDictionary gtm_dictionaryWithHttpArgumentsString:@""],
                        [NSDictionary dictionary],
                        @"- empty arguments string should give an empty dictionary");
  XCTAssertEqualObjects([NSDictionary gtm_dictionaryWithHttpArgumentsString:@"a"],
                        [NSDictionary dictionaryWithObject:@"" forKey:@"a"],
                        @"- missing '=' should result in an empty string value");
  XCTAssertEqualObjects([NSDictionary gtm_dictionaryWithHttpArgumentsString:@"a="],
                        [NSDictionary dictionaryWithObject:@"" forKey:@"a"],
                        @"- no value");
  XCTAssertEqualObjects([NSDictionary gtm_dictionaryWithHttpArgumentsString:@"&a=1"],
                        [NSDictionary dictionaryWithObject:@"1" forKey:@"a"],
                        @"- empty segment should be skipped");
  XCTAssertEqualObjects([NSDictionary gtm_dictionaryWithHttpArgumentsString:@"abc=123"],
                        [NSDictionary dictionaryWithObject:@"123" forKey:@"abc"],
                        @"- simple one-pair dictionary should work");
  XCTAssertEqualObjects([NSDictionary gtm_dictionaryWithHttpArgumentsString:@"a=1&a=2&a=3"],
                        [NSDictionary dictionaryWithObject:@"1" forKey:@"a"],
                        @"- only first occurrence of a key is returned");
  NSString* complex = @"a%2Bb=specialkey&complex=1%2B1%21%3D3%20%26%202%2A6%2F3%3D4&c";
  NSDictionary* result = [NSDictionary dictionaryWithObjectsAndKeys:
                          @"1+1!=3 & 2*6/3=4", @"complex",
                                @"specialkey", @"a+b",
                                          @"", @"c",
                                               nil];
  XCTAssertEqualObjects([NSDictionary gtm_dictionaryWithHttpArgumentsString:complex],
                        result,
                        @"- keys and values should be unescaped correctly");
  XCTAssertEqualObjects([NSDictionary gtm_dictionaryWithHttpArgumentsString:@"a=%FC"],
                        [NSDictionary dictionaryWithObject:@"" forKey:@"a"],
                        @"- invalid UTF8 characters result in an empty value, not a crash");
}

- (void)testArgumentsString {
  XCTAssertEqualObjects([[NSDictionary dictionary] gtm_httpArgumentsString], @"",
                        @"- empty dictionary should give an empty string");
  XCTAssertEqualObjects([[NSDictionary dictionaryWithObject:@"123" forKey:@"abc"] gtm_httpArgumentsString],
                        @"abc=123",
                        @"- simple one-pair dictionary should work");
  NSDictionary* arguments = [NSDictionary dictionaryWithObjectsAndKeys:
                             @"1+1!=3 & 2*6/3=4", @"complex",
                                   @"specialkey", @"a+b",
                                                  nil];
  NSString* argumentString = [arguments gtm_httpArgumentsString];
  // check for individual pieces since order is not guaranteed
  NSString* component1 = @"a%2Bb=specialkey";
  NSString* component2 = @"complex=1%2B1%21%3D3%20%26%202%2A6%2F3%3D4";
  XCTAssertNotEqual([argumentString rangeOfString:component1].location, (NSUInteger)NSNotFound,
                    @"- '%@' not found in '%@'", component1, argumentString);
  XCTAssertNotEqual([argumentString rangeOfString:component2].location, (NSUInteger)NSNotFound,
                    @"- '%@' not found in '%@'", component2, argumentString);
  XCTAssertNotEqual([argumentString rangeOfString:@"&"].location, (NSUInteger)NSNotFound,
                    @"- special characters should be escaped");
  XCTAssertNotEqual([argumentString characterAtIndex:0], (unichar)'&',
                    @"- there should be no & at the beginning of the string");
  XCTAssertNotEqual([argumentString characterAtIndex:([argumentString length] - 1)], (unichar)'&',
                    @"- there should be no & at the end of the string");
}

@end

#pragma clang diagnostic pop
