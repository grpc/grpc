//
//  GTMTypeCasting.h
//
//  Copyright 2010 Google Inc.
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
#import "GTMDefines.h"

// These are some basic macros for making down-casting safer in Objective C.
// They are loosely based on the same cast types with similar names in C++.
// A typical usage would look like this:
//
// Bar* b = [[Bar alloc] init];
// Foo* a = GTM_STATIC_CAST(Foo, b);
//
// Note that it's GTM_STATIC_CAST(Foo, b) and not GTM_STATIC_CAST(Foo*, b).
//
// GTM_STATIC_CAST runs only in debug mode, and will assert if and only if:
//   - object is non nil
//   - [object isKindOfClass:[cls class]] returns nil
//
// otherwise it returns object.
//
// GTM_DYNAMIC_CAST runs in both debug and release and will return nil if
//   - object is nil
//   - [object isKindOfClass:[cls class]] returns nil
//
// otherwise it returns object.
//

// Support functions for dealing with casting.
GTM_INLINE id GTMDynamicCastSupport(Class cls, id object) {
  _GTMDevAssert(cls, @"Nil Class");
  return [object isKindOfClass:cls] ? object : nil;
}

GTM_INLINE id GTMStaticCastSupport(Class cls, id object) {
  id value = nil;
  if (object) {
    value = GTMDynamicCastSupport(cls, object);
    _GTMDevAssert(value, @"Could not cast %@ to class %@", object, cls);
  }
  return value;
}

#ifndef GTM_STATIC_CAST
  #ifdef DEBUG
    #define GTM_STATIC_CAST(type, object) \
      ((type *) GTMStaticCastSupport([type class], object))
  #else
    #define GTM_STATIC_CAST(type, object) ((type *) (object))
  #endif
#endif

#ifndef GTM_DYNAMIC_CAST
  #define GTM_DYNAMIC_CAST(type, object) \
    ((type *) GTMDynamicCastSupport([type class], object))
#endif
