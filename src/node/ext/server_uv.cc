/*
 *
 * Copyright 2017, Google Inc.
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

#ifdef GRPC_UV

#include "server.h"

#include <node.h>
#include <nan.h>
#include "grpc/grpc.h"
#include "grpc/support/time.h"

#include "call.h"
#include "completion_queue.h"

namespace grpc {
namespace node {

using Nan::Callback;
using Nan::MaybeLocal;

using v8::External;
using v8::Function;
using v8::FunctionTemplate;
using v8::Local;
using v8::Object;
using v8::Value;

static Callback *shutdown_callback = NULL;

class ServerShutdownOp : public Op {
 public:
  ServerShutdownOp(grpc_server *server): server(server) {
  }

  ~ServerShutdownOp() {
  }

  Local<Value> GetNodeValue() const {
    return Nan::Null();
  }

  bool ParseOp(Local<Value> value, grpc_op *out) {
    return true;
  }
  bool IsFinalOp() {
    return false;
  }
  void OnComplete(bool success) {
    /* Because cancel_all_calls was called, we assume that shutdown_and_notify
       completes successfully */
    grpc_server_destroy(server);
  }

  grpc_server *server;

 protected:
  std::string GetTypeString() const { return "shutdown"; }
};

Server::Server(grpc_server *server) : wrapped_server(server) {
}

Server::~Server() {
  this->ShutdownServer();
}

NAN_METHOD(ServerShutdownCallback) {
  if (!info[0]->IsNull()) {
    return Nan::ThrowError("forceShutdown failed somehow");
  }
}

void Server::ShutdownServer() {
  Nan::HandleScope scope;
  if (this->wrapped_server != NULL) {
    if (shutdown_callback == NULL) {
      Local<FunctionTemplate>callback_tpl =
          Nan::New<FunctionTemplate>(ServerShutdownCallback);
      shutdown_callback = new Callback(
          Nan::GetFunction(callback_tpl).ToLocalChecked());
    }

    ServerShutdownOp *op = new ServerShutdownOp(this->wrapped_server);
    unique_ptr<OpVec> ops(new OpVec());
    ops->push_back(unique_ptr<Op>(op));

    grpc_server_shutdown_and_notify(
        this->wrapped_server, GetCompletionQueue(),
        new struct tag(new Callback(**shutdown_callback), ops.release(), NULL,
                       Nan::Null()));
    grpc_server_cancel_all_calls(this->wrapped_server);
    CompletionQueueNext();
    this->wrapped_server = NULL;
  }
}

}  // namespace grpc
}  // namespace node

#endif /* GRPC_UV */
