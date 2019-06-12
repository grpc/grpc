//
//  GTMCarbonEvent.m
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

#import "GTMCarbonEvent.h"
#import <AppKit/AppKit.h>
#import "GTMDebugSelectorValidation.h"
#import "GTMTypeCasting.h"

// Wrapper for all the info we need about a hotkey that we can store in a
// Foundation storage class.
@interface GTMCarbonHotKey (GTMCarbonHotKeyPrivate)

// Create a HotKey record
//  Arguments:
//    keyID - id of the hotkey
//    target - object we are going to call when the hotkey is hit
//    action - selector we are going to call on target
//    userInfo - user storage
//    whenPressed - do we do it on key down or key up?
//  Returns:
//    a hotkey record, or nil on failure
- (id)initWithHotKey:(EventHotKeyID)keyID
              target:(id)target
              action:(SEL)selector
            userInfo:(id)userInfo
         whenPressed:(BOOL)onKeyDown;

// Does this record match key |keyID|
//  Arguments:
//    keyID - the id to match against
//  Returns:
//    Yes if we match this key id
- (BOOL)matchesHotKeyID:(EventHotKeyID)keyID;

// Make target perform selector
//  Returns:
//    Yes if handled
- (BOOL)sendAction;

- (void)setHotKeyRef:(EventHotKeyRef)ref;
@end

@implementation GTMCarbonEvent

//  Create an event of class |inClass| and kind |inKind|
//
//  Returns:
//    Autoreleased GTMCarbonEvent
//
+ (id)eventWithClass:(UInt32)inClass kind:(UInt32)kind {
  return [[[self alloc] initWithClass:inClass kind:kind] autorelease];
}


//  Create an event based on |event|. Retains |event|.
//
//  Returns:
//    Autoreleased GTMCarbonEvent
//
+ (id)eventWithEvent:(EventRef)event {
  return [[[self alloc] initWithEvent:event] autorelease];
}


//  Create an event based on the event currently being handled.
//
//  Returns:
//    Autoreleased GTMCarbonEvent
//
+ (id)currentEvent {
  return [self eventWithEvent:GetCurrentEvent()];
}


//  Create an event of class |inClass| and kind |inKind|
//
//  Returns:
//    GTMCarbonEvent
//
- (id)initWithClass:(UInt32)inClass kind:(UInt32)kind {
  if ((self = [super init])) {
    __Verify_noErr(CreateEvent(kCFAllocatorDefault, inClass, kind,
                               0, kEventAttributeNone, &event_));
  }
  return self;
}


//  Create an event based on |event|. Retains |event|.
//
//  Returns:
//    GTMCarbonEvent
//
- (id)initWithEvent:(EventRef)event {
  if ((self = [super init])) {
    if (event) {
      event_ = RetainEvent(event);
    }
  }
  return self;
}


// This does a proper event copy, but ignores the |zone|. No way to do a copy
// of an event into a specific zone.
//
//  Arguments:
//    zone - the zone to copy to
//  Returns:
//    the copied event. nil on failure
- (id)copyWithZone:(NSZone *)zone {
  GTMCarbonEvent *carbonEvent = nil;
  EventRef newEvent = CopyEvent([self event]);
  if (newEvent) {
    carbonEvent = [[[self class] allocWithZone:zone] initWithEvent:newEvent];
    ReleaseEvent(newEvent);
  }
  return carbonEvent;
}

// releases our retained event
//
- (void)dealloc {
  if (event_) {
    ReleaseEvent(event_);
    event_ = NULL;
  }
  [super dealloc];
}

// description utliity for debugging
//
- (NSString *)description {
  // Use 8 bytes because stack protection gives us a warning if we use a
  // smaller buffer.
  char cls[8];
  UInt32 kind;

  // Need everything bigendian if we are printing out the class as a "string"
  *((UInt32 *)cls) = CFSwapInt32HostToBig([self eventClass]);
  kind = [self eventKind];
  cls[4] = 0;
  return [NSString stringWithFormat:@"GTMCarbonEvent '%s' %lu",
          cls, (unsigned long)kind];
}


//  Get the event's class.
//
//  Returns:
//    event class
//
- (UInt32)eventClass {
  return GetEventClass(event_);
}


//  Get the event's kind.
//
//  Returns:
//    event kind
//
- (UInt32)eventKind {
  return GetEventKind(event_);
}


//  Set the event's time.
//
//  Arguments:
//    time - the time you want associated with the event
//
- (void)setTime:(EventTime)eventTime {
  __Verify_noErr(SetEventTime(event_, eventTime));
}


//  Get the event's time.
//
//  Returns:
//    the time associated with the event
//
- (EventTime)time {
  return GetEventTime(event_);
}


//  Get the event's eventref for passing to other carbon functions.
//
//  Returns:
//    the event ref associated with the event
//
- (EventRef)event {
  return event_;
}


//  Sends event to an event target with options
//
//  Arguments:
//    target - target to send event to.
//    options - options to send event. See SendEventToEventTargetWithOptions
//              for details.
//
//  Returns:
//    OSStatus value.
//
- (OSStatus)sendToTarget:(GTMCarbonEventHandler *)target
                 options:(OptionBits)options {
  return SendEventToEventTargetWithOptions(event_,
                                           [target eventTarget], options);
}

//  Post event to an event queue.
//
//  Arguments:
//    queue - queue to post it to.
//    priority - priority to post it with
//
//  Returns:
//    OSStatus value.
//
- (OSStatus)postToQueue:(EventQueueRef)queue priority:(EventPriority)priority {
  return PostEventToQueue(queue, event_, priority);
}


//  Post event to current queue with standard priority.
//
- (void)postToCurrentQueue {
  __Verify_noErr([self postToQueue:GetCurrentEventQueue()
                          priority:kEventPriorityStandard]);
}


//  Post event to main queue with standard priority.
//
- (void)postToMainQueue {
  __Verify_noErr([self postToQueue:GetMainEventQueue()
                          priority:kEventPriorityStandard]);
}


//  Sets (or adds) a parameter to an event. Try not to use this function
//  directly. Look at the PARAM_TEMPLATE_DECL/DEFN macros below.
//
//  Arguments:
//    name - the parameter name.
//    type - the parameter type.
//    size - the size of the data that |data| points to.
//    data - pointer to the data you want to set the parameter to.
//
- (void)setParameterNamed:(EventParamName)name
                     type:(EventParamType)type
                     size:(ByteCount)size
                     data:(const void *)data {
  __Verify_noErr(SetEventParameter(event_, name, type, size, data));
}


//  Gets a parameter from an event. Try not to use this function
//  directly. Look at the PARAM_TEMPLATE_DECL/DEFN macros below.
//
//  Arguments:
//    name - the parameter name.
//    type - the parameter type.
//    size - the size of the data that |data| points to.
//    data - pointer to the buffer that you want to fill with your data.
//
//  Returns:
//    YES is parameter is retrieved successfully. NO if parameter doesn't exist.
//
- (BOOL)getParameterNamed:(EventParamName)name
                     type:(EventParamType)type
                     size:(ByteCount)size
                     data:(void *)data {
  OSStatus status = GetEventParameter(event_, name, type,
                                      NULL, size, NULL, data);
  return status == noErr;
}


//  Gets a the size of a parameter from an event.
//
//  Arguments:
//    name - the parameter name.
//    type - the parameter type.
//
//  Returns:
//    The size of the buffer required to hold the parameter. 0 if parameter
//    doesn't exist.
//
- (ByteCount)sizeOfParameterNamed:(EventParamName)name
                             type:(EventParamType)type {
  ByteCount size = 0;
  __Verify_noErr(GetEventParameter(event_, name, type, NULL, 0, &size, NULL));
  return size;
}

@end

@implementation GTMCarbonEvent (GTMCarbonEventGettersAndSetters)
GTM_PARAM_TEMPLATE_DEFN(UInt32)
GTM_PARAM_TEMPLATE_DEFN(EventHotKeyID)
@end

UInt32 GTMCocoaToCarbonKeyModifiers(NSUInteger inCocoaModifiers) {
  UInt32 carbModifiers = 0;
  if (inCocoaModifiers & NSAlphaShiftKeyMask) carbModifiers |= alphaLock;
  if (inCocoaModifiers & NSShiftKeyMask) carbModifiers |= shiftKey;
  if (inCocoaModifiers & NSControlKeyMask) carbModifiers |= controlKey;
  if (inCocoaModifiers & NSAlternateKeyMask) carbModifiers |= optionKey;
  if (inCocoaModifiers & NSCommandKeyMask) carbModifiers |= cmdKey;
  return carbModifiers;
}

NSUInteger GTMCarbonToCocoaKeyModifiers(UInt32 inCarbonModifiers) {
  NSUInteger nsModifiers = 0;
  if (inCarbonModifiers & alphaLock) nsModifiers |= NSAlphaShiftKeyMask;
  if (inCarbonModifiers & shiftKey) nsModifiers |= NSShiftKeyMask;
  if (inCarbonModifiers & controlKey) nsModifiers |= NSControlKeyMask;
  if (inCarbonModifiers & optionKey) nsModifiers |= NSAlternateKeyMask;
  if (inCarbonModifiers & cmdKey) nsModifiers |= NSCommandKeyMask;
  return nsModifiers;
}

const OSType kGTMCarbonFrameworkSignature = 'GTM ';

@implementation GTMCarbonEventHandler

-(void)dealloc {
  if (eventHandler_) {
    __Verify_noErr(RemoveEventHandler(eventHandler_));
  }
  [super dealloc];
}
// Does our delegate respond to eventHandler:receivedEvent:handler:
//
// Returns:
//  YES if delegate responds to eventHandler:receivedEvent:handler:
- (BOOL) delegateRespondsToHandleEvent {
  return delegateRespondsToHandleEvent_;
}

// Registers the event handler to listen for |events|.
//
// Arguments:
//   events - an array of EventTypeSpec. The events to register for.
//   count - the number of EventTypeSpecs in events.
//
- (void)registerForEvents:(const EventTypeSpec *)events count:(size_t)count {
  __Verify_noErr(AddEventTypesToHandler([self eventHandler], count, events));
}

// Causes the event handler to stop listening for |events|.
//
// Arguments:
//   events - an array of EventTypeSpec. The events to register for.
//   count - the number of EventTypeSpecs in events.
//
- (void)unregisterForEvents:(const EventTypeSpec *)events count:(size_t)count {
  __Verify_noErr(
      RemoveEventTypesFromHandler([self eventHandler], count, events));
}

// To be overridden by subclasses to respond to events. All subclasses should
// call [super handleEvent:handler:] if they don't handle the event themselves.
//
// Arguments:
//   event - the event to be handled
//   handler - the call ref in case you want to call CallNextEventHandler
//             in your method
// Returns:
//   OSStatus - usually either noErr or eventNotHandledErr
//
- (OSStatus)handleEvent:(GTMCarbonEvent *)event
                handler:(EventHandlerCallRef)handler {
  OSStatus status = eventNotHandledErr;
  __Require(event, CantUseParams);
  __Require(handler, CantUseParams);
  __Require([event event], CantUseParams);
  status = CallNextEventHandler(handler, [event event]);
CantUseParams:
  return status;
}

// To be overridden by subclasses to return the event target for the class.
// GTMCarbonEventHandler's implementation returns NULL.
//
// Returns:
//   The event target ref.
//
- (EventTargetRef)eventTarget {
  // Defaults implementation needs to be overridden
  return NULL;
}

// C callback for our registered EventHandlerUPP.
//
// Arguments:
//  inHandler - handler given to us from Carbon Event system
//  inEvent - the event we are handling
//  inUserData - refcon that we gave to the carbon event system. Is a
//               GTMCarbonEventHandler in disguise.
// Returns:
//  status of event handler
//
static OSStatus EventHandler(EventHandlerCallRef inHandler,
                             EventRef inEvent,
                             void *inUserData) {
  GTMCarbonEvent *event = [GTMCarbonEvent eventWithEvent:inEvent];
  GTMCarbonEventHandler *handler
    = GTM_STATIC_CAST(GTMCarbonEventHandler, inUserData);

  // First check to see if our delegate cares about this event. If the delegate
  // handles it (i.e responds to it and does not return eventNotHandledErr) we
  // do not pass it on to default handling.
  OSStatus status = eventNotHandledErr;
  if ([handler delegateRespondsToHandleEvent]) {
    status = [[handler delegate] gtm_eventHandler:handler
                                    receivedEvent:event
                                          handler:inHandler];
  }
  if (status == eventNotHandledErr) {
    status = [handler handleEvent:event handler:inHandler];
  }
  return status;
}

// Gets the underlying EventHandlerRef for that this class wraps.
//
// Returns:
//   The EventHandlerRef this class wraps.
//
- (EventHandlerRef)eventHandler {
  if (!eventHandler_) {
    static EventHandlerUPP sHandlerProc = NULL;
    if ( sHandlerProc == NULL ) {
      sHandlerProc = NewEventHandlerUPP(EventHandler);
    }
    __Verify_noErr(InstallEventHandler([self eventTarget],
                                       sHandlerProc, 0,
                                       NULL, self, &eventHandler_));
  }
  return eventHandler_;
}

// Gets the delegate for the handler
//
// Returns:
//   the delegate
- (id)delegate {
  return delegate_;
}

// Sets the delegate for the handler and caches whether it responds to
// the eventHandler:receivedEvent:handler: selector for performance purposes.
//
// Arguments:
//   delegate - the delegate for the handler
- (void)setDelegate:(id)delegate {
  delegate_ = delegate;
  SEL selector = @selector(gtm_eventHandler:receivedEvent:handler:);
  delegateRespondsToHandleEvent_ = [delegate respondsToSelector:selector];
}

@end

@implementation GTMCarbonEventMonitorHandler

+ (GTMCarbonEventMonitorHandler *)sharedEventMonitorHandler {
  static GTMCarbonEventMonitorHandler *obj = nil;
  if (!obj) {
    obj = [[self alloc] init];
  }
  return obj;
}

- (EventTargetRef)eventTarget {
  return GetEventMonitorTarget();
}

@end

#if (MAC_OS_X_VERSION_MAX_ALLOWED == MAC_OS_X_VERSION_10_5)
// Accidentally marked as !LP64 in the 10.5sdk, it's back in the 10.6 sdk.
// If you remove this decl, please remove it from GTMCarbonEventTest.m as well.
extern EventTargetRef GetApplicationEventTarget(void);
#endif  // (MAC_OS_X_VERSION_MAX_ALLOWED == MAC_OS_X_VERSION_10_5)

@implementation GTMCarbonEventApplicationEventHandler

+ (GTMCarbonEventApplicationEventHandler *)sharedApplicationEventHandler {
  static GTMCarbonEventApplicationEventHandler *obj = nil;
  if (!obj) {
    obj = [[self alloc] init];
  }
  return obj;
}

- (EventTargetRef)eventTarget {
  return GetApplicationEventTarget();
}

@end

@implementation GTMCarbonEventDispatcherHandler

+ (GTMCarbonEventDispatcherHandler *)sharedEventDispatcherHandler {
  static GTMCarbonEventDispatcherHandler *obj = nil;
  if (!obj) {
    obj = [[self alloc] init];
  }
  return obj;
}

// Register for the events we handle, and set up the dictionaries we need
// to keep track of the hotkeys and commands that we handle.
//  Returns:
//    GTMCarbonApplication or nil on failure
- (id)init {
  if ((self = [super init])) {
    static EventTypeSpec events[] = {
      { kEventClassKeyboard, kEventHotKeyPressed },
      { kEventClassKeyboard, kEventHotKeyReleased },
    };
    [self registerForEvents:events count:GetEventTypeCount(events)];
    hotkeys_ = [[NSMutableArray alloc] initWithCapacity:0];
  }
  return self;
}

// COV_NF_START
// Singleton, we never get released. Just here for completeness.
- (void)dealloc {
  [hotkeys_ release];
  [super dealloc];
}
// COV_NF_END

- (EventTargetRef)eventTarget {
  return GetEventDispatcherTarget();
}

// Registers a hotkey. When the hotkey is executed by the user, target will be
// called with selector.
//  Arguments:
//    keyCode - the virtual keycode of the hotkey
//    cocoaModifiers - the modifiers that need to be used with |keyCode|. NB
//                     that these are cocoa modifiers, so NSCommandKeyMask etc.
//    target - instance that will get |action| called when the hotkey fires
//    action - the method to call on |target| when the hotkey fires
//             action should have the signature - (void)handler:(GTMCarbonEventDispatcherHandler *)handler
//    userInfo - user storage
//    onKeyDown - is YES, the hotkey fires on the keydown (usual) otherwise
//              it fires on the key up.
//  Returns:
//    a EventHotKeyRef that you can use with other Carbon functions, or for
//    unregistering the hotkey. Note that all hotkeys are unregistered
//    automatically when an app quits. Will be NULL on failure.
- (GTMCarbonHotKey *)registerHotKey:(NSUInteger)keyCode
                          modifiers:(NSUInteger)cocoaModifiers
                             target:(id)target
                             action:(SEL)selector
                           userInfo:(id)userInfo
                        whenPressed:(BOOL)onKeyDown {
  static UInt32 sCurrentID = 0;

  GTMCarbonHotKey *newKey = nil;
  EventHotKeyRef theRef = NULL;
  EventHotKeyID keyID;
  keyID.signature = kGTMCarbonFrameworkSignature;
  keyID.id = ++sCurrentID;
  newKey = [[[GTMCarbonHotKey alloc] initWithHotKey:keyID
                                             target:target
                                             action:selector
                                           userInfo:userInfo
                                        whenPressed:onKeyDown] autorelease];
  __Require(newKey, CantCreateKey);
  __Require_noErr_Action(RegisterEventHotKey((UInt32)keyCode,
                                             GTMCocoaToCarbonKeyModifiers(cocoaModifiers),
                                             keyID,
                                             [self eventTarget],
                                             0,
                                             &theRef),
                         CantRegisterHotKey, newKey = nil);
  [newKey setHotKeyRef:theRef];
  [hotkeys_ addObject:newKey];

CantRegisterHotKey:
CantCreateKey:
  return newKey;
}

// Unregisters a hotkey previously registered with registerHotKey.
//  Arguments:
//    keyRef - the EventHotKeyRef to unregister
- (void)unregisterHotKey:(GTMCarbonHotKey *)keyRef {
  __Check([hotkeys_ containsObject:keyRef]);
  [[keyRef retain] autorelease];
  [hotkeys_ removeObject:keyRef];
  __Verify_noErr(UnregisterEventHotKey([keyRef hotKeyRef]));
}

// A hotkey has been hit. See if it is one of ours, and if so fire it.
//  Arguments:
//    event - the hotkey even that was received
//  Returns:
//    Yes if handled.
- (BOOL)handleHotKeyEvent:(GTMCarbonEvent *)event {
  EventHotKeyID keyID;
  BOOL handled = [event getEventHotKeyIDParameterNamed:kEventParamDirectObject
                                                  data:&keyID];
  if (handled) {
    GTMCarbonHotKey *hotkey;
    for (hotkey in hotkeys_) {
      if ([hotkey matchesHotKeyID:keyID]) {
        EventKind kind = [event eventKind];
        BOOL onKeyDown = [hotkey onKeyDown];
        if ((kind == kEventHotKeyPressed && onKeyDown) ||
            (kind == kEventHotKeyReleased && !onKeyDown)) {
          handled = [hotkey sendAction];
        }
        break;
      }
    }
  }
  return handled;
}

// Currently we handle hotkey and command events here. If we get one of them
// we dispatch them off to the handlers above. Otherwise we just call up to
// super.
//  Arguments:
//    event - the event to check
//    handler - the handler call ref
//  Returns:
//    OSStatus
- (OSStatus)handleEvent:(GTMCarbonEvent *)event
                handler:(EventHandlerCallRef)handler {
  OSStatus theStatus = eventNotHandledErr;
  if ([event eventClass] == kEventClassKeyboard) {
    EventKind kind = [event eventKind];
    if (kind == kEventHotKeyPressed || kind == kEventHotKeyReleased) {
      theStatus = [self handleHotKeyEvent:event] ? noErr : eventNotHandledErr;
    }
  }
  // We didn't handle it, maybe somebody upstairs will.
  if (theStatus == eventNotHandledErr) {
    theStatus = [super handleEvent:event handler:handler];
  }
  return theStatus;
}

@end

@implementation GTMCarbonHotKey

// Init a HotKey record. In debug version make sure that the selector we are
// passed matches what we expect. (
//  Arguments:
//    keyID - id of the hotkey
//    reference - hotkey reference
//    target - object we are going to call when the hotkey is hit
//    action - selector we are going to call on target
//    userinfo - info for user
//    whenPressed - do we do it on key down or key up?
//  Returns:
//    a hotkey record, or nil on failure
- (id)initWithHotKey:(EventHotKeyID)keyID
              target:(id)target
              action:(SEL)selector
            userInfo:(id)userInfo
         whenPressed:(BOOL)onKeyDown {
  if ((self = [super init])) {
    if(!target || !selector) {
      [self release];
      return nil;
    }
    id_ = keyID;
    target_ = [target retain];
    userInfo_ = [userInfo retain];
    selector_ = selector;
    onKeyDown_ = onKeyDown;
    GTMAssertSelectorNilOrImplementedWithReturnTypeAndArguments(target,
                                                                selector,
                                                                @encode(void),
                                                                @encode(id),
                                                                NULL);
  }
  return self;
}

- (void)dealloc {
  [target_ release];
  [userInfo_ release];
  [super dealloc];
}

- (NSUInteger)hash {
  return (NSUInteger)hotKeyRef_;
}

- (BOOL)isEqual:(id)object {
  return [object isMemberOfClass:[self class]]
    && (hotKeyRef_ == [object hotKeyRef]);
}

// Does this record match key |keyID|
//  Arguments:
//    keyID - the id to match against
//  Returns:
//    Yes if we match this key id
- (BOOL)matchesHotKeyID:(EventHotKeyID)keyID {
  return (id_.signature == keyID.signature) && (id_.id == keyID.id);
}

- (BOOL)sendAction {
  BOOL handled = NO;
  @try {
    [target_ performSelector:selector_ withObject:self];
    handled = YES;
  }
  @catch (NSException * e) {
    handled = NO;
    _GTMDevLog(@"Exception fired in hotkey: %@ (%@)", [e name], [e reason]);
  }  // COV_NF_LINE
  return handled;
}

- (BOOL)onKeyDown {
  return onKeyDown_;
}

- (id)userInfo {
  return userInfo_;
}

- (EventHotKeyRef)hotKeyRef {
  return hotKeyRef_;
}

- (void)setHotKeyRef:(EventHotKeyRef)ref {
  hotKeyRef_ = ref;
}

- (NSString *)description {
  return [NSString stringWithFormat:@"<%@ %p> - ref %p signature %lu id %lu",
          [self class], self, hotKeyRef_,
          (unsigned long)id_.signature, (unsigned long)id_.id];
}

@end



