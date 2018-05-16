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

#import "GRXMappingWriter.h"

@interface GRXForwardingWriter ()<GRXWriteable>
@end

@implementation GRXMappingWriter {
  id (^_map)(id value);
}

- (instancetype)initWithWriter:(GRXWriter *)writer {
  return [self initWithWriter:writer map:nil];
}

// Designated initializer
- (instancetype)initWithWriter:(GRXWriter *)writer map:(id (^)(id value))map {
  if ((self = [super initWithWriter:writer])) {
    _map = map ?: ^id(id value) {
      return value;
    };
  }
  return self;
}

// Override
- (void)writeValue:(id)value {
  [super writeValue:_map(value)];
}
@end
