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

#ifndef NET_GRPC_NODE_CHANNEL_CREDENTIALS_H_
#define NET_GRPC_NODE_CHANNEL_CREDENTIALS_H_

#include <nan.h>
#include <node.h>
#include "grpc/grpc.h"
#include "grpc/grpc_security.h"

namespace grpc {
namespace node {

/* Wrapper class for grpc_channel_credentials structs */
class ChannelCredentials : public Nan::ObjectWrap {
 public:
  static void Init(v8::Local<v8::Object> exports);
  static bool HasInstance(v8::Local<v8::Value> val);
  /* Wrap a grpc_channel_credentials struct in a javascript object */
  static v8::Local<v8::Value> WrapStruct(grpc_channel_credentials *credentials);

  /* Returns the grpc_channel_credentials struct that this object wraps */
  grpc_channel_credentials *GetWrappedCredentials();

 private:
  explicit ChannelCredentials(grpc_channel_credentials *credentials);
  ~ChannelCredentials();

  // Prevent copying
  ChannelCredentials(const ChannelCredentials &);
  ChannelCredentials &operator=(const ChannelCredentials &);

  static NAN_METHOD(New);
  static NAN_METHOD(CreateSsl);
  static NAN_METHOD(CreateInsecure);

  static NAN_METHOD(Compose);
  static Nan::Callback *constructor;
  // Used for typechecking instances of this javascript class
  static Nan::Persistent<v8::FunctionTemplate> fun_tpl;

  grpc_channel_credentials *wrapped_credentials;
};

}  // namespace node
}  // namespace grpc

#endif  // NET_GRPC_NODE_CHANNEL_CREDENTIALS_H_
