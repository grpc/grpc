//
//  GTMCarbonEvent.h
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

#import <Foundation/Foundation.h>
#import <Carbon/Carbon.h>

#import "GTMDefines.h"

@class GTMCarbonEventHandler;
@class GTMCarbonHotKey;

// Objective C wrapper for a Carbon Event
@interface GTMCarbonEvent : NSObject <NSCopying> {
 @private
  EventRef event_;  //Event we are wrapping. STRONG
}


//  Create an event of class |inClass| and kind |inKind|
//
//  Returns:
//    Autoreleased GTMCarbonEvent
//
+ (id)eventWithClass:(UInt32)inClass kind:(UInt32)kind;

//  Create an event based on |event|. Retains |event|.
//
//  Returns:
//    Autoreleased GTMCarbonEvent
//
+ (id)eventWithEvent:(EventRef)event;

//  Create an event based on the event currently being handled.
//
//  Returns:
//    Autoreleased GTMCarbonEvent
//
+ (id)currentEvent;

//  Create an event of class |inClass| and kind |inKind|
//
//  Returns:
//    GTMCarbonEvent
//
- (id)initWithClass:(UInt32)inClass kind:(UInt32)kind;

//  Create an event based on |event|. Retains |event|.
//
//  Returns:
//    GTMCarbonEvent
//
- (id)initWithEvent:(EventRef)event;

//  Get the event's class.
//
//  Returns:
//    event class
//
- (UInt32)eventClass;

//  Get the event's kind.
//
//  Returns:
//    event kind
//
- (UInt32)eventKind;

//  Set the event's time.
//
//  Arguments:
//    time - the time you want associated with the event
//
- (void)setTime:(EventTime)eventTime;

//  Get the event's time.
//
//  Returns:
//    the time associated with the event
//
- (EventTime)time;

//  Get the event's eventref for passing to other carbon functions.
//
//  Returns:
//    the event ref associated with the event
//
- (EventRef)event;

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
                     data:(const void *)data;


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
                     data:(void *)data;

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
                             type:(EventParamType)type;

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
                 options:(OptionBits)options;

//  Post event to an event queue.
//
//  Arguments:
//    queue - queue to post it to.
//    priority - priority to post it with
//
//  Returns:
//    OSStatus value.
//
- (OSStatus)postToQueue:(EventQueueRef)queue priority:(EventPriority)priority;

//  Post event to current queue with standard priority.
//
- (void)postToCurrentQueue;

//  Post event to main queue with standard priority.
//
- (void)postToMainQueue;

@end

// Macros for defining simple set/get parameter methods for GTMCarbonEvent. See
// the category GTMCarbonEvent (GTMCarbonEventGettersAndSetters) for an example
// of their use. GTM_PARAM_TEMPLATE_DECL2/DEFN2 is for the case where the
// parameter name is different than the parameter type (rare, but it does
// occur...e.g. for a Rect, the name is typeQDRectangle, and the type is Rect,
// so it would be GTM_PARAM_TEMPLATE_DECL2(QDRectangle, Rect) ). In most cases
// you will just use GTM_PARAM_TEMPLATE_DECL/DEFN.
#define GTM_PARAM_TEMPLATE_DECL2(paramName, paramType) \
- (void)set##paramName##ParameterNamed:(EventParamName)name data:(paramType *)data; \
- (BOOL)get##paramName##ParameterNamed:(EventParamName)name data:(paramType *)data;

#define GTM_PARAM_TEMPLATE_DEFN2(paramName, paramType) \
- (void)set##paramName##ParameterNamed:(EventParamName)name data:(paramType *)data { \
[self setParameterNamed:name type:type##paramName size:sizeof(paramType) data:data]; \
} \
- (BOOL)get##paramName##ParameterNamed:(EventParamName)name data:(paramType *)data { \
return [self getParameterNamed:name type:type##paramName size:sizeof(paramType) data:data]; \
}

#define GTM_PARAM_TEMPLATE_DECL(paramType) GTM_PARAM_TEMPLATE_DECL2(paramType, paramType)
#define GTM_PARAM_TEMPLATE_DEFN(paramType) GTM_PARAM_TEMPLATE_DEFN2(paramType, paramType)


// Category defining some basic types that we want to be able to easily set and
// get from GTMCarbonEvents
@interface GTMCarbonEvent (GTMCarbonEventGettersAndSetters)
GTM_PARAM_TEMPLATE_DECL(UInt32)
GTM_PARAM_TEMPLATE_DECL(EventHotKeyID)
@end

//  Utility function for converting between modifier types
//  Arguments:
//    inCocoaModifiers - keyboard modifiers in carbon form
//                       (NSCommandKeyMask etc)
//  Returns:
//    Carbon modifiers equivalent to |inCocoaModifiers| (cmdKey etc)
GTM_EXTERN UInt32 GTMCocoaToCarbonKeyModifiers(NSUInteger inCocoaModifiers);

// Utility function for converting between modifier types
//  Arguments:
//    inCarbonModifiers - keyboard modifiers in carbon form (cmdKey etc)
//  Returns:
//    cocoa modifiers equivalent to |inCocoaModifiers| (NSCommandKeyMask etc)
GTM_EXTERN NSUInteger GTMCarbonToCocoaKeyModifiers(UInt32 inCarbonModifiers);

// An "abstract" superclass for objects that handle events such as
// menus, HIObjects, etc.
//
// Subclasses are expected to override the eventTarget and
// handleEvent:handler: methods to customize them.
@interface GTMCarbonEventHandler : NSObject {
 @private
  // handler we are wrapping
  // lazily created in the eventHandler method
  EventHandlerRef eventHandler_;
  GTM_WEAK id delegate_;  // Our delegate
  // Does our delegate respond to the gtm_eventHandler:receivedEvent:handler:
  // selector? Cached for performance reasons.
  BOOL delegateRespondsToHandleEvent_;
}

// Registers the event handler to listen for |events|.
//
// Arguments:
//   events - an array of EventTypeSpec. The events to register for.
//   count - the number of EventTypeSpecs in events.
//
- (void)registerForEvents:(const EventTypeSpec *)events count:(size_t)count;

// Causes the event handler to stop listening for |events|.
//
// Arguments:
//   events - an array of EventTypeSpec. The events to register for.
//   count - the number of EventTypeSpecs in events.
//
- (void)unregisterForEvents:(const EventTypeSpec *)events count:(size_t)count;

// To be overridden by subclasses to respond to events.
//
// All subclasses should call [super handleEvent:handler:] if they
// don't handle the event themselves.
//
// Arguments:
//   event - the event to be handled
//   handler - the call ref in case you want to call CallNextEventHandler
//             in your method
// Returns:
//   OSStatus - usually either noErr or eventNotHandledErr
//
- (OSStatus)handleEvent:(GTMCarbonEvent *)event
                handler:(EventHandlerCallRef)handler;

// To be overridden by subclasses to return the event target for the class.
// GTMCarbonEventHandler's implementation returns NULL.
//
// Returns:
//   The event target ref.
//
- (EventTargetRef)eventTarget;

// Gets the underlying EventHandlerRef for that this class wraps.
//
// Returns:
//   The EventHandlerRef this class wraps.
//
- (EventHandlerRef)eventHandler;

// Gets the delegate for the handler
//
// Returns:
//   the delegate
- (id)delegate;

// Sets the delegate for the handler
//
// Arguments:
//   delegate - the delegate to set to
- (void)setDelegate:(id)delegate;

@end

// Category for methods that a delegate of GTMCarbonEventHandlerDelegate may
// want to implement.
@interface NSObject (GTMCarbonEventHandlerDelegate)

// If a delegate implements this method it gets called before every event
// that the handler gets sent. If it returns anything but eventNotHandledErr,
// the handlers handlerEvent:handler: method will not be called, and
// the return value returned by the delegate will be returned back to the
// carbon event dispatch system. This allows you to override any method
// that a handler may implement.
//
// Arguments:
//  delegate - the delegate to set to
//
- (OSStatus)gtm_eventHandler:(GTMCarbonEventHandler *)sender
               receivedEvent:(GTMCarbonEvent *)event
                     handler:(EventHandlerCallRef)handler;

@end

// A general OSType for use when setting properties on GTMCarbonEvent objects.
// This is the "signature" as part of commandIDs, controlsIDs, and properties.
// 'GooG'
GTM_EXTERN const OSType kGTMCarbonFrameworkSignature;

// An event handler class representing the event monitor event handler
//
// there is only one of these per application. This way you can put
// event handlers directly on the dispatcher if necessary.
@interface GTMCarbonEventMonitorHandler : GTMCarbonEventHandler
// Accessor to get the GTMCarbonEventMonitorHandler singleton.
//
// Returns:
//  pointer to the GTMCarbonEventMonitorHandler singleton.
+ (GTMCarbonEventMonitorHandler *)sharedEventMonitorHandler;
@end

// An event handler class representing the application event handler.
//
// there is only one of these per application. This way you can put
// event handlers directly on the application if necessary.
@interface GTMCarbonEventApplicationEventHandler : GTMCarbonEventHandler
// Accessor to get the GTMCarbonEventApplicationEventHandler singleton.
//
// Returns:
//  pointer to the GTMCarbonEventApplicationEventHandler singleton.
+ (GTMCarbonEventApplicationEventHandler *)sharedApplicationEventHandler;
@end

// An event handler class representing the toolbox dispatcher event handler
//
// there is only one of these per application. This way you can put
// event handlers directly on the dispatcher if necessary.
@interface GTMCarbonEventDispatcherHandler : GTMCarbonEventHandler {
 @private
  NSMutableArray *hotkeys_;  // Collection of registered hotkeys
}

// Accessor to get the GTMCarbonEventDispatcherHandler singleton.
//
// Returns:
//  pointer to the GTMCarbonEventDispatcherHandler singleton.
+ (GTMCarbonEventDispatcherHandler *)sharedEventDispatcherHandler;

// Registers a hotkey. When the hotkey is executed by the user, target will be
// called with selector.
//  Arguments:
//    keyCode - the virtual keycode of the hotkey
//    cocoaModifiers - the modifiers that need to be used with |keyCode|. NB
//                     that these are cocoa modifiers, so NSCommandKeyMask etc.
//    target - instance that will get |action| called when the hotkey fires
//    action - the method to call on |target| when the hotkey fires
//    userInfo - storage for callers use
//    onPress - is YES, the hotkey fires on the keydown (usual) otherwise
//              it fires on the key up.
//  Returns:
//    a GTMCarbonHotKey. Note that all hotkeys are unregistered
//    automatically when an app quits. Will be nil on failure.
- (GTMCarbonHotKey *)registerHotKey:(NSUInteger)keyCode
                          modifiers:(NSUInteger)cocoaModifiers
                             target:(id)target
                             action:(SEL)action
                           userInfo:(id)userInfo
                        whenPressed:(BOOL)onPress;

// Unregisters a hotkey previously registered with registerHotKey.
//  Arguments:
//    keyRef - the EventHotKeyRef to unregister
- (void)unregisterHotKey:(GTMCarbonHotKey *)keyRef;

@end

// Wrapper for all the info we need about a hotkey that we can store in a
// Foundation storage class. We expecct selector to have this signature:
// - (void)hitHotKey:(GTMCarbonHotKey *)key;
@interface GTMCarbonHotKey : NSObject {
 @private
  EventHotKeyID id_; // EventHotKeyID for this hotkey.
  EventHotKeyRef hotKeyRef_;
  id target_;  // Object we are going to call when the hotkey is hit
  SEL selector_;  // Selector we are going to call on target_
  BOOL onKeyDown_;  // Do we do it on key down or on key up?
  id userInfo_;
}

- (id)userInfo;
- (EventHotKeyRef)hotKeyRef;
- (BOOL)onKeyDown;
@end
