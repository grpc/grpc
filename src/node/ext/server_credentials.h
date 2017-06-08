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

#ifndef NET_GRPC_NODE_SERVER_CREDENTIALS_H_
#define NET_GRPC_NODE_SERVER_CREDENTIALS_H_

#include <nan.h>
#include <node.h>
#include "grpc/grpc.h"
#include "grpc/grpc_security.h"

namespace grpc {
namespace node {

/* Wrapper class for grpc_server_credentials structs */
class ServerCredentials : public Nan::ObjectWrap {
 public:
  static void Init(v8::Local<v8::Object> exports);
  static bool HasInstance(v8::Local<v8::Value> val);
  /* Wrap a grpc_server_credentials struct in a javascript object */
  static v8::Local<v8::Value> WrapStruct(grpc_server_credentials *credentials);

  /* Returns the grpc_server_credentials struct that this object wraps */
  grpc_server_credentials *GetWrappedServerCredentials();

 private:
  explicit ServerCredentials(grpc_server_credentials *credentials);
  ~ServerCredentials();

  // Prevent copying
  ServerCredentials(const ServerCredentials &);
  ServerCredentials &operator=(const ServerCredentials &);

  static NAN_METHOD(New);
  static NAN_METHOD(CreateSsl);
  static NAN_METHOD(CreateInsecure);
  static Nan::Callback *constructor;
  // Used for typechecking instances of this javascript class
  static Nan::Persistent<v8::FunctionTemplate> fun_tpl;

  grpc_server_credentials *wrapped_credentials;
};

}  // namespace node
}  // namespace grpc

#endif  // NET_GRPC_NODE_SERVER_CREDENTIALS_H_
