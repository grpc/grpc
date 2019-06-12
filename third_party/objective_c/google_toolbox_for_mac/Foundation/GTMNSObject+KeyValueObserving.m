//
//  GTMNSObject+KeyValueObserving.m
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

//
//  MAKVONotificationCenter.m
//  MAKVONotificationCenter
//
//  Created by Michael Ash on 10/15/08.
//

// This code is based on code by Michael Ash.
// See comment in header.
#import "GTMNSObject+KeyValueObserving.h"

#import <libkern/OSAtomic.h>
#include <objc/runtime.h>

#import "GTMDefines.h"
#import "GTMDebugSelectorValidation.h"
#import "GTMMethodCheck.h"

// A singleton that works as a dispatch center for KVO
// -[NSObject observeValueForKeyPath:ofObject:change:context:] and turns them
// into selector dispatches. It stores a collection of
// GTMKeyValueObservingHelpers, and keys them via the key generated by
// -dictionaryKeyForObserver:ofObject:forKeyPath:selector.
@interface GTMKeyValueObservingCenter : NSObject {
 @private
  NSMutableDictionary *observerHelpers_;
}

+ (id)defaultCenter;

- (void)addObserver:(id)observer
           ofObject:(id)target
         forKeyPath:(NSString *)keyPath
           selector:(SEL)selector
           userInfo:(id)userInfo
            options:(NSKeyValueObservingOptions)options;
- (void)removeObserver:(id)observer
              ofObject:(id)target
            forKeyPath:(NSString *)keyPath
              selector:(SEL)selector;
- (id)dictionaryKeyForObserver:(id)observer
                      ofObject:(id)target
                    forKeyPath:(NSString *)keyPath
                      selector:(SEL)selector;
@end

@interface GTMKeyValueObservingHelper : NSObject {
 @private
  GTM_WEAK id observer_;
  SEL selector_;
  id userInfo_;
  GTM_WEAK id target_;
  NSString* keyPath_;
}

- (id)initWithObserver:(id)observer
                object:(id)target
               keyPath:(NSString *)keyPath
              selector:(SEL)selector
              userInfo:(id)userInfo
               options:(NSKeyValueObservingOptions)options;
- (void)deregister;

@end

@interface GTMKeyValueChangeNotification ()
- (id)initWithKeyPath:(NSString *)keyPath ofObject:(id)object
             userInfo:(id)userInfo change:(NSDictionary *)change;
@end

@implementation GTMKeyValueObservingHelper

// For info how and why we use these statics:
// http://lists.apple.com/archives/cocoa-dev/2006/Jul/msg01038.html
static char GTMKeyValueObservingHelperContextData;
static char* GTMKeyValueObservingHelperContext
  = &GTMKeyValueObservingHelperContextData;

- (id)initWithObserver:(id)observer
                object:(id)target
               keyPath:(NSString *)keyPath
              selector:(SEL)selector
              userInfo:(id)userInfo
               options:(NSKeyValueObservingOptions)options {
  if((self = [super init])) {
    observer_ = observer;
    selector_ = selector;
    userInfo_ = [userInfo retain];

    target_ = target;
    keyPath_ = [keyPath retain];

    [target addObserver:self
             forKeyPath:keyPath
                options:options
                context:GTMKeyValueObservingHelperContext];
  }
  return self;
}

- (NSString *)description {
  return [NSString stringWithFormat:
          @"%@ <observer = %@ keypath = %@ target = %@ selector = %@>",
          [self class], observer_, keyPath_, target_,
          NSStringFromSelector(selector_)];
}

- (void)dealloc {
  if (target_) {
    _GTMDevLog(@"Didn't deregister %@", self);
    [self deregister];
  }
  [userInfo_ release];
  [keyPath_ release];
  [super dealloc];
}

- (void)observeValueForKeyPath:(NSString *)keyPath
                      ofObject:(id)object
                        change:(NSDictionary *)change
                       context:(void *)context {
  if(context == GTMKeyValueObservingHelperContext) {
    GTMKeyValueChangeNotification *notification
      = [[GTMKeyValueChangeNotification alloc] initWithKeyPath:keyPath
                                                      ofObject:object
                                                      userInfo:userInfo_
                                                        change:change];
    [observer_ performSelector:selector_ withObject:notification];
    [notification release];
  } else {
    // COV_NF_START
    // There's no way this should ever be called.
    // If it is, the call will go up to NSObject which will assert.
    [super observeValueForKeyPath:keyPath
                         ofObject:object
                           change:change
                          context:context];
    // COV_NF_END
  }
}

- (void)deregister {
  [target_ removeObserver:self forKeyPath:keyPath_];
  target_ = nil;
}

@end

@implementation GTMKeyValueObservingCenter

+ (id)defaultCenter {
  static GTMKeyValueObservingCenter *center = nil;
  if(!center) {
    // do a bit of clever atomic setting to make this thread safe
    // if two threads try to set simultaneously, one will fail
    // and the other will set things up so that the failing thread
    // gets the shared center
    GTMKeyValueObservingCenter *newCenter = [[self alloc] init];
    if(!OSAtomicCompareAndSwapPtrBarrier(NULL,
                                         newCenter,
                                         (void *)&center)) {
      [newCenter release];  // COV_NF_LINE no guarantee we'll hit this line
    }
  }
  return center;
}

- (id)init {
  if((self = [super init])) {
    observerHelpers_ = [[NSMutableDictionary alloc] init];
  }
  return self;
}

// COV_NF_START
// Singletons don't get deallocated
- (void)dealloc {
  [observerHelpers_ release];
  [super dealloc];
}
// COV_NF_END

- (id)dictionaryKeyForObserver:(id)observer
                      ofObject:(id)target
                    forKeyPath:(NSString *)keyPath
                      selector:(SEL)selector {
  NSString *key = nil;
  if (!target && !keyPath && !selector) {
    key = [NSString stringWithFormat:@"%p:", observer];
  } else {
    key = [NSString stringWithFormat:@"%p:%@:%p:%p",
           observer, keyPath, selector, target];
  }
  return key;
}

- (void)addObserver:(id)observer
           ofObject:(id)target
         forKeyPath:(NSString *)keyPath
           selector:(SEL)selector
           userInfo:(id)userInfo
            options:(NSKeyValueObservingOptions)options {
  GTMKeyValueObservingHelper *helper
    = [[GTMKeyValueObservingHelper alloc] initWithObserver:observer
                                                    object:target
                                                   keyPath:keyPath
                                                  selector:selector
                                                  userInfo:userInfo
                                                   options:options];
  id key = [self dictionaryKeyForObserver:observer
                                 ofObject:target
                               forKeyPath:keyPath
                                 selector:selector];
  @synchronized(self) {
    GTMKeyValueObservingHelper *oldHelper = [observerHelpers_ objectForKey:key];
    if (oldHelper) {
      _GTMDevLog(@"%@ already observing %@ forKeyPath %@",
                 observer, target, keyPath);
      [oldHelper deregister];
    }
    [observerHelpers_ setObject:helper forKey:key];
  }
  [helper release];
}

- (void)removeObserver:(id)observer
              ofObject:(id)target
            forKeyPath:(NSString *)keyPath
              selector:(SEL)selector {
  id key = [self dictionaryKeyForObserver:observer
                                 ofObject:target
                               forKeyPath:keyPath
                                 selector:selector];
  NSMutableArray *allValidHelperKeys = [NSMutableArray array];
  NSArray *allValidHelpers = nil;
  @synchronized(self) {

    NSString *helperKey;
    for (helperKey in [observerHelpers_ allKeys]) {
      if ([helperKey hasPrefix:key]) {
        [allValidHelperKeys addObject:helperKey];
      }
    }
#if DEBUG
    if ([allValidHelperKeys count] == 0) {
      _GTMDevLog(@"%@ was not observing %@ with keypath %@",
                 observer, target, keyPath);
    }
#endif // DEBUG
    allValidHelpers = [observerHelpers_ objectsForKeys:allValidHelperKeys
                                        notFoundMarker:[NSNull null]];
    [observerHelpers_ removeObjectsForKeys:allValidHelperKeys];
  }
  [allValidHelpers makeObjectsPerformSelector:@selector(deregister)];
}

@end

@implementation NSObject (GTMKeyValueObservingAdditions)

- (void)gtm_addObserver:(id)observer
             forKeyPath:(NSString *)keyPath
               selector:(SEL)selector
               userInfo:(id)userInfo
                options:(NSKeyValueObservingOptions)options {
  _GTMDevAssert(observer && [keyPath length] && selector,
                @"Missing observer, keyPath, or selector");
  GTMKeyValueObservingCenter *center
    = [GTMKeyValueObservingCenter defaultCenter];
  GTMAssertSelectorNilOrImplementedWithArguments(
      observer,
      selector,
      @encode(GTMKeyValueChangeNotification *),
      NULL);
  [center addObserver:observer
             ofObject:self
           forKeyPath:keyPath
             selector:selector
             userInfo:userInfo
              options:options];
}

- (void)gtm_removeObserver:(id)observer
                forKeyPath:(NSString *)keyPath
                  selector:(SEL)selector {
  _GTMDevAssert(observer && [keyPath length] && selector,
                @"Missing observer, keyPath, or selector");
  GTMKeyValueObservingCenter *center
    = [GTMKeyValueObservingCenter defaultCenter];
  GTMAssertSelectorNilOrImplementedWithArguments(
      observer,
      selector,
      @encode(GTMKeyValueChangeNotification *),
      NULL);
  [center removeObserver:observer
                ofObject:self
              forKeyPath:keyPath
                selector:selector];
}

- (void)gtm_stopObservingAllKeyPaths {
  GTMKeyValueObservingCenter *center
    = [GTMKeyValueObservingCenter defaultCenter];
  [center removeObserver:self
                ofObject:nil
              forKeyPath:nil
                selector:Nil];
}

@end


@implementation GTMKeyValueChangeNotification

- (id)initWithKeyPath:(NSString *)keyPath ofObject:(id)object
             userInfo:(id)userInfo change:(NSDictionary *)change {
  if ((self = [super init])) {
    keyPath_ = [keyPath copy];
    object_ = [object retain];
    userInfo_ = [userInfo retain];
    change_ = [change retain];
  }
  return self;
}

- (void)dealloc {
  [keyPath_ release];
  [object_ release];
  [userInfo_ release];
  [change_ release];
  [super dealloc];
}

- (id)copyWithZone:(NSZone *)zone {
  return [[[self class] allocWithZone:zone] initWithKeyPath:keyPath_
                                                   ofObject:object_
                                                   userInfo:userInfo_
                                                     change:change_];
}

- (BOOL)isEqual:(id)object {
  return ([keyPath_ isEqualToString:[object keyPath]]
          && [object_ isEqual:[object object]]
          && [userInfo_ isEqual:[object userInfo]]
          && [change_ isEqual:[object change]]);
}

- (NSString *)description {
  return [NSString stringWithFormat:
          @"%@ <object = %@ keypath = %@ userInfo = %@ change = %@>",
          [self class], object_, keyPath_, userInfo_, change_];
}

- (NSUInteger)hash {
  return [keyPath_ hash] + [object_ hash] + [userInfo_ hash] + [change_ hash];
}

- (NSString *)keyPath {
  return keyPath_;
}

- (id)object {
  return object_;
}

- (id)userInfo {
  return userInfo_;
}

- (NSDictionary *)change {
  return change_;
}

@end

#ifdef DEBUG

static void SwizzleMethodsInClass(Class cls, SEL sel1, SEL sel2) {
  Method m1 = class_getInstanceMethod(cls, sel1);
  Method m2 = class_getInstanceMethod(cls, sel2);
  method_exchangeImplementations(m1, m2);
}

// This category exists to attempt to help deal with tricky KVO issues.
// KVO is a wonderful technology in some ways, but is extremely fragile and
// allows developers a lot of freedom to shoot themselves in the foot.
// Refactoring an app that uses a lot of KVO can be really difficult, as can
// debugging it.
// These are some tools that we have found useful when working with KVO. Note
// that these tools are only on in Debug builds.
// To enable our KVO debugging, set the GTMDebugKVO environment
// variable to 1 and you will get a whole pile of KVO logging that may help you
// track down problems.
// bash - export GTMDebugKVO=1
// csh/tcsh - setenv GTMDebugKVO 1
//

@interface NSObject (GTMDebugKeyValueObserving)
- (void)_gtmDebugAddObserver:(NSObject *)observer
                  forKeyPath:(NSString *)keyPath
                     options:(NSKeyValueObservingOptions)options
                     context:(void *)context;
- (void)_gtmDebugArrayAddObserver:(NSObject *)observer
               toObjectsAtIndexes:(NSIndexSet *)indexes
                       forKeyPath:(NSString *)keyPath
                          options:(NSKeyValueObservingOptions)options
                          context:(void *)context;
- (void)_gtmDebugRemoveObserver:(NSObject *)observer
                     forKeyPath:(NSString *)keyPath;
- (void)_gtmDebugArrayRemoveObserver:(NSObject *)observer
                fromObjectsAtIndexes:(NSIndexSet *)indexes
                          forKeyPath:(NSString *)keyPath;
- (void)_gtmDebugWillChangeValueForKey:(NSString*)key;
- (void)_gtmDebugDidChangeValueForKey:(NSString*)key;

@end

@implementation NSObject (GTMDebugKeyValueObserving)
GTM_METHOD_CHECK(NSObject, _gtmDebugAddObserver:forKeyPath:options:context:);
GTM_METHOD_CHECK(NSObject, _gtmDebugRemoveObserver:forKeyPath:);
GTM_METHOD_CHECK(NSObject, _gtmDebugWillChangeValueForKey:);
GTM_METHOD_CHECK(NSObject, _gtmDebugDidChangeValueForKey:);
GTM_METHOD_CHECK(NSArray,
    _gtmDebugArrayAddObserver:toObjectsAtIndexes:forKeyPath:options:context:);
GTM_METHOD_CHECK(NSArray,
    _gtmDebugArrayRemoveObserver:fromObjectsAtIndexes:forKeyPath:);

+ (void)load {
  NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];
  NSDictionary *env = [[NSProcessInfo processInfo] environment];
  id debugKeyValue = [env valueForKey:@"GTMDebugKVO"];
  BOOL debug = NO;
  if ([debugKeyValue isKindOfClass:[NSNumber class]]) {
    debug = [debugKeyValue intValue] != 0 ? YES : NO;
  } else if ([debugKeyValue isKindOfClass:[NSString class]]) {
    debug = ([debugKeyValue hasPrefix:@"Y"] || [debugKeyValue hasPrefix:@"T"] ||
             [debugKeyValue intValue]);
  }
  Class cls = Nil;
  if (debug) {
    cls = [NSObject class];
    SwizzleMethodsInClass(cls,
        @selector(addObserver:forKeyPath:options:context:),
        @selector(_gtmDebugAddObserver:forKeyPath:options:context:));
    SwizzleMethodsInClass(cls,
        @selector(removeObserver:forKeyPath:),
        @selector(_gtmDebugRemoveObserver:forKeyPath:));
    SwizzleMethodsInClass(cls,
        @selector(willChangeValueForKey:),
        @selector(_gtmDebugWillChangeValueForKey:));
    SwizzleMethodsInClass(cls,
        @selector(didChangeValueForKey:),
        @selector(_gtmDebugDidChangeValueForKey:));
    cls = [NSArray class];
    SwizzleMethodsInClass(cls,
        @selector(addObserver:toObjectsAtIndexes:forKeyPath:options:context:),
        @selector(_gtmDebugArrayAddObserver:toObjectsAtIndexes:forKeyPath:options:context:));
    SwizzleMethodsInClass(cls,
        @selector(removeObserver:fromObjectsAtIndexes:forKeyPath:),
        @selector(_gtmDebugArrayRemoveObserver:fromObjectsAtIndexes:forKeyPath:));
  }
  [pool drain];
}

- (void)_gtmDebugAddObserver:(NSObject *)observer
                  forKeyPath:(NSString *)keyPath
                     options:(NSKeyValueObservingOptions)options
                     context:(void *)context {
  _GTMDevLog(@"Adding observer %@ to %@ keypath '%@'", observer, self, keyPath);
  [self _gtmDebugAddObserver:observer forKeyPath:keyPath
                     options:options context:context];
}

- (void)_gtmDebugArrayAddObserver:(NSObject *)observer
               toObjectsAtIndexes:(NSIndexSet *)indexes
                       forKeyPath:(NSString *)keyPath
                          options:(NSKeyValueObservingOptions)options
                          context:(void *)context {
  _GTMDevLog(@"Array adding observer %@ to indexes %@ of %@ keypath '%@'",
             observer, indexes, self, keyPath);
  [self _gtmDebugArrayAddObserver:observer
               toObjectsAtIndexes:indexes
                       forKeyPath:keyPath
                          options:options context:context];
}

- (void)_gtmDebugRemoveObserver:(NSObject *)observer
                     forKeyPath:(NSString *)keyPath {
  _GTMDevLog(@"Removing observer %@ from %@ keypath '%@'",
             observer, self, keyPath);
  [self _gtmDebugRemoveObserver:observer forKeyPath:keyPath];
}

- (void)_gtmDebugArrayRemoveObserver:(NSObject *)observer
                fromObjectsAtIndexes:(NSIndexSet *)indexes
                          forKeyPath:(NSString *)keyPath {
  _GTMDevLog(@"Array removing observer %@ from indexes %@ of %@ keypath '%@'",
             indexes, observer, self, keyPath);
  [self _gtmDebugArrayRemoveObserver:observer
                fromObjectsAtIndexes:indexes
                          forKeyPath:keyPath];
}

- (void)_gtmDebugWillChangeValueForKey:(NSString*)key {
  _GTMDevLog(@"Will change '%@' of %@", key, self);
  [self _gtmDebugWillChangeValueForKey:key];
}

- (void)_gtmDebugDidChangeValueForKey:(NSString*)key {
  _GTMDevLog(@"Did change '%@' of %@", key, self);
  [self _gtmDebugDidChangeValueForKey:key];
}

@end

#endif  // DEBUG
