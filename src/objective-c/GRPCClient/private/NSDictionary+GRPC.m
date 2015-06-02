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

#include <grpc/support/alloc.h>

#pragma mark Category for binary metadata elements

@interface NSData (GRPCMetadata)
+ (instancetype)grpc_dataFromMetadataValue:(grpc_metadata *)metadata;

// Fill a metadata object with the binary value in this NSData and the given key.
- (void)grpc_initMetadata:(grpc_metadata *)metadata withKey:(NSString *)key;
@end

@implementation NSData (GRPCMetadata)
+ (instancetype)grpc_dataFromMetadataValue:(grpc_metadata *)metadata {
  // TODO(jcanizales): Should we use a non-copy constructor?
  return [self dataWithBytes:metadata->value length:metadata->value_length];
}

- (void)grpc_initMetadata:(grpc_metadata *)metadata withKey:(NSString *)key {
  // TODO(jcanizales): Encode Unicode chars as ASCII.
  metadata->key = [key stringByAppendingString:@"-bin"].UTF8String;
  metadata->value = self.bytes;
  metadata->value_length = self.length;
}
@end

#pragma mark Category for textual metadata elements

@interface NSString (GRPCMetadata)
+ (instancetype)grpc_stringFromMetadataValue:(grpc_metadata *)metadata;

// Fill a metadata object with the textual value in this NSString and the given key.
- (void)grpc_initMetadata:(grpc_metadata *)metadata withKey:(NSString *)key;
@end

@implementation NSString (GRPCMetadata)
+ (instancetype)grpc_stringFromMetadataValue:(grpc_metadata *)metadata {
  return [[self alloc] initWithBytes:metadata->value
                              length:metadata->value_length
                            encoding:NSASCIIStringEncoding];
}

- (void)grpc_initMetadata:(grpc_metadata *)metadata withKey:(NSString *)key {
  if ([key hasSuffix:@"-bin"]) {
    // Disallow this, as at best it will confuse the server. If the app really needs to send a
    // textual header with a name ending in "-bin", it can be done by removing the suffix and
    // encoding the NSString as a NSData object.
    //
    // Why raise an exception: In the most common case, the developer knows this won't happen in
    // their code, so the exception isn't triggered. In the rare cases when the developer can't
    // tell, it's easy enough to add a sanitizing filter before the header is set. There, the
    // developer can choose whether to drop such a header, or trim its name. Doing either ourselves,
    // silently, would be very unintuitive for the user.
    [NSException raise:NSInvalidArgumentException
                format:@"Metadata keys ending in '-bin' are reserved for NSData values."];
  }
  // TODO(jcanizales): Encode Unicode chars as ASCII.
  metadata->key = key.UTF8String;
  metadata->value = self.UTF8String;
  metadata->value_length = self.length;
}
@end

#pragma mark Category for metadata arrays

@implementation NSDictionary (GRPC)
+ (instancetype)grpc_dictionaryFromMetadata:(grpc_metadata *)entries count:(size_t)count {
  NSMutableDictionary *metadata = [NSMutableDictionary dictionaryWithCapacity:count];
  for (grpc_metadata *entry = entries; entry < entries + count; entry++) {
    // TODO(jcanizales): Verify in a C library test that it's converting header names to lower case
    // automatically.
    NSString *name = [NSString stringWithCString:entry->key encoding:NSASCIIStringEncoding];
    if (!name) {
      // log?
      continue;
    }
    id value;
    if ([name hasSuffix:@"-bin"]) {
      name = [name substringToIndex:name.length - 4];
      value = [NSData grpc_dataFromMetadataValue:entry];
    } else {
      value = [NSString grpc_stringFromMetadataValue:entry];
    }
    if (!metadata[name]) {
      metadata[name] = [NSMutableArray array];
    }
    [metadata[name] addObject:value];
  }
  return metadata;
}

- (grpc_metadata *)grpc_metadataArray {
  grpc_metadata *metadata = gpr_malloc([self count] * sizeof(grpc_metadata));
  int i = 0;
  for (id key in self) {
    id value = self[key];
    grpc_metadata *current = &metadata[i];
    if ([value respondsToSelector:@selector(grpc_initMetadata:withKey:)]) {
      [value grpc_initMetadata:current withKey:key];
    } else {
      [NSException raise:NSInvalidArgumentException
                  format:@"Metadata values must be NSString or NSData."];
    }
    i += 1;
  }
  return metadata;
}
@end
