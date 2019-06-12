//
//  GTMKeyValueAnimation.h
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

#import <AppKit/AppKit.h>

// Simple class for doing key value path animation on a target.
// The key value path of target will be set to the currentValue
// (not the currentProgress) of the animation.
// Defaults to NSAnimationNonblocking as opposed to NSAnimationBlocking
// because in our experience most use cases don't want blocking animations.
// KeyPath of target must represent a CGFloat value.
@interface GTMKeyValueAnimation : NSAnimation {
 @private
  id target_;
  NSString *keyPath_;
}

- (id)initWithTarget:(id)target keyPath:(NSString*)keyPath;
- (id)target;
- (NSString *)keyPath;

@end
