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
  return [self dataWithBytes:GRPC_SLICE_START_PTR(metadata->value)
                      length:GRPC_SLICE_LENGTH(metadata->value)];
}

- (void)grpc_initMetadata:(grpc_metadata *)metadata {
  metadata->value = grpc_slice_from_copied_buffer(self.bytes, self.length);
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
  return [[self alloc] initWithBytes:GRPC_SLICE_START_PTR(metadata->value)
                              length:GRPC_SLICE_LENGTH(metadata->value)
                            encoding:NSASCIIStringEncoding];
}

// Precondition: This object contains only ASCII characters.
- (void)grpc_initMetadata:(grpc_metadata *)metadata {
  metadata->value = grpc_slice_from_copied_string(self.UTF8String);
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
    char *key = grpc_slice_to_c_string(entry->key);
    NSString *name = [NSString stringWithCString:key encoding:NSASCIIStringEncoding];
    gpr_free(key);
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
  for (NSString *key in self) {
    id value = self[key];
    current->key = grpc_slice_from_copied_string(key.UTF8String);
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
