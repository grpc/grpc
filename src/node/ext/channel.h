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

#ifndef NET_GRPC_NODE_CHANNEL_H_
#define NET_GRPC_NODE_CHANNEL_H_

#include <nan.h>
#include <node.h>
#include "grpc/grpc.h"

namespace grpc {
namespace node {

bool ParseChannelArgs(v8::Local<v8::Value> args_val,
                      grpc_channel_args **channel_args_ptr);

void DeallocateChannelArgs(grpc_channel_args *channel_args);

/* Wrapper class for grpc_channel structs */
class Channel : public Nan::ObjectWrap {
 public:
  static void Init(v8::Local<v8::Object> exports);
  static bool HasInstance(v8::Local<v8::Value> val);
  /* This is used to typecheck javascript objects before converting them to
     this type */
  static v8::Persistent<v8::Value> prototype;

  /* Returns the grpc_channel struct that this object wraps */
  grpc_channel *GetWrappedChannel();

 private:
  explicit Channel(grpc_channel *channel);
  ~Channel();

  // Prevent copying
  Channel(const Channel &);
  Channel &operator=(const Channel &);

  static NAN_METHOD(New);
  static NAN_METHOD(Close);
  static NAN_METHOD(GetTarget);
  static NAN_METHOD(GetConnectivityState);
  static NAN_METHOD(WatchConnectivityState);
  static Nan::Callback *constructor;
  static Nan::Persistent<v8::FunctionTemplate> fun_tpl;

  grpc_channel *wrapped_channel;
};

}  // namespace node
}  // namespace grpc

#endif  // NET_GRPC_NODE_CHANNEL_H_
