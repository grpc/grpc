//
//  GTMFadeTruncatingLabelTest.m
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
//
#import "GTMSenTestCase.h"
#import "GTMFadeTruncatingLabel.h"

@interface GTMFadeTruncatingLabelTest : GTMTestCase
@end


@implementation GTMFadeTruncatingLabelTest

- (void)testFadeTruncatingLabelRight {
  GTMFadeTruncatingLabel* label = [[[GTMFadeTruncatingLabel alloc]
                                    initWithFrame:CGRectMake(0, 0, 200, 25)]
                                   autorelease];
  label.text = @"A very long string that won't fit";
  label.text = @"A short string";

  // Dark background, light text.
  label.backgroundColor = [UIColor blackColor];
  [label setTextColor:[UIColor whiteColor]];

  label.text = @"A very long string that won't fit";
  label.text = @"A short string";
}

- (void)testFadeTruncatingLabelLeftAndRight {
  GTMFadeTruncatingLabel* label = [[[GTMFadeTruncatingLabel alloc]
                                    initWithFrame:CGRectMake(0, 0, 200, 25)]
                                   autorelease];
  label.truncateMode = GTMFadeTruncatingHeadAndTail;

  label.text = @"Fade on both left and right";
}

@end
