/*
 *
 * Copyright 2016, Google Inc.
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

#include <grpc/slice.h>
#include <grpc/support/alloc.h>
#include <nan.h>
#include <node.h>

#include "slice.h"

namespace grpc {
namespace node {

using Nan::Persistent;

using v8::Local;
using v8::String;
using v8::Value;

namespace {
void SliceFreeCallback(char *data, void *hint) {
  grpc_slice *slice = reinterpret_cast<grpc_slice *>(hint);
  grpc_slice_unref(*slice);
  delete slice;
}

void string_destroy_func(void *user_data) {
  delete reinterpret_cast<Nan::Utf8String *>(user_data);
}

void buffer_destroy_func(void *user_data) {
  delete reinterpret_cast<PersistentValue *>(user_data);
}
}  // namespace

grpc_slice CreateSliceFromString(const Local<String> source) {
  Nan::HandleScope scope;
  Nan::Utf8String *utf8_value = new Nan::Utf8String(source);
  return grpc_slice_new_with_user_data(**utf8_value, source->Length(),
                                       string_destroy_func, utf8_value);
}

grpc_slice CreateSliceFromBuffer(const Local<Value> source) {
  // Prerequisite: ::node::Buffer::HasInstance(source)
  Nan::HandleScope scope;
  return grpc_slice_new_with_user_data(
      ::node::Buffer::Data(source), ::node::Buffer::Length(source),
      buffer_destroy_func, new PersistentValue(source));
}
Local<String> CopyStringFromSlice(const grpc_slice slice) {
  Nan::EscapableHandleScope scope;
  if (GRPC_SLICE_LENGTH(slice) == 0) {
    return scope.Escape(Nan::EmptyString());
  }
  return scope.Escape(
      Nan::New<String>(const_cast<char *>(reinterpret_cast<const char *>(
                           GRPC_SLICE_START_PTR(slice))),
                       GRPC_SLICE_LENGTH(slice))
          .ToLocalChecked());
}

Local<Value> CreateBufferFromSlice(const grpc_slice slice) {
  Nan::EscapableHandleScope scope;
  grpc_slice *slice_ptr = new grpc_slice;
  *slice_ptr = grpc_slice_ref(slice);
  return scope.Escape(
      Nan::NewBuffer(
          const_cast<char *>(
              reinterpret_cast<const char *>(GRPC_SLICE_START_PTR(*slice_ptr))),
          GRPC_SLICE_LENGTH(*slice_ptr), SliceFreeCallback, slice_ptr)
          .ToLocalChecked());
}

}  // namespace node
}  // namespace grpc
