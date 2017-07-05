/*
 *
 * Copyright 2016 gRPC authors.
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
