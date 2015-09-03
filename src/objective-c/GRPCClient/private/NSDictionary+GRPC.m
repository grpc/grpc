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

// Fill a metadata object with the binary value in this NSData.
- (void)grpc_initMetadata:(grpc_metadata *)metadata;
@end

@implementation NSData (GRPCMetadata)
+ (instancetype)grpc_dataFromMetadataValue:(grpc_metadata *)metadata {
  // TODO(jcanizales): Should we use a non-copy constructor?
  return [self dataWithBytes:metadata->value length:metadata->value_length];
}

- (void)grpc_initMetadata:(grpc_metadata *)metadata {
  metadata->value = self.bytes;
  metadata->value_length = self.length;
}
@end

#pragma mark Category for textual metadata elements

@interface NSString (GRPCMetadata)
+ (instancetype)grpc_stringFromMetadataValue:(grpc_metadata *)metadata;

// Fill a metadata object with the textual value in this NSString.
- (void)grpc_initMetadata:(grpc_metadata *)metadata;
@end

@implementation NSString (GRPCMetadata)
+ (instancetype)grpc_stringFromMetadataValue:(grpc_metadata *)metadata {
  return [[self alloc] initWithBytes:metadata->value
                              length:metadata->value_length
                            encoding:NSASCIIStringEncoding];
}

// Precondition: This object contains only ASCII characters.
- (void)grpc_initMetadata:(grpc_metadata *)metadata {
  metadata->value = self.UTF8String;
  metadata->value_length = self.length;
}
@end

#pragma mark Category for metadata arrays

@implementation NSDictionary (GRPC)
+ (instancetype)grpc_dictionaryFromMetadataArray:(grpc_metadata_array)array {
  return [self grpc_dictionaryFromMetadata:array.metadata count:array.count];
}

+ (instancetype)grpc_dictionaryFromMetadata:(grpc_metadata *)entries count:(size_t)count {
  NSMutableDictionary *metadata = [NSMutableDictionary dictionaryWithCapacity:count];
  for (grpc_metadata *entry = entries; entry < entries + count; entry++) {
    NSString *name = [NSString stringWithCString:entry->key encoding:NSASCIIStringEncoding];
    if (!name || metadata[name]) {
      // Log if name is nil?
      continue;
    }
    id value;
    if ([name hasSuffix:@"-bin"]) {
      value = [NSData grpc_dataFromMetadataValue:entry];
    } else {
      value = [NSString grpc_stringFromMetadataValue:entry];
    }
    metadata[name] = value;
  }
  return metadata;
}

// Preconditions: All keys are ASCII strings. Keys ending in -bin have NSData values; the others
// have NSString values.
- (grpc_metadata *)grpc_metadataArray {
  grpc_metadata *metadata = gpr_malloc([self count] * sizeof(grpc_metadata));
  grpc_metadata *current = metadata;
  for (NSString* key in self) {
    id value = self[key];
    current->key = key.UTF8String;
    if ([value respondsToSelector:@selector(grpc_initMetadata:)]) {
      [value grpc_initMetadata:current];
    } else {
      [NSException raise:NSInvalidArgumentException
                  format:@"Metadata values must be NSString or NSData."];
    }
    ++current;
  }
  return metadata;
}
@end
