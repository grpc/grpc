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

#ifndef NET_GRPC_NODE_SERVER_H_
#define NET_GRPC_NODE_SERVER_H_

#include <nan.h>
#include <node.h>
#include "grpc/grpc.h"

namespace grpc {
namespace node {

/* Wraps grpc_server as a JavaScript object. Provides a constructor
   and wrapper methods for grpc_server_create, grpc_server_request_call,
   grpc_server_add_http2_port, and grpc_server_start. */
class Server : public Nan::ObjectWrap {
 public:
  /* Initializes the Server class and exposes the constructor and
     wrapper methods to JavaScript */
  static void Init(v8::Local<v8::Object> exports);
  /* Tests whether the given value was constructed by this class's
     JavaScript constructor */
  static bool HasInstance(v8::Local<v8::Value> val);

  void DestroyWrappedServer();

 private:
  explicit Server(grpc_server *server);
  ~Server();

  // Prevent copying
  Server(const Server &);
  Server &operator=(const Server &);

  void ShutdownServer();

  static NAN_METHOD(New);
  static NAN_METHOD(RequestCall);
  static NAN_METHOD(AddHttp2Port);
  static NAN_METHOD(Start);
  static NAN_METHOD(TryShutdown);
  static NAN_METHOD(ForceShutdown);
  static Nan::Callback *constructor;
  static Nan::Persistent<v8::FunctionTemplate> fun_tpl;

  grpc_server *wrapped_server;
  grpc_completion_queue *shutdown_queue;
};

}  // namespace node
}  // namespace grpc

#endif  // NET_GRPC_NODE_SERVER_H_
