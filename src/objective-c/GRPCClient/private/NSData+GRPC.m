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

#import "NSData+GRPC.h"

#include <grpc/byte_buffer.h>
#include <grpc/byte_buffer_reader.h>
#include <string.h>

// TODO(jcanizales): Move these two incantations to the C library.

static void MallocAndCopyByteBufferToCharArray(grpc_byte_buffer *buffer,
                                               size_t *length, char **array) {
  grpc_byte_buffer_reader reader;
  if (!grpc_byte_buffer_reader_init(&reader, buffer)) {
    // grpc_byte_buffer_reader_init can fail if the data sent by the server
    // could not be decompressed for any reason. This is an issue with the data
    // coming from the server and thus we want the RPC to fail with error code
    // INTERNAL.
    *array = NULL;
    *length = 0;
    return;
  }
  // The slice contains uncompressed data even if compressed data was received
  // because the reader takes care of automatically decompressing it
  grpc_slice slice = grpc_byte_buffer_reader_readall(&reader);
  size_t uncompressed_length = GRPC_SLICE_LENGTH(slice);
  char *result = malloc(uncompressed_length);
  if (result) {
    memcpy(result, GRPC_SLICE_START_PTR(slice), uncompressed_length);
  }
  grpc_slice_unref(slice);
  *array = result;
  *length = uncompressed_length;
}

static grpc_byte_buffer *CopyCharArrayToNewByteBuffer(const char *array,
                                                      size_t length) {
  grpc_slice slice = grpc_slice_from_copied_buffer(array, length);
  grpc_byte_buffer *buffer = grpc_raw_byte_buffer_create(&slice, 1);
  grpc_slice_unref(slice);
  return buffer;
}

@implementation NSData (GRPC)
+ (instancetype)grpc_dataWithByteBuffer:(grpc_byte_buffer *)buffer {
  if (buffer == NULL) {
    return nil;
  }
  char *array;
  size_t length;
  MallocAndCopyByteBufferToCharArray(buffer, &length, &array);
  if (!array) {
    // TODO(jcanizales): grpc_byte_buffer is reference-counted, so we can
    // prevent this memory problem by implementing a subclass of NSData
    // that wraps the grpc_byte_buffer. Then enumerateByteRangesUsingBlock:
    // can be implemented using a grpc_byte_buffer_reader.
    return nil;
  }
  // Not depending upon size assumption of NSUInteger
  NSUInteger length_max = MIN(length, UINT_MAX);
  return [self dataWithBytesNoCopy:array length:length_max freeWhenDone:YES];
}

- (grpc_byte_buffer *)grpc_byteBuffer {
  // Some implementations of NSData, as well as grpc_byte_buffer, support O(1)
  // appending of byte arrays by not using internally a single contiguous memory
  // block for representation.
  // The following implementation is thus not optimal, sometimes requiring two
  // copies (one by self.bytes and another by grpc_slice_from_copied_buffer).
  // If it turns out to be an issue, we can use enumerateByteRangesUsingblock:
  // to create an array of grpc_slice objects to pass to
  // grpc_raw_byte_buffer_create.
  // That would make it do exactly one copy, always.
  return CopyCharArrayToNewByteBuffer((const char *)self.bytes,
                                      (size_t)self.length);
}
@end
