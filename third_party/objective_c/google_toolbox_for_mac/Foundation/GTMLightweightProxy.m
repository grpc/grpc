//
//  GTMLightweightProxy.m
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

#import "GTMLightweightProxy.h"
#import "GTMDefines.h"

@implementation GTMLightweightProxy

- (id)initWithRepresentedObject:(id)object {
  // it's weak, we don't retain
  representedObject_ = object;
  return self;
}

- (id)init {
  return [self initWithRepresentedObject:nil];
}

- (void)dealloc {
  // it's weak, we don't release
  representedObject_ = nil;
  [super dealloc];
}

- (id)representedObject {
  // Use a local variable to avoid a bogus compiler warning.
  id repObject = nil;
  @synchronized(self) {
    // Even though we don't retain this object, we hang it on the lifetime
    // of the calling threads pool so it's lifetime is safe for at least that
    // long.
    repObject = [representedObject_ retain];
  }
  return [repObject autorelease];
}

- (void)setRepresentedObject:(id)object {
  @synchronized(self) {
    representedObject_ = object;
  }
}

// Passes any unhandled method to the represented object if it responds to that
// method.
- (void)forwardInvocation:(NSInvocation*)invocation {
  id target = [self representedObject];
  // Silently discard all messages when there's no represented object
  if (!target)
    return;

  SEL aSelector = [invocation selector];
  if ([target respondsToSelector:aSelector])
    [invocation invokeWithTarget:target];
}

// Gets the represented object's method signature for |selector|; necessary for
// forwardInvocation.
- (NSMethodSignature*)methodSignatureForSelector:(SEL)selector {
  id target = [self representedObject];
  if (target) {
    return [target methodSignatureForSelector:selector];
  } else {
    // Apple's underlying forwarding code crashes if we return nil here.
    // Since we are not going to use the invocation being constructed
    // if there's no representedObject, a random valid NSMethodSignature is fine.
    return [NSObject methodSignatureForSelector:@selector(alloc)];
  }
}

// Prevents exceptions from unknown selectors if there is no represented
// object, and makes the exception come from the right place if there is one.
- (void)doesNotRecognizeSelector:(SEL)selector {
  id target = [self representedObject];
  if (target)
    [target doesNotRecognizeSelector:selector];
}

// Checks the represented object's selectors to allow clients of the proxy to
// do respondsToSelector: tests.
- (BOOL)respondsToSelector:(SEL)selector {
  if (selector == @selector(initWithRepresentedObject:) ||
      selector == @selector(representedObject) ||
      selector == @selector(setRepresentedObject:) ||
      [super respondsToSelector:selector]) {
    return YES;
  }

  id target = [self representedObject];
  return target && [target respondsToSelector:selector];
}

@end
