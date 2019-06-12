//
//  GTMUILocalizerTest.m
//
//  Copyright 2011 Google Inc.
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

#import "GTMUILocalizerTest.h"
#import "GTMSenTestCase.h"

@interface TestUILocalizer : GTMUILocalizer
- (void)localize:(id)object;
@end

@implementation TestUILocalizer
- (NSString *)localizedStringForString:(NSString *)string {
  if ([string length] >= 5)
    return [string substringFromIndex:5];
  else
    return string;
}

- (void)localize:(id)object {
  [self localizeObject:object recursively:YES];
}
@end


@implementation GTMUILocalizerTestViewController

@synthesize label = label_;
@synthesize button = button_;
@synthesize segmentedControl = segmentedControl_;
@synthesize searchBar = searchBar_;

- (id)init {
  NSBundle *bundle = [NSBundle bundleForClass:[self class]];
  return [self initWithNibName:@"GTMUILocalizerTest" bundle:bundle];
}
@end

@interface GTMUILocalizerTest : GTMTestCase
- (void)checkValues:(NSString *)value
       onController:(GTMUILocalizerTestViewController *)controller;
@end

@implementation GTMUILocalizerTest
- (void)checkValues:(NSString *)value
       onController:(GTMUILocalizerTestViewController *)controller {
  // Label
  XCTAssertEqualStrings(value, [[controller label] text]);
  // Button
  UIControlState allStates[] = { UIControlStateNormal,
                                 UIControlStateHighlighted,
                                 UIControlStateDisabled,
                                 UIControlStateSelected };
  for (size_t idx = 0; idx < (sizeof(allStates)/sizeof(allStates[0])); ++idx) {
    UIControlState state = allStates[idx];
    XCTAssertEqualStrings(value, [[controller button] titleForState:state]);
  }
  // SegementedControl
  for (NSUInteger i = 0;
       i < [[controller segmentedControl] numberOfSegments];
       ++i) {
    XCTAssertEqualStrings(value,
        [[controller segmentedControl] titleForSegmentAtIndex:i]);
  }
  // SearchBar
  XCTAssertEqualStrings(value, [[controller searchBar] text]);
  XCTAssertEqualStrings(value, [[controller searchBar] placeholder]);
  XCTAssertEqualStrings(value, [[controller searchBar] prompt]);

// Accessibility label seems to not be working at all. They always are nil.
// Even when setting those explicitly there, the getter always returns nil.
// This might be because the gobal accessibility switch is not on during the
// tests.
#if 0
  XCTAssertEqualStrings(value, [[controller view] accessibilityLabel]);
  XCTAssertEqualStrings(value, [[controller view] accessibilityHint]);
  XCTAssertEqualStrings(value, [[controller label] accessibilityLabel]);
  XCTAssertEqualStrings(value, [[controller label] accessibilityHint]);
#endif
}

- (void)testLocalization {
  GTMUILocalizerTestViewController *controller =
    [[[GTMUILocalizerTestViewController alloc] init] autorelease];

  // Load the view.
  [controller view];

  [self checkValues:@"^IDS_FOO" onController:controller];

  TestUILocalizer *localizer = [[TestUILocalizer alloc] init];
  [localizer localize:[controller view]];

  [self checkValues:@"FOO" onController:controller];
}
@end
