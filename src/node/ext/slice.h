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
#include <nan.h>
#include <node.h>

namespace grpc {
namespace node {

typedef Nan::Persistent<v8::Value, Nan::CopyablePersistentTraits<v8::Value>>
    PersistentValue;

grpc_slice CreateSliceFromString(const v8::Local<v8::String> source);

grpc_slice CreateSliceFromBuffer(const v8::Local<v8::Value> source);

v8::Local<v8::String> CopyStringFromSlice(const grpc_slice slice);

v8::Local<v8::Value> CreateBufferFromSlice(const grpc_slice slice);

}  // namespace node
}  // namespace grpc
