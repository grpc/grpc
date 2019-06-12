//
//  GTMIBArray.m
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


#import "GTMIBArray.h"
#import "GTMDefines.h"

@implementation GTMIBArray

- (void)dealloc {
  [realArray_ release];
  [super dealloc];
}

- (void)setupRealArray {

#ifdef DEBUG
  // It is very easy to create a cycle if you are chaining these in IB, so in
  // debug builds, we try to catch this to inform the developer.  Use -[NSArray
  // indexOfObjectIdenticalTo:] to get pointer comparisons instead of object
  // equality.
  static NSMutableArray *ibArraysBuilding = nil;
  if (!ibArraysBuilding) {
    ibArraysBuilding = [[NSMutableArray alloc] init];
  }
  _GTMDevAssert([ibArraysBuilding indexOfObjectIdenticalTo:self] == NSNotFound,
                @"There is a cycle in your GTMIBArrays!");
  [ibArraysBuilding addObject:self];
#endif  // DEBUG

  // Build the array up.
  NSMutableArray *builder = [NSMutableArray array];
  Class ibArrayClass = [GTMIBArray class];
  id objs[] = {
    object1_, object2_, object3_, object4_, object5_,
  };
  for (size_t idx = 0 ; idx < sizeof(objs) / (sizeof(objs[0])) ; ++idx) {
    id obj = objs[idx];
    if (obj) {
      if ([obj isKindOfClass:ibArrayClass]) {
        [builder addObjectsFromArray:obj];
      } else {
        [builder addObject:obj];
      }
    }
  }

#ifdef DEBUG
  [ibArraysBuilding removeObject:self];
#endif  // DEBUG

  // Now copy with our zone.
  realArray_ = [builder copyWithZone:[self zone]];
}

// ----------------------------------------------------------------------------
// NSArray has two methods that everything else seems to work on, simply
// implement those.

- (NSUInteger)count {
  if (!realArray_) [self setupRealArray];
  return [realArray_ count];
}

- (id)objectAtIndex:(NSUInteger)idx {
  if (!realArray_) [self setupRealArray];
  return [realArray_ objectAtIndex:idx];
}

// ----------------------------------------------------------------------------
// Directly relay the enumeration based calls just in case there is some extra
// efficency to be had.

- (NSEnumerator *)objectEnumerator {
  if (!realArray_) [self setupRealArray];
  return [realArray_ objectEnumerator];
}

- (NSEnumerator *)reverseObjectEnumerator {
  if (!realArray_) [self setupRealArray];
  return [realArray_ reverseObjectEnumerator];
}

#if MAC_OS_X_VERSION_MAX_ALLOWED >= MAC_OS_X_VERSION_10_5

- (NSUInteger)countByEnumeratingWithState:(NSFastEnumerationState *)state
                                  objects:(id *)stackbuf
                                    count:(NSUInteger)len {
  if (!realArray_) [self setupRealArray];
  return [realArray_ countByEnumeratingWithState:state
                                         objects:stackbuf
                                           count:len];
}

#endif  // MAC_OS_X_VERSION_MAX_ALLOWED >= MAC_OS_X_VERSION_10_5

// ----------------------------------------------------------------------------
// Directly relay the copy methods, again, for any extra efficency.

- (id)copyWithZone:(NSZone *)zone {
  if (!realArray_) [self setupRealArray];
  return [realArray_ copyWithZone:zone];
}

- (id)mutableCopyWithZone:(NSZone *)zone {
  if (!realArray_) [self setupRealArray];
  return [realArray_ mutableCopyWithZone:zone];
}

// ----------------------------------------------------------------------------
// On 10.6, being loaded out of a nib causes the object to get hashed and
// stored off.  The implementation of -hash in NSArray then calls -count, which
// causes this object to latch on to an empty array.  So...
// 1. -hash gets overridden to simply use the class pointer to maintain
//    the -[NSObject hash] contract that equal objects must have the same hash
//    value.  This puts the work in isEqual...
// 2. -isEqual: overide.  Objects can't use the NSArray version until all of
//    the outlets have been filled in and the object is truly setup. The best
//    escape for this is to simply do pointer comparison until the outlets are
//    fully setup.
// 3. awakeFromNib gets overridden to force the initialize of the real array
//    when all the outlets have been filled in.
//
// NOTE: The first attempt was to just overide hash, but that only makes the
// first IBArray in a nib work. The fact that isEqual was falling through to
// the NSArray version (comparing to empty arrays), prevented all of the
// IBArrays from being fully loaded from the nib correctly.

- (NSUInteger)hash {
  return (NSUInteger)(void*)[self class];
}

- (BOOL)isEqual:(id)anObject {
  if ([anObject isMemberOfClass:[self class]]) {
    GTMIBArray *ibArray2 = anObject;
    if (!realArray_ || !(ibArray2->realArray_)) {
      // If realArray_ or ibArray2 haven't been fully configured yet, the only
      // way they can be equal is if they are the same pointer.
      return (self == anObject);
    }
  }
  return [super isEqual:anObject];
}

- (void)awakeFromNib {
  [realArray_ autorelease];
  realArray_ = nil;
  [self setupRealArray];
}

@end
