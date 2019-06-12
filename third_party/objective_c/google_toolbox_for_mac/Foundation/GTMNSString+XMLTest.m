//
//  GTMNSString+XMLTest.m
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
#import "GTMNSString+XML.h"


@interface GTMNSString_XMLTest : GTMTestCase
@end

@implementation GTMNSString_XMLTest

- (void)testStringBySanitizingAndEscapingForXML {
  // test the substitutions cases
  UniChar chars[] = {
    'z', 0, 'z', 1, 'z', 4, 'z', 5, 'z', 34, 'z', 38, 'z', 39, 'z',
    60, 'z', 62, 'z', ' ', 'z', 0xd800, 'z', 0xDFFF, 'z', 0xE000,
    'z', 0xFFFE, 'z', 0xFFFF, 'z', '\n', 'z', '\r', 'z', '\t', 'z' };

  NSString *string1 = [NSString stringWithCharacters:chars
                                              length:sizeof(chars) / sizeof(UniChar)];
  NSString *string2 =
   [NSString stringWithFormat:@"zzzzz&quot;z&amp;z&apos;z&lt;z&gt;z zzz%Czzz\nz\rz\tz",
    (unsigned short)0xE000];

  XCTAssertEqualObjects([string1 gtm_stringBySanitizingAndEscapingForXML],
                        string2,
                        @"Sanitize and Escape for XML failed");

  // force the backing store of the NSString to test extraction paths
  char ascBuffer[] = "a\01bcde\nf";
  NSString *ascString =
    [[[NSString alloc] initWithBytesNoCopy:ascBuffer
                                    length:sizeof(ascBuffer) / sizeof(char)
                                  encoding:NSASCIIStringEncoding
                              freeWhenDone:NO] autorelease];
  XCTAssertEqualObjects([ascString gtm_stringBySanitizingAndEscapingForXML],
                        @"abcde\nf",
                        @"Sanitize and Escape for XML from asc buffer failed");

  // test empty string
  XCTAssertEqualObjects([@"" gtm_stringBySanitizingAndEscapingForXML], @"");
}

- (void)testStringBySanitizingToXMLSpec {
  // test the substitutions cases
  UniChar chars[] = {
    'z', 0, 'z', 1, 'z', 4, 'z', 5, 'z', 34, 'z', 38, 'z', 39, 'z',
    60, 'z', 62, 'z', ' ', 'z', 0xd800, 'z', 0xDFFF, 'z', 0xE000,
    'z', 0xFFFE, 'z', 0xFFFF, 'z', '\n', 'z', '\r', 'z', '\t', 'z' };

  NSString *string1 = [NSString stringWithCharacters:chars
                                              length:sizeof(chars) / sizeof(UniChar)];
  NSString *string2 =
    [NSString stringWithFormat:@"zzzzz\"z&z'z<z>z zzz%Czzz\nz\rz\tz",
     (unsigned short)0xE000];

  XCTAssertEqualObjects([string1 gtm_stringBySanitizingToXMLSpec],
                        string2,
                        @"Sanitize for XML failed");

  // force the backing store of the NSString to test extraction paths
  char ascBuffer[] = "a\01bcde\nf";
  NSString *ascString =
  [[[NSString alloc] initWithBytesNoCopy:ascBuffer
                                  length:sizeof(ascBuffer) / sizeof(char)
                                encoding:NSASCIIStringEncoding
                            freeWhenDone:NO] autorelease];
  XCTAssertEqualObjects([ascString gtm_stringBySanitizingToXMLSpec],
                        @"abcde\nf",
                        @"Sanitize and Escape for XML from asc buffer failed");

  // test empty string
  XCTAssertEqualObjects([@"" gtm_stringBySanitizingToXMLSpec], @"");
}

@end
