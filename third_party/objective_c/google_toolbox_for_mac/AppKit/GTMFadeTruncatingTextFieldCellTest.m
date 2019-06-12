//  GTMFadeTruncatingTextFieldCellTest.m
//
//  Copyright 2009 Google Inc.
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

#import "GTMDefines.h"

#if MAC_OS_X_VERSION_MIN_REQUIRED >= MAC_OS_X_VERSION_10_5

#import "GTMSenTestCase.h"
#import "GTMFadeTruncatingTextFieldCell.h"

@interface GTMFadeTruncatingTextFieldCellTest : GTMTestCase
@end

@implementation GTMFadeTruncatingTextFieldCellTest

- (void)testFadeCellRight {
  NSTextField *field = [[[NSTextField alloc] initWithFrame:
                         NSMakeRect(0, 0, 100, 16)] autorelease];
  [field setCell:[[[GTMFadeTruncatingTextFieldCell alloc] initTextCell:@""]
                  autorelease]];

  [field setStringValue:@"A very long string that won't fit"];
  [field setStringValue:@"A short string"];

  // Dark background, light text (force the background to draw (which is odd
  // for a text cell), but this is to make sure the support for light on dark
  // is tested.
  [field setTextColor:[NSColor whiteColor]];
  [field setDrawsBackground:YES];
  [field setBackgroundColor:[NSColor blackColor]];

  [field setStringValue:@"A very long string that won't fit"];
  [field setStringValue:@"A short string"];
}

- (void)testFadeCellLeftAndRight {
  NSTextField *field = [[[NSTextField alloc] initWithFrame:
                         NSMakeRect(0, 0, 100, 16)] autorelease];
  GTMFadeTruncatingTextFieldCell *cell =
      [[[GTMFadeTruncatingTextFieldCell alloc] initTextCell:@""] autorelease];
  [cell setTruncateMode:GTMFadeTruncatingHeadAndTail];
  [cell setDesiredCharactersToTruncateFromHead:5];
  [field setCell:cell];

  [field setStringValue:@"Fade on both left and right AAAA"];
  [field setStringValue:@"Fade on left only A"];
  [field setStringValue:@"A short string"];
  // Test the case where the number of characters to truncate from head is not
  // specified. This should cause the string to be drawn centered.
  [cell setDesiredCharactersToTruncateFromHead:0];
  [field setStringValue:@"Fade on both left and right AAAA"];
  // Border with a solid background color.
  [field setTextColor:[NSColor whiteColor]];
  [field setDrawsBackground:YES];
  [field setBackgroundColor:[NSColor blackColor]];
  [field setBordered:YES];
}

@end

#endif  // MAC_OS_X_VERSION_MIN_REQUIRED >= MAC_OS_X_VERSION_10_5
