#import "NSData+GRPC.h"

#include <byte_buffer.h>
#include <string.h>

// TODO(jcanizales): Move these two incantations to the C library.

static void CopyByteBufferToCharArray(grpc_byte_buffer *buffer, char *array) {
  size_t offset = 0;
  grpc_byte_buffer_reader *reader = grpc_byte_buffer_reader_create(buffer);
  gpr_slice next;
  while (grpc_byte_buffer_reader_next(reader, &next) != 0){
    memcpy(array + offset, GPR_SLICE_START_PTR(next), (size_t) GPR_SLICE_LENGTH(next));
    offset += GPR_SLICE_LENGTH(next);
    gpr_slice_unref(next);
  }
  grpc_byte_buffer_reader_destroy(reader);
}

static grpc_byte_buffer *CopyCharArrayToNewByteBuffer(const char *array, size_t length) {
  gpr_slice slice = gpr_slice_from_copied_buffer(array, length);
  grpc_byte_buffer *buffer = grpc_byte_buffer_create(&slice, 1);
  gpr_slice_unref(slice);
  return buffer;
}

@implementation NSData (GRPC)
+ (instancetype)grpc_dataWithByteBuffer:(grpc_byte_buffer *)buffer {
  NSUInteger length = grpc_byte_buffer_length(buffer);
  char *array = malloc(length * sizeof(*array));
  if (!array) {
    // TODO(jcanizales): grpc_byte_buffer is reference-counted, so we can
    // prevent this memory problem by implementing a subclass of NSData
    // that wraps the grpc_byte_buffer. Then enumerateByteRangesUsingBlock:
    // can be implemented using a grpc_byte_buffer_reader.
    return nil;
  }
  CopyByteBufferToCharArray(buffer, array);
  return [self dataWithBytesNoCopy:array length:length freeWhenDone:YES];
}

- (grpc_byte_buffer *)grpc_byteBuffer {
  // Some implementations of NSData, as well as grpc_byte_buffer, support O(1)
  // appending of byte arrays by not using internally a single contiguous memory
  // block for representation.
  // The following implementation is thus not optimal, sometimes requiring two
  // copies (one by self.bytes and another by gpr_slice_from_copied_buffer).
  // If it turns out to be an issue, we can use enumerateByteRangesUsingblock:
  // to create an array of gpr_slice objects to pass to grpc_byte_buffer_create.
  // That would make it do exactly one copy, always.
  return CopyCharArrayToNewByteBuffer((const char *)self.bytes, (size_t)self.length);
}
@end
