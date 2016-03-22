/*
 *
 * Copyright 2015-2016, Google Inc.
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

#include <node.h>
#include <nan.h>
#include "grpc/grpc.h"
#include "grpc/byte_buffer_reader.h"
#include "grpc/support/slice.h"

#include "byte_buffer.h"

namespace grpc {
namespace node {


using v8::Context;
using v8::Function;
using v8::Local;
using v8::Object;
using v8::Number;
using v8::Value;

grpc_byte_buffer *BufferToByteBuffer(Local<Value> buffer) {
  Nan::HandleScope scope;
  int length = ::node::Buffer::Length(buffer);
  char *data = ::node::Buffer::Data(buffer);
  gpr_slice slice = gpr_slice_malloc(length);
  memcpy(GPR_SLICE_START_PTR(slice), data, length);
  grpc_byte_buffer *byte_buffer(grpc_raw_byte_buffer_create(&slice, 1));
  gpr_slice_unref(slice);
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
  size_t length = grpc_byte_buffer_length(buffer);
  char *result = new char[length];
  size_t offset = 0;
  grpc_byte_buffer_reader reader;
  grpc_byte_buffer_reader_init(&reader, buffer);
  gpr_slice next;
  while (grpc_byte_buffer_reader_next(&reader, &next) != 0) {
    memcpy(result + offset, GPR_SLICE_START_PTR(next), GPR_SLICE_LENGTH(next));
    offset += GPR_SLICE_LENGTH(next);
    gpr_slice_unref(next);
  }
  return scope.Escape(MakeFastBuffer(
      Nan::NewBuffer(result, length, delete_buffer, NULL).ToLocalChecked()));
}

Local<Value> MakeFastBuffer(Local<Value> slowBuffer) {
  Nan::EscapableHandleScope scope;
  Local<Object> globalObj = Nan::GetCurrentContext()->Global();
  Local<Function> bufferConstructor = Local<Function>::Cast(
      globalObj->Get(Nan::New("Buffer").ToLocalChecked()));
  Local<Value> consArgs[3] = {
    slowBuffer,
    Nan::New<Number>(::node::Buffer::Length(slowBuffer)),
    Nan::New<Number>(0)
  };
  Local<Object> fastBuffer = bufferConstructor->NewInstance(3, consArgs);
  return scope.Escape(fastBuffer);
}
}  // namespace node
}  // namespace grpc
