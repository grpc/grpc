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
