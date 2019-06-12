//
//  GTMNSAnimation+Duration.m
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

#import "GTMNSAnimation+Duration.h"

const NSUInteger kGTMLeftMouseUpAndKeyDownMask
  = NSLeftMouseUpMask | NSKeyDownMask;

NSTimeInterval GTMModifyDurationBasedOnCurrentState(NSTimeInterval duration,
                                                    NSUInteger eventMask) {
  NSEvent *currentEvent = [NSApp currentEvent];
  NSUInteger currentEventMask = NSEventMaskFromType([currentEvent type]);
  if (eventMask & currentEventMask) {
    NSUInteger modifiers = [currentEvent modifierFlags];
    if (!(modifiers & (NSAlternateKeyMask |
                       NSCommandKeyMask))) {
      if (modifiers & NSShiftKeyMask) {
        // 25 is the ascii code generated for a shift-tab (End-of-message)
        // The shift modifier is ignored if it is applied to a Tab key down/up.
        // Tab and shift-tab are often used for navigating around UI elements,
        // and in the majority of cases slowing down the animations while
        // navigating around UI elements is not desired.
        BOOL isShiftTab = (currentEventMask & (NSKeyDownMask | NSKeyUpMask))
          && !(modifiers & NSControlKeyMask)
          && ([[currentEvent characters] length] == 1)
          && ([[currentEvent characters] characterAtIndex:0] == 25);
        if (!isShiftTab) {
          duration *= 5.0;
        }
      }
      // These are additive, so shift+control returns 10 * duration.
      if (modifiers & NSControlKeyMask) {
        duration *= 2.0;
      }
    }
  }
  return duration;
}

@implementation NSAnimation (GTMNSAnimationDurationAdditions)

- (id)gtm_initWithDuration:(NSTimeInterval)duration
                 eventMask:(NSUInteger)eventMask
            animationCurve:(NSAnimationCurve)animationCurve {
  return [self initWithDuration:GTMModifyDurationBasedOnCurrentState(duration,
                                                                     eventMask)
                 animationCurve:animationCurve];
}

- (void)gtm_setDuration:(NSTimeInterval)duration
              eventMask:(NSUInteger)eventMask {
  [self setDuration:GTMModifyDurationBasedOnCurrentState(duration,
                                                         eventMask)];
}

@end

#if MAC_OS_X_VERSION_MIN_REQUIRED >= MAC_OS_X_VERSION_10_5

@implementation NSAnimationContext (GTMNSAnimationDurationAdditions)

- (void)gtm_setDuration:(NSTimeInterval)duration
              eventMask:(NSUInteger)eventMask {
  [self setDuration:GTMModifyDurationBasedOnCurrentState(duration,
                                                         eventMask)];
}

@end

@implementation CAAnimation (GTMCAAnimationDurationAdditions)

- (void)gtm_setDuration:(CFTimeInterval)duration
              eventMask:(NSUInteger)eventMask {
  [self setDuration:GTMModifyDurationBasedOnCurrentState(duration,
                                                         eventMask)];
}

@end

#endif  // MAC_OS_X_VERSION_MIN_REQUIRED >= MAC_OS_X_VERSION_10_5
