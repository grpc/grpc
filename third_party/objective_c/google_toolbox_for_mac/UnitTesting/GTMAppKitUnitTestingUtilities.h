//
//  GTMAppKitUnitTestingUtilities.h
//
//  Copyright 2006-2010 Google Inc.
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

#import "GTMFoundationUnitTestingUtilities.h"

// Collection of utilities for unit testing
@interface GTMAppKitUnitTestingUtilities : NSObject

// Check if the screen saver is running. Some unit tests don't work when
// the screen saver is active.
+ (BOOL)isScreenSaverActive;

// Allows for posting either a keydown or a keyup with all the modifiers being
// applied. Passing a 'g' with NSKeyDown and NSShiftKeyMask
// generates two events (a shift key key down and a 'g' key keydown). Make sure
// to balance this with a keyup, or things could get confused. Events get posted
// using the CGRemoteOperation events which means that it gets posted in the
// system event queue. Thus you can affect other applications if your app isn't
// the active app (or in some cases, such as hotkeys, even if it is).
//  Arguments:
//    type - Event type. Currently accepts NSKeyDown and NSKeyUp
//    keyChar - character on the keyboard to type. Make sure it is lower case.
//              If you need upper case, pass in the NSShiftKeyMask in the
//              modifiers. i.e. to generate "G" pass in 'g' and NSShiftKeyMask.
//              to generate "+" pass in '=' and NSShiftKeyMask.
//    cocoaModifiers - an int made up of bit masks. Handles NSAlphaShiftKeyMask,
//                    NSShiftKeyMask, NSControlKeyMask, NSAlternateKeyMask, and
//                    NSCommandKeyMask
+ (void)postKeyEvent:(NSEventType)type
           character:(CGCharCode)keyChar
           modifiers:(UInt32)cocoaModifiers;

// Syntactic sugar for posting a keydown immediately followed by a key up event
// which is often what you really want.
//  Arguments:
//    keyChar - character on the keyboard to type. Make sure it is lower case.
//              If you need upper case, pass in the NSShiftKeyMask in the
//              modifiers. i.e. to generate "G" pass in 'g' and NSShiftKeyMask.
//              to generate "+" pass in '=' and NSShiftKeyMask.
//    cocoaModifiers - an int made up of bit masks. Handles NSAlphaShiftKeyMask,
//                    NSShiftKeyMask, NSControlKeyMask, NSAlternateKeyMask, and
//                    NSCommandKeyMask
+ (void)postTypeCharacterEvent:(CGCharCode)keyChar
                     modifiers:(UInt32)cocoaModifiers;

@end

// Some category methods to simplify spinning the runloops in such a way as
// to make tests less flaky, but have them complete as fast as possible.
@interface NSApplication (GTMUnitTestingRunAdditions)
// Has NSApplication call nextEventMatchingMask repeatedly until
// [context shouldStop] returns YES or it returns nil because the current date
// is greater than |date|.
// Return YES if the runloop was stopped because [context shouldStop] returned
// YES.
- (BOOL)gtm_runUntilDate:(NSDate *)date
                 context:(id<GTMUnitTestingRunLoopContext>)context
    NS_DEPRECATED(10_4, 10_8, 1_0, 7_0, "Please move to XCTestExpectations");

// Calls -gtm_runUntilDate:context: with the timeout date set to 60 seconds.
- (BOOL)gtm_runUpToSixtySecondsWithContext:(id<GTMUnitTestingRunLoopContext>)context
    NS_DEPRECATED(10_4, 10_8, 1_0, 7_0, "Please move to XCTestExpectations");
@end
