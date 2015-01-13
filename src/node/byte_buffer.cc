#include <string.h>
#include <malloc.h>

#include <node.h>
#include <nan.h>
#include "grpc/grpc.h"
#include "grpc/support/slice.h"

namespace grpc {
namespace node {

#include "byte_buffer.h"

using ::node::Buffer;
using v8::Handle;
using v8::Value;

grpc_byte_buffer *BufferToByteBuffer(Handle<Value> buffer) {
  NanScope();
  int length = Buffer::Length(buffer);
  char *data = Buffer::Data(buffer);
  gpr_slice slice = gpr_slice_malloc(length);
  memcpy(GPR_SLICE_START_PTR(slice), data, length);
  grpc_byte_buffer *byte_buffer(grpc_byte_buffer_create(&slice, 1));
  gpr_slice_unref(slice);
  return byte_buffer;
}

Handle<Value> ByteBufferToBuffer(grpc_byte_buffer *buffer) {
  NanEscapableScope();
  if (buffer == NULL) {
    NanReturnNull();
  }
  size_t length = grpc_byte_buffer_length(buffer);
  char *result = reinterpret_cast<char*>(calloc(length, sizeof(char)));
  size_t offset = 0;
  grpc_byte_buffer_reader *reader = grpc_byte_buffer_reader_create(buffer);
  gpr_slice next;
  while (grpc_byte_buffer_reader_next(reader, &next) != 0) {
    memcpy(result+offset, GPR_SLICE_START_PTR(next), GPR_SLICE_LENGTH(next));
    offset += GPR_SLICE_LENGTH(next);
  }
  return NanEscapeScope(NanNewBufferHandle(result, length));
}
}  // namespace node
}  // namespace grpc
