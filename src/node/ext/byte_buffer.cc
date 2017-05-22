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

#include <string.h>

#include <nan.h>
#include <node.h>
#include "grpc/byte_buffer_reader.h"
#include "grpc/grpc.h"
#include "grpc/slice.h"

#include "byte_buffer.h"
#include "slice.h"

namespace grpc {
namespace node {

using Nan::Callback;
using Nan::MaybeLocal;

using v8::Function;
using v8::Local;
using v8::Object;
using v8::Number;
using v8::Value;

grpc_byte_buffer *BufferToByteBuffer(Local<Value> buffer) {
  Nan::HandleScope scope;
  grpc_slice slice = CreateSliceFromBuffer(buffer);
  grpc_byte_buffer *byte_buffer(grpc_raw_byte_buffer_create(&slice, 1));
  grpc_slice_unref(slice);
  return byte_buffer;
}

namespace {
void delete_buffer(char *data, void *hint) {
  grpc_slice *slice = static_cast<grpc_slice *>(hint);
  grpc_slice_unref(*slice);
  delete slice;
}
}

Local<Value> ByteBufferToBuffer(grpc_byte_buffer *buffer) {
  Nan::EscapableHandleScope scope;
  if (buffer == NULL) {
    return scope.Escape(Nan::Null());
  }
  grpc_byte_buffer_reader reader;
  if (!grpc_byte_buffer_reader_init(&reader, buffer)) {
    Nan::ThrowError("Error initializing byte buffer reader.");
    return scope.Escape(Nan::Undefined());
  }
  grpc_slice *slice = new grpc_slice;
  *slice = grpc_byte_buffer_reader_readall(&reader);
  grpc_byte_buffer_reader_destroy(&reader);
  char *result = reinterpret_cast<char *>(GRPC_SLICE_START_PTR(*slice));
  size_t length = GRPC_SLICE_LENGTH(*slice);
  Local<Value> buf =
      Nan::NewBuffer(result, length, delete_buffer, slice).ToLocalChecked();
  return scope.Escape(buf);
}

}  // namespace node
}  // namespace grpc
