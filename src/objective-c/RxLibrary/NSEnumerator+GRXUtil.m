/*
 *
 * Copyright 2015 gRPC authors.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

#import "NSEnumerator+GRXUtil.h"

#import "private/GRXNSBlockEnumerator.h"
#import "private/GRXNSFastEnumerator.h"
#import "private/GRXNSScalarEnumerator.h"

@implementation NSEnumerator (GRXUtil)

+ (NSEnumerator *)grx_enumeratorWithContainer:(id<NSFastEnumeration>)container {
  // TODO(jcanizales): Consider checking if container responds to objectEnumerator and return that?
  return [[GRXNSFastEnumerator alloc] initWithContainer:container];
}

+ (NSEnumerator *)grx_enumeratorWithSingleValue:(id)value {
  return [[GRXNSScalarEnumerator alloc] initWithValue:value];
}

+ (NSEnumerator *)grx_enumeratorWithValueSupplier:(id (^)(void))block {
  return [[GRXNSBlockEnumerator alloc] initWithValueSupplier:block];
}
@end
