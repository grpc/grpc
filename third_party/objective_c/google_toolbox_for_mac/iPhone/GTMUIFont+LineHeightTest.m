//
//  GTMUIFont+LineHeightTest.m
//
//  Copyright 2008 Google Inc.
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
#import "GTMUIFont+LineHeight.h"

@interface GTMUIFontLineHeightTest : GTMTestCase
@end


@implementation GTMUIFontLineHeightTest

- (void)testLineHeight {
  UIFont *font = [UIFont systemFontOfSize:[UIFont systemFontSize]];
  XCTAssertNotNil(font);
  XCTAssertGreaterThanOrEqual([font gtm_lineHeight], (CGFloat)5.0);

  UIFont *fontSmall = [UIFont systemFontOfSize:[UIFont smallSystemFontSize]];
  XCTAssertNotNil(fontSmall);
  XCTAssertGreaterThanOrEqual([fontSmall gtm_lineHeight], (CGFloat)5.0);

  XCTAssertGreaterThan([font gtm_lineHeight], [fontSmall gtm_lineHeight]);
}

@end
