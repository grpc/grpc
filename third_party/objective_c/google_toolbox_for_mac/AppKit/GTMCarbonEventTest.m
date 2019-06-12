//
//  GTMCarbonEventTest.m
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

#import "GTMSenTestCase.h"
#import "GTMCarbonEvent.h"
#import "GTMAppKitUnitTestingUtilities.h"

@interface GTMCarbonEventTest : GTMTestCase {
 @private
  GTMCarbonEvent *event_;
}
@end

@interface GTMCarbonEventHandlerTest : GTMTestCase {
 @private
  GTMCarbonEventHandler *handler_;
}
@end

@interface GTMCarbonEventMonitorHandlerTest : GTMTestCase
@end

@interface GTMCarbonEventApplicationEventHandlerTest : GTMTestCase
@end

@interface GTMCarbonEventDispatcherHandlerTest : GTMTestCase {
 @private
  XCTestExpectation *hotKeyHitExpectation_;
}
@end

static const UInt32 kTestClass = 'foo ';
static const UInt32 kTestKind = 'bar ';
static const UInt32 kTestParameterName = 'baz ';
static const UInt32 kTestBadParameterName = 'bom ';
static const UInt32 kTestParameterValue = 'bam ';

@implementation GTMCarbonEventTest
- (void)setUp {
  event_ = [[GTMCarbonEvent eventWithClass:kTestClass kind:kTestKind] retain];
}

- (void)tearDown {
  [event_ release];
}

- (void)testCopy {
  GTMCarbonEvent *event2 = [[event_ copy] autorelease];
  XCTAssertNotNil(event2);
}

- (void)testEventWithClassAndKind {
  XCTAssertEqual([event_ eventClass], kTestClass);
  XCTAssertEqual([event_ eventKind], kTestKind);
}

- (void)testEventWithEvent {
  GTMCarbonEvent *event2 = [GTMCarbonEvent eventWithEvent:[event_ event]];
  XCTAssertEqual([event2 event], [event_ event]);
}

- (void)testCurrentEvent {
  EventRef eventRef = GetCurrentEvent();
  GTMCarbonEvent *event = [GTMCarbonEvent currentEvent];
  XCTAssertEqual([event event], eventRef);
}

- (void)testEventClass {
  [self testEventWithClassAndKind];
}

- (void)testEventKind {
  [self testEventWithClassAndKind];
}

- (void)testSetTime {
  EventTime eventTime = [event_ time];
  XCTAssertNotEqualWithAccuracy(eventTime, kEventDurationNoWait, 0.01);
  XCTAssertNotEqualWithAccuracy(eventTime, kEventDurationForever, 0.01);
  [event_ setTime:kEventDurationForever];
  EventTime testTime = [event_ time];
  XCTAssertEqualWithAccuracy(testTime, kEventDurationForever, 0.01);
  [event_ setTime:eventTime];
  XCTAssertEqualWithAccuracy([event_ time], eventTime, 0.01);
}

- (void)testTime {
  [self testSetTime];
}

- (void)testEvent {
  [self testEventWithEvent];
}

- (void)testSetParameterNamed {
  UInt32 theData = kTestParameterValue;
  [event_ setUInt32ParameterNamed:kTestParameterName data:&theData];
  theData = 0;
  XCTAssertEqual([event_ sizeOfParameterNamed:kTestParameterName
                                         type:typeUInt32],
                  sizeof(UInt32));
  XCTAssertTrue([event_ getUInt32ParameterNamed:kTestParameterName
                                           data:&theData]);
  XCTAssertEqual(theData, kTestParameterValue);
}

- (void)testGetParameterNamed {
  [self testSetParameterNamed];
  UInt32 theData = kTestParameterValue;
  XCTAssertFalse([event_ getUInt32ParameterNamed:kTestBadParameterName
                                            data:&theData]);
  XCTAssertFalse([event_ getUInt32ParameterNamed:kTestBadParameterName
                                            data:NULL]);

}

- (void)testSizeOfParameterNamed {
  [self testSetParameterNamed];
}

- (void)testHasParameterNamed {
  [self testSetParameterNamed];
}

- (OSStatus)gtm_eventHandler:(GTMCarbonEventHandler *)sender
               receivedEvent:(GTMCarbonEvent *)event
                     handler:(EventHandlerCallRef)handler {
  OSStatus status = eventNotHandledErr;
  if ([event eventClass] == kTestClass && [event eventKind] == kTestKind) {
    status = noErr;
  }
  return status;
}

- (void)testSendToTarget {
  EventTypeSpec types = { kTestClass, kTestKind };
  GTMCarbonEventDispatcherHandler *handler
    = [[GTMCarbonEventDispatcherHandler sharedEventDispatcherHandler]
       autorelease];
  [handler registerForEvents:&types count:1];
  OSStatus status = [event_ sendToTarget:handler options:0];
  XCTAssertErr(status, eventNotHandledErr, @"status: %d", (int)status);
  [handler setDelegate:self];
  status = [event_ sendToTarget:handler options:0];
  XCTAssertNoErr(status, @"status: %d", (int)status);
  [handler unregisterForEvents:&types count:1];
}

- (void)testPostToQueue {
  EventQueueRef eventQueue = GetMainEventQueue();
  [event_ postToMainQueue];
  OSStatus status = [event_ postToQueue:eventQueue
                               priority:kEventPriorityStandard];
  XCTAssertErr(status, eventAlreadyPostedErr, @"status: %d", (int)status);
  EventTypeSpec types = { kTestClass, kTestKind };
  status = FlushEventsMatchingListFromQueue(eventQueue, 1, &types);
  XCTAssertNoErr(status, @"status: %ld", (long)status);

  eventQueue = GetCurrentEventQueue();
  [event_ postToCurrentQueue];
  status = [event_ postToQueue:eventQueue priority:kEventPriorityStandard];
  XCTAssertErr(status, eventAlreadyPostedErr, @"status: %d", (int)status);
  status = FlushEventsMatchingListFromQueue(eventQueue, 1, &types);
  XCTAssertNoErr(status, @"status: %ld", (long)status);
}

- (void)testPostToMainQueue {
  [self testPostToQueue];
}

- (void)testPostToCurrentQueue {
  XCTAssertEqual(GetCurrentEventQueue(), GetMainEventQueue());
  [self testPostToMainQueue];
}

- (void)testDescription {
  NSString *descString
    = [NSString stringWithFormat:@"GTMCarbonEvent 'foo ' %lu",
       (unsigned long)kTestKind];
  XCTAssertEqualObjects([event_ description], descString);
}
@end

@implementation GTMCarbonEventHandlerTest

- (void)setUp {
  handler_ = [[GTMCarbonEventHandler alloc] init];
}

- (void)tearDown {
  [handler_ release];
}

- (void)testEventTarget {
  XCTAssertNULL([handler_ eventTarget]);
}

- (void)testEventHandler {
  XCTAssertErr([handler_ handleEvent:nil handler:nil],
               (long)eventNotHandledErr);
}

- (void)testDelegate {
  [handler_ setDelegate:self];
  XCTAssertEqualObjects([handler_ delegate], self);
  [handler_ setDelegate:nil];
  XCTAssertNil([handler_ delegate]);
}


- (void)testSetDelegate {
  [self testDelegate];
}

@end

@implementation GTMCarbonEventMonitorHandlerTest

- (void)testEventHandler {
  GTMCarbonEventMonitorHandler *monitor
    = [GTMCarbonEventMonitorHandler sharedEventMonitorHandler];
  XCTAssertEqual([monitor eventTarget], GetEventMonitorTarget());
}

@end

#if (MAC_OS_X_VERSION_MAX_ALLOWED == MAC_OS_X_VERSION_10_5)
// Accidentally marked as !LP64 in the 10.5sdk, it's back in the 10.6 sdk.
// If you remove this decl, please remove it from GTMCarbonEvent.m as well.
extern EventTargetRef GetApplicationEventTarget(void);
#endif  // (MAC_OS_X_VERSION_MAX_ALLOWED == MAC_OS_X_VERSION_10_5)

@implementation GTMCarbonEventApplicationEventHandlerTest

- (void)testEventHandler {
  GTMCarbonEventApplicationEventHandler *handler
    = [GTMCarbonEventApplicationEventHandler sharedApplicationEventHandler];
  XCTAssertEqual([handler eventTarget], GetApplicationEventTarget());
}

@end

@implementation GTMCarbonEventDispatcherHandlerTest

- (void)testEventHandler {
  GTMCarbonEventDispatcherHandler *dispatcher
    = [GTMCarbonEventDispatcherHandler sharedEventDispatcherHandler];
  XCTAssertEqual([dispatcher eventTarget], GetEventDispatcherTarget());
}

- (void)hitHotKey:(GTMCarbonHotKey *)key {
  XCTAssertEqualObjects([key userInfo], self);
  [hotKeyHitExpectation_ fulfill];
}

- (void)hitExceptionalHotKey:(GTMCarbonHotKey *)key {
  XCTAssertEqualObjects([key userInfo], self);
  [hotKeyHitExpectation_ fulfill];
  [NSException raise:@"foo" format:@"bar"];
}

- (void)testRegisterHotKeyModifiersTargetActionWhenPressed {

  // This test can't be run if the screen saver is active because the security
  // agent blocks us from sending events via remote operations
  if (![GTMAppKitUnitTestingUtilities isScreenSaverActive]) {
    GTMCarbonEventDispatcherHandler *dispatcher
      = [GTMCarbonEventDispatcherHandler sharedEventDispatcherHandler];
    XCTAssertNotNil(dispatcher, @"Unable to acquire singleton");
    UInt32 keyMods = (NSShiftKeyMask | NSControlKeyMask
                      | NSAlternateKeyMask | NSCommandKeyMask);
    XCTAssertNil([dispatcher registerHotKey:0x5
                                  modifiers:keyMods
                                     target:nil
                                     action:nil
                                   userInfo:nil
                                whenPressed:YES],
                  @"Shouldn't have created hotkey");
    GTMCarbonHotKey *hotKey = [dispatcher registerHotKey:0x5
                                               modifiers:keyMods
                                                  target:self
                                                  action:@selector(hitHotKey:)
                                                userInfo:self
                                             whenPressed:YES];
    XCTAssertNotNil(hotKey, @"Unable to create hotkey");

    // Post the hotkey combo to the event queue. If everything is working
    // correctly hitHotKey: should get called, and hotKeyHitExpecatation_ will
    // be set for us. We run the event loop for a set amount of time waiting for
    // this to happen.
    hotKeyHitExpectation_ = [self expectationWithDescription:@"hotKey"];
    [GTMAppKitUnitTestingUtilities postTypeCharacterEvent:'g' modifiers:keyMods];
    [self waitForExpectationsWithTimeout:60 handler:NULL];
    [dispatcher unregisterHotKey:hotKey];
  }
}

- (void)testRegisterHotKeyModifiersTargetActionWhenPressedException {

  // This test can't be run if the screen saver is active because the security
  // agent blocks us from sending events via remote operations
  if (![GTMAppKitUnitTestingUtilities isScreenSaverActive]) {
    GTMCarbonEventDispatcherHandler *dispatcher
      = [GTMCarbonEventDispatcherHandler sharedEventDispatcherHandler];
    XCTAssertNotNil(dispatcher, @"Unable to acquire singleton");
    UInt32 keyMods = (NSShiftKeyMask | NSControlKeyMask
                      | NSAlternateKeyMask | NSCommandKeyMask);
    GTMCarbonHotKey *hotKey
      = [dispatcher registerHotKey:0x5
                         modifiers:keyMods
                            target:self
                            action:@selector(hitExceptionalHotKey:)
                          userInfo:self
                       whenPressed:YES];
    XCTAssertNotNil(hotKey, @"Unable to create hotkey");

    // Post the hotkey combo to the event queue. If everything is working
    // correctly hitHotKey: should get called, and hotKeyHitExpecatation_ will
    // be set for us. We run the event loop for a set amount of time waiting for
    // this to happen.
    hotKeyHitExpectation_ = [self expectationWithDescription:@"hotKey"];
    [GTMAppKitUnitTestingUtilities postTypeCharacterEvent:'g' modifiers:keyMods];
    [self waitForExpectationsWithTimeout:60 handler:NULL];
    [dispatcher unregisterHotKey:hotKey];
  }
}

- (void)testKeyModifiers {
  struct {
    NSUInteger cocoaKey_;
    UInt32 carbonKey_;
  } keyMap[] = {
    { NSAlphaShiftKeyMask, alphaLock},
    { NSShiftKeyMask, shiftKey},
    { NSControlKeyMask, controlKey},
    { NSAlternateKeyMask, optionKey},
    { NSCommandKeyMask, cmdKey},
  };
  size_t combos = pow(2, sizeof(keyMap) / sizeof(keyMap[0]));
  for (size_t i = 0; i < combos; i++) {
    NSUInteger cocoaMods = 0;
    UInt32 carbonMods = 0;
    for (size_t j = 0; j < 32 && j < sizeof(keyMap) / sizeof(keyMap[0]); j++) {
      if (i & 1 << j) {
        cocoaMods |= keyMap[j].cocoaKey_;
        carbonMods |= keyMap[j].carbonKey_;
      }
    }
    XCTAssertEqual(GTMCocoaToCarbonKeyModifiers(cocoaMods), carbonMods);
    XCTAssertEqual(GTMCarbonToCocoaKeyModifiers(carbonMods), cocoaMods);
  }
}

@end
