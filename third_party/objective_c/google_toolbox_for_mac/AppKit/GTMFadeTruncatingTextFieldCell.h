//
//  GTMFadeTruncatingTextFieldCell.h
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

#import <Cocoa/Cocoa.h>


#if MAC_OS_X_VERSION_MIN_REQUIRED >= MAC_OS_X_VERSION_10_5

typedef enum {
  GTMFadeTruncatingTail,
  GTMFadeTruncatingHead,
  GTMFadeTruncatingHeadAndTail,
} GTMFadeTruncateMode;

// A simple text field cell that can truncate at the beginning or the end
// using a gradient. By default it truncates the end.
@interface GTMFadeTruncatingTextFieldCell : NSTextFieldCell {
 @private
  NSUInteger desiredCharactersToTruncateFromHead_;
  GTMFadeTruncateMode truncateMode_;
}

@property (nonatomic) GTMFadeTruncateMode truncateMode;

// When truncating the head this specifies the maximum number of characters
// that can be truncated. Setting this to 0 means that there is no maximum.
@property (nonatomic) NSUInteger desiredCharactersToTruncateFromHead;

@end

#endif
