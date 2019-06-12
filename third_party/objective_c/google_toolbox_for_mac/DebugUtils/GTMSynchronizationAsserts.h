//
//  GTMSynchronizationAsserts.h
//
//  Copyright 2016 Google Inc.
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

#if !__has_feature(objc_arc)
#error "This file needs to be compiled with ARC enabled."
#endif

#import <Foundation/Foundation.h>

#import "GTMDefines.h"  // For _GTMDevAssert.

// Macros to monitor synchronization blocks in debug builds.
//
// These report problems using _GTMDevAssert, which may be defined by the
// project or by GTMDefines.h
//
// GTMMonitorSynchronized           Start monitoring a top-level-only @sync scope.
//                                  Asserts if already inside a monitored @sync scope.
// GTMMonitorRecursiveSynchronized  Start monitoring a top-level or recursive @sync
//                                  scope.
// GTMCheckSynchronized             Assert that the current execution is inside a monitored @sync
//                                  scope.
// GTMCheckNotSynchronized          Assert that the current execution is not inside a monitored
//                                  @sync scope.
//
// Example usage:
//
// - (void)myExternalMethod {
//   @synchronized(self) {
//     GTMMonitorSynchronized(self)
//
// - (void)myInternalMethod {
//   GTMCheckSynchronized(self);
//
// - (void)callMyCallbacks {
//   GTMCheckNotSynchronized(self);
//
// GTMCheckNotSynchronized is available for verifying the code isn't
// in a deadlockable @sync state, important when posting notifications and
// invoking callbacks.
//
// Don't use GTMCheckNotSynchronized immediately before a @sync scope; the
// normal recursiveness check of GTMMonitorSynchronized can catch those.

#if DEBUG

  #define __GTMMonitorSynchronizedVariableInner(varname, counter) \
      varname ## counter
  #define __GTMMonitorSynchronizedVariable(varname, counter)      \
      __GTMMonitorSynchronizedVariableInner(varname, counter)

  #define GTMMonitorSynchronized(obj)                                           \
      NS_VALID_UNTIL_END_OF_SCOPE id                                            \
        __GTMMonitorSynchronizedVariable(__monitor, __COUNTER__) =              \
        [[GTMSyncMonitorInternal alloc] initWithSynchronizationObject:obj       \
                                                    allowRecursive:NO           \
                                                     functionName:__func__]

  #define GTMMonitorRecursiveSynchronized(obj)                                  \
      NS_VALID_UNTIL_END_OF_SCOPE id                                            \
        __GTMMonitorSynchronizedVariable(__monitor, __COUNTER__) =              \
        [[GTMSyncMonitorInternal alloc] initWithSynchronizationObject:obj       \
                                                    allowRecursive:YES          \
                                                     functionName:__func__]

  #define GTMCheckSynchronized(obj) {                                           \
      _GTMDevAssert(                                                            \
          [GTMSyncMonitorInternal functionsHoldingSynchronizationOnObject:obj], \
          @"GTMCheckSynchronized(" #obj ") failed: not sync'd"                  \
          @" on " #obj " in %s. Call stack:\n%@",                               \
          __func__, [NSThread callStackSymbols]);                               \
      }

  #define GTMCheckNotSynchronized(obj) {                                       \
      _GTMDevAssert(                                                           \
        ![GTMSyncMonitorInternal functionsHoldingSynchronizationOnObject:obj], \
        @"GTMCheckNotSynchronized(" #obj ") failed: was sync'd"                \
        @" on " #obj " in %s by %@. Call stack:\n%@", __func__,                \
        [GTMSyncMonitorInternal functionsHoldingSynchronizationOnObject:obj],  \
        [NSThread callStackSymbols]);                                          \
      }

// GTMSyncMonitorInternal is a private class that keeps track of the
// beginning and end of synchronized scopes, relying on ARC to release
// it at the end of a scope.
//
// This class should not be used directly, but only via the
// GTMMonitorSynchronized macro.
@interface GTMSyncMonitorInternal : NSObject {
  NSValue *_objectKey;        // The synchronize target object.
  const char *_functionName;  // The function containing the monitored sync block.
}

- (instancetype)initWithSynchronizationObject:(id)object
                               allowRecursive:(BOOL)allowRecursive
                                 functionName:(const char *)functionName;
// Return the names of the functions that hold sync on the object, or nil if none.
+ (NSArray *)functionsHoldingSynchronizationOnObject:(id)object;
@end

#else

  // !DEBUG
  #define GTMMonitorSynchronized(obj) do { } while (0)
  #define GTMMonitorRecursiveSynchronized(obj) do { } while (0)
  #define GTMCheckSynchronized(obj) do { } while (0)
  #define GTMCheckNotSynchronized(obj) do { } while (0)

#endif  // DEBUG
