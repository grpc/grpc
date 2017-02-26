/*
 *
 * Copyright 2015, gRPC authors
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

#include <string.h>

#include <node.h>
#include <nan.h>
#include "grpc/grpc.h"
#include "grpc/byte_buffer_reader.h"
#include "grpc/slice.h"

#include "byte_buffer.h"
#include "slice.h"

namespace grpc {
namespace node {

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
void delete_buffer(char *data, void *hint) { delete[] data; }
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
  grpc_slice slice = grpc_byte_buffer_reader_readall(&reader);
  size_t length = GRPC_SLICE_LENGTH(slice);
  char *result = new char[length];
  memcpy(result, GRPC_SLICE_START_PTR(slice), length);
  grpc_slice_unref(slice);
  return scope.Escape(MakeFastBuffer(
      Nan::NewBuffer(result, length, delete_buffer, NULL).ToLocalChecked()));
}

Local<Value> MakeFastBuffer(Local<Value> slowBuffer) {
  Nan::EscapableHandleScope scope;
  Local<Object> globalObj = Nan::GetCurrentContext()->Global();
  MaybeLocal<Value> constructorValue = Nan::Get(
      globalObj, Nan::New("Buffer").ToLocalChecked());
  Local<Function> bufferConstructor = Local<Function>::Cast(
      constructorValue.ToLocalChecked());
  const int argc = 3;
  Local<Value> consArgs[argc] = {
    slowBuffer,
    Nan::New<Number>(::node::Buffer::Length(slowBuffer)),
    Nan::New<Number>(0)
  };
  MaybeLocal<Object> fastBuffer = Nan::NewInstance(bufferConstructor,
                                                   argc, consArgs);
  return scope.Escape(fastBuffer.ToLocalChecked());
}
}  // namespace node
}  // namespace grpc
