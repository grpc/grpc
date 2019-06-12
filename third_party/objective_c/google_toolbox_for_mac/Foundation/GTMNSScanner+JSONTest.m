//
//  GTMNSScanner+JSONTest.m
//
//  Copyright 2005-2008 Google Inc.
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
#import "GTMNSScanner+JSON.h"

#pragma clang diagnostic push
// Ignore all of the deprecation warnings for GTMNSScanner+JSON
#pragma clang diagnostic ignored "-Wdeprecated-declarations"

@interface GTMNSScanner_JSONTest : GTMTestCase
@end

struct {
  NSString *testString_;
  NSString *resultString_;
  BOOL isObject_;
} testStrings[] = {
  { @"", nil, NO },
  { @"\"Empty String\"", nil, NO },
  { @"[\"Unclosed array\"", nil, NO },
  { @"[\"escape at end of unfinished string\\", nil, NO },
  { @"junk, [\"Unclosed array with junk before\"", nil, NO },
  { @"\"Unopened array\"]", nil, NO },
  { @"\"Unopened array with junk after\"] junk", nil, NO },
  { @"[\"array\"]", @"[\"array\"]", NO },
  { @"junk [\"array with junk\"]", @"[\"array with junk\"]", NO },
  { @"[\"array with junk\"], junk", @"[\"array with junk\"]", NO },
  { @"[[[\"nested array\"]]]", @"[[[\"nested array\"]]]", NO },
  { @"[[[\"badly nested array\"]]", nil, NO },
  { @"[[[\"over nested array\"]]]]", @"[[[\"over nested array\"]]]", NO },
  { @"[{]", @"[{]", NO },
  { @"[\"closer in quotes\":\"]\"]", @"[\"closer in quotes\":\"]\"]", NO },
  { @"[\"escaped closer\":\\]]", @"[\"escaped closer\":\\]", NO },
  { @"[\"double escape\":\\\\]", @"[\"double escape\":\\\\]", NO },
  { @"[\"doub esc quote\":\"\\\"]\"]", @"[\"doub esc quote\":\"\\\"]\"]", NO },
  { @"[\"opener in quotes\":\"[\"]", @"[\"opener in quotes\":\"[\"]", NO },
  { @"[\"escaped opener\":\\[]", nil, NO },
  { @"[\"escaped opener\":\\[]]", @"[\"escaped opener\":\\[]]", NO },
  { @"[\"doub esc quote\":\"\\\"[\"]", @"[\"doub esc quote\":\"\\\"[\"]", NO },
  { @"{\"Unclosed object\"", nil, YES },
  { @"junk, {\"Unclosed object with junk before\"", nil, YES },
  { @"\"Unopened object\"}", nil, YES },
  { @"\"Unopened object with junk after\"} junk", nil, YES },
  { @"{\"object\"}", @"{\"object\"}", YES },
  { @"junk, {\"object with junk\"}", @"{\"object with junk\"}", YES },
  { @"{\"object with junk\"}, junk", @"{\"object with junk\"}", YES },
  { @"{{{\"nested object\"}}}", @"{{{\"nested object\"}}}", YES },
  { @"{{{\"badly nested object\"}}", nil, YES },
  { @"{{{\"over nested object\"}}}}", @"{{{\"over nested object\"}}}", YES },
  { @"{[}", @"{[}", YES },
  { @"{\"closer in quotes\":\"}\"}", @"{\"closer in quotes\":\"}\"}", YES },
  { @"{\"escaped closer\":\\}}", @"{\"escaped closer\":\\}", YES },
  { @"{\"double escape\":\\\\}", @"{\"double escape\":\\\\}", YES },
  { @"{\"doub esc quote\":\"\\\"}\"}", @"{\"doub esc quote\":\"\\\"}\"}", YES },
  { @"{\"opener in quotes\":\"{\"}", @"{\"opener in quotes\":\"{\"}", YES },
  { @"{\"escaped opener\":\\{}", nil, YES },
  { @"{\"escaped opener\":\\{}}", @"{\"escaped opener\":\\{}}", YES },
  { @"{\"doub esc quote\":\"\\\"{\"}", @"{\"doub esc quote\":\"\\\"{\"}", YES },
};

@implementation GTMNSScanner_JSONTest

- (void)testJSONObject {
  NSCharacterSet *set = [[NSCharacterSet illegalCharacterSet] invertedSet];
  for (size_t i = 0; i < sizeof(testStrings) / sizeof(testStrings[0]); ++i) {
    NSScanner *scanner
      = [NSScanner scannerWithString:testStrings[i].testString_];
    [scanner setCharactersToBeSkipped:set];
    NSString *array = nil;
    BOOL goodArray = [scanner gtm_scanJSONArrayString:&array];
    scanner = [NSScanner scannerWithString:testStrings[i].testString_];
    [scanner setCharactersToBeSkipped:set];
    NSString *object = nil;
    BOOL goodObject = [scanner gtm_scanJSONObjectString:&object];
    if (testStrings[i].resultString_) {
      if (testStrings[i].isObject_) {
        XCTAssertEqualStrings(testStrings[i].resultString_,
                              object, @"Test String: %@",
                              testStrings[i].testString_);
        XCTAssertNil(array, @"Test String: %@", testStrings[i].testString_);
        XCTAssertTrue(goodObject, @"Test String: %@",
                      testStrings[i].testString_);
        XCTAssertFalse(goodArray, @"Test String: %@",
                       testStrings[i].testString_);
      } else {
        XCTAssertEqualStrings(testStrings[i].resultString_, array,
                              @"Test String: %@", testStrings[i].testString_);
        XCTAssertNil(object, @"Test String: %@", testStrings[i].testString_);
        XCTAssertTrue(goodArray, @"Test String: %@", testStrings[i].testString_);
        XCTAssertFalse(goodObject, @"Test String: %@",
                       testStrings[i].testString_);
      }
    } else {
      XCTAssertNil(object, @"Test String: %@", testStrings[i].testString_);
      XCTAssertNil(array, @"Test String: %@", testStrings[i].testString_);
      XCTAssertFalse(goodArray, @"Test String: %@", testStrings[i].testString_);
      XCTAssertFalse(goodObject, @"Test String: %@", testStrings[i].testString_);
    }
  }
}

- (void)testScanningCharacters {
  NSCharacterSet *alphaSet = [NSCharacterSet alphanumericCharacterSet];
  NSString *testString = @"asdfasdf[]:,";
  NSScanner *scanner = [NSScanner scannerWithString:testString];
  [scanner setCharactersToBeSkipped:alphaSet];
  NSString *array = nil;
  XCTAssertTrue([scanner gtm_scanJSONArrayString:&array]);
  XCTAssertEqualStrings(array, @"[]");
  NSString *nextValue = nil;
  XCTAssertTrue([scanner scanString:@":," intoString:&nextValue]);
  XCTAssertEqualStrings(@":,", nextValue);
  scanner = [NSScanner scannerWithString:testString];
  XCTAssertFalse([scanner gtm_scanJSONArrayString:&array]);
}

@end

#pragma clang diagnostic pop
