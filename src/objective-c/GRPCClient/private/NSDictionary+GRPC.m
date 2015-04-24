/*
 *
 * Copyright 2015, Google Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *     * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 * copyright notice, this list of conditions and the following disclaimer
 * in the documentation and/or other materials provided with the
 * distribution.
 *     * Neither the name of Google Inc. nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

#import "NSDictionary+GRPC.h"

#include <grpc/grpc.h>
#include <grpc/support/alloc.h>

@implementation NSDictionary (GRPC)
+ (instancetype)grpc_dictionaryFromMetadata:(grpc_metadata *)entries count:(size_t)count {
  NSMutableDictionary *metadata = [NSMutableDictionary dictionaryWithCapacity:count];
  for (grpc_metadata *entry = entries; entry < entries + count; entry++) {
    // TODO(jcanizales): Verify in a C library test that it's converting header names to lower case automatically.
    NSString *name = [NSString stringWithUTF8String:entry->key];
    if (!name) {
      continue;
    }
    if (!metadata[name]) {
      metadata[name] = [NSMutableArray array];
    }
    // TODO(jcanizales): Should we use a non-copy constructor?
    [metadata[name] addObject:[NSData dataWithBytes:entry->value
                                             length:entry->value_length]];
  }
  return metadata;
}

- (size_t)grpc_toMetadataArray:(grpc_metadata **)metadata {
  size_t count = 0;
  size_t capacity = 0;
  for (id key in self) {
    capacity += [self[key] count];
  }
  *metadata = gpr_malloc(capacity * sizeof(grpc_metadata));
  for (id key in self) {
    id value_array = self[key];
    for (id value in value_array) {
      grpc_metadata *current = &(*metadata)[count];
      current->key = [key UTF8String];
      current->value = [value UTF8String];
      current->value_length = [value length];
      count += 1;
    }
  }
  return count;
}
@end
