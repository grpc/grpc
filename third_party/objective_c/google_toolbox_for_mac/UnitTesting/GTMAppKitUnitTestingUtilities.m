//
//  GTMAppKitUnitTestingUtilities.m
//
//  Copyright 2006-2008 Google Inc.
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

#import "GTMAppKitUnitTestingUtilities.h"
#import "GTMDefines.h"

static CGKeyCode GTMKeyCodeForCharCode(CGCharCode charCode);

@implementation GTMAppKitUnitTestingUtilities

+ (BOOL)isScreenSaverActive {
  BOOL answer = NO;
  ProcessSerialNumber psn;
  if (GetFrontProcess(&psn) == noErr) {
    CFDictionaryRef cfProcessInfo
      = ProcessInformationCopyDictionary(&psn,
                                         kProcessDictionaryIncludeAllInformationMask);
    NSDictionary *processInfo = GTMCFAutorelease(cfProcessInfo);

    NSString *bundlePath = [processInfo objectForKey:@"BundlePath"];
    // ScreenSaverEngine is the frontmost app if the screen saver is actually
    // running Security Agent is the frontmost app if the "enter password"
    // dialog is showing
    NSString *bundleName = [bundlePath lastPathComponent];
    answer = ([bundleName isEqualToString:@"ScreenSaverEngine.app"]
              || [bundleName isEqualToString:@"SecurityAgent.app"]);
  }
  return answer;
}

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
           modifiers:(UInt32)cocoaModifiers {
  __Require(![self isScreenSaverActive], CantWorkWithScreenSaver);
  __Require(type == NSKeyDown || type == NSKeyUp, CantDoEvent);
  CGKeyCode code = GTMKeyCodeForCharCode(keyChar);
  __Verify(code != 256);
  CGEventRef event = CGEventCreateKeyboardEvent(NULL, code, type == NSKeyDown);
  __Require(event, CantCreateEvent);
  CGEventSetFlags(event, cocoaModifiers);
  CGEventPost(kCGSessionEventTap, event);
  CFRelease(event);
CantCreateEvent:
CantDoEvent:
CantWorkWithScreenSaver:
  return;
}

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
+ (void)postTypeCharacterEvent:(CGCharCode)keyChar modifiers:(UInt32)cocoaModifiers {
  [self postKeyEvent:NSKeyDown character:keyChar modifiers:cocoaModifiers];
  [self postKeyEvent:NSKeyUp character:keyChar modifiers:cocoaModifiers];
}

@end

// Returns a virtual key code for a given charCode. Handles all of the
// NS*FunctionKeys as well.
static CGKeyCode GTMKeyCodeForCharCode(CGCharCode charCode) {
  // character map taken from http://classicteck.com/rbarticles/mackeyboard.php
  int characters[] = {
    'a', 's', 'd', 'f', 'h', 'g', 'z', 'x', 'c', 'v', 256, 'b', 'q', 'w',
    'e', 'r', 'y', 't', '1', '2', '3', '4', '6', '5', '=', '9', '7', '-',
    '8', '0', ']', 'o', 'u', '[', 'i', 'p', '\n', 'l', 'j', '\'', 'k', ';',
    '\\', ',', '/', 'n', 'm', '.', '\t', ' ', '`', '\b', 256, '\e'
  };

  // function key map taken from
  // file:///Developer/ADC%20Reference%20Library/documentation/Cocoa/Reference/ApplicationKit/ObjC_classic/Classes/NSEvent.html
  int functionKeys[] = {
    // NSUpArrowFunctionKey - NSF12FunctionKey
    126, 125, 123, 124, 122, 120, 99, 118, 96, 97, 98, 100, 101, 109, 103, 111,
    // NSF13FunctionKey - NSF28FunctionKey
    105, 107, 113, 256, 256, 256, 256, 256, 256, 256, 256, 256, 256, 256, 256, 256,
    // NSF29FunctionKey - NSScrollLockFunctionKey
    256, 256, 256, 256, 256, 256, 256, 256, 117, 115, 256, 119, 116, 121, 256, 256,
    // NSPauseFunctionKey - NSPrevFunctionKey
    256, 256, 256, 256, 256, 256, 256, 256, 256, 256, 256, 256, 256, 256, 256, 256,
    // NSNextFunctionKey - NSModeSwitchFunctionKey
    256, 256, 256, 256, 256, 256, 114, 1
  };

  CGKeyCode outCode = 0;

  // Look in the function keys
  if (charCode >= NSUpArrowFunctionKey && charCode <= NSModeSwitchFunctionKey) {
    outCode = functionKeys[charCode - NSUpArrowFunctionKey];
  } else {
    // Look in our character map
    for (size_t i = 0; i < (sizeof(characters) / sizeof (int)); i++) {
      if (characters[i] == charCode) {
        outCode = i;
        break;
      }
    }
  }
  return outCode;
}

@implementation NSApplication (GTMUnitTestingRunAdditions)

- (BOOL)gtm_runUntilDate:(NSDate *)date
                 context:(id<GTMUnitTestingRunLoopContext>)context {
  BOOL contextShouldStop = NO;
  while (1) {
    contextShouldStop = [context shouldStop];
    if (contextShouldStop) break;
    NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];
    NSEvent *event = [NSApp nextEventMatchingMask:NSAnyEventMask
                                         untilDate:date
                                            inMode:NSDefaultRunLoopMode
                                           dequeue:YES];
    if (!event) {
      [pool drain];
      break;
    }
    [NSApp sendEvent:event];
    [pool drain];
  }
  return contextShouldStop;
}

- (BOOL)gtm_runUpToSixtySecondsWithContext:(id<GTMUnitTestingRunLoopContext>)context {
  return [self gtm_runUntilDate:[NSDate dateWithTimeIntervalSinceNow:60]
                        context:context];
}

@end
