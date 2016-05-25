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

#include <memory>

#include "server.h"

#include <nan.h>
#include <node.h>

#include <vector>
#include "call.h"
#include "completion_queue_async_worker.h"
#include "grpc/grpc.h"
#include "grpc/grpc_security.h"
#include "grpc/support/log.h"
#include "server_credentials.h"
#include "timeval.h"

namespace grpc {
namespace node {

using Nan::Callback;
using Nan::EscapableHandleScope;
using Nan::HandleScope;
using Nan::Maybe;
using Nan::MaybeLocal;
using Nan::ObjectWrap;
using Nan::Persistent;
using Nan::Utf8String;

using std::unique_ptr;
using v8::Array;
using v8::Boolean;
using v8::Date;
using v8::Exception;
using v8::Function;
using v8::FunctionTemplate;
using v8::Local;
using v8::Number;
using v8::Object;
using v8::String;
using v8::Value;

Nan::Callback *Server::constructor;
Persistent<FunctionTemplate> Server::fun_tpl;

class NewCallOp : public Op {
 public:
  NewCallOp() {
    call = NULL;
    grpc_call_details_init(&details);
    grpc_metadata_array_init(&request_metadata);
  }

  ~NewCallOp() {
    grpc_call_details_destroy(&details);
    grpc_metadata_array_destroy(&request_metadata);
  }

  Local<Value> GetNodeValue() const {
    Nan::EscapableHandleScope scope;
    if (call == NULL) {
      return scope.Escape(Nan::Null());
    }
    Local<Object> obj = Nan::New<Object>();
    Nan::Set(obj, Nan::New("call").ToLocalChecked(), Call::WrapStruct(call));
    Nan::Set(obj, Nan::New("method").ToLocalChecked(),
             Nan::New(details.method).ToLocalChecked());
    Nan::Set(obj, Nan::New("host").ToLocalChecked(),
             Nan::New(details.host).ToLocalChecked());
    Nan::Set(obj, Nan::New("deadline").ToLocalChecked(),
             Nan::New<Date>(TimespecToMilliseconds(details.deadline))
                 .ToLocalChecked());
    Nan::Set(obj, Nan::New("metadata").ToLocalChecked(),
             ParseMetadata(&request_metadata));
    return scope.Escape(obj);
  }

  bool ParseOp(Local<Value> value, grpc_op *out,
               shared_ptr<Resources> resources) {
    return true;
  }

  grpc_call *call;
  grpc_call_details details;
  grpc_metadata_array request_metadata;

 protected:
  std::string GetTypeString() const { return "new_call"; }
};

Server::Server(grpc_server *server) : wrapped_server(server) {
  shutdown_queue = grpc_completion_queue_create(NULL);
  grpc_server_register_non_listening_completion_queue(server, shutdown_queue,
                                                      NULL);
}

Server::~Server() {
  this->ShutdownServer();
  grpc_completion_queue_shutdown(this->shutdown_queue);
  grpc_server_destroy(this->wrapped_server);
  grpc_completion_queue_destroy(this->shutdown_queue);
}

void Server::Init(Local<Object> exports) {
  HandleScope scope;
  Local<FunctionTemplate> tpl = Nan::New<FunctionTemplate>(New);
  tpl->SetClassName(Nan::New("Server").ToLocalChecked());
  tpl->InstanceTemplate()->SetInternalFieldCount(1);
  Nan::SetPrototypeMethod(tpl, "requestCall", RequestCall);
  Nan::SetPrototypeMethod(tpl, "addHttp2Port", AddHttp2Port);
  Nan::SetPrototypeMethod(tpl, "start", Start);
  Nan::SetPrototypeMethod(tpl, "tryShutdown", TryShutdown);
  Nan::SetPrototypeMethod(tpl, "forceShutdown", ForceShutdown);
  fun_tpl.Reset(tpl);
  Local<Function> ctr = Nan::GetFunction(tpl).ToLocalChecked();
  Nan::Set(exports, Nan::New("Server").ToLocalChecked(), ctr);
  constructor = new Callback(ctr);
}

bool Server::HasInstance(Local<Value> val) {
  HandleScope scope;
  return Nan::New(fun_tpl)->HasInstance(val);
}

void Server::ShutdownServer() {
  grpc_server_shutdown_and_notify(this->wrapped_server, this->shutdown_queue,
                                  NULL);
  grpc_server_cancel_all_calls(this->wrapped_server);
  grpc_completion_queue_pluck(this->shutdown_queue, NULL,
                              gpr_inf_future(GPR_CLOCK_REALTIME), NULL);
}

NAN_METHOD(Server::New) {
  /* If this is not a constructor call, make a constructor call and return
     the result */
  if (!info.IsConstructCall()) {
    const int argc = 1;
    Local<Value> argv[argc] = {info[0]};
    MaybeLocal<Object> maybe_instance =
        constructor->GetFunction()->NewInstance(argc, argv);
    if (maybe_instance.IsEmpty()) {
      // There's probably a pending exception
      return;
    } else {
      info.GetReturnValue().Set(maybe_instance.ToLocalChecked());
      return;
    }
  }
  grpc_server *wrapped_server;
  grpc_completion_queue *queue = CompletionQueueAsyncWorker::GetQueue();
  grpc_channel_args *channel_args;
  if (!ParseChannelArgs(info[0], &channel_args)) {
    DeallocateChannelArgs(channel_args);
    return Nan::ThrowTypeError(
        "Server options must be an object with "
        "string keys and integer or string values");
  }
  wrapped_server = grpc_server_create(channel_args, NULL);
  DeallocateChannelArgs(channel_args);
  grpc_server_register_completion_queue(wrapped_server, queue, NULL);
  Server *server = new Server(wrapped_server);
  server->Wrap(info.This());
  info.GetReturnValue().Set(info.This());
}

NAN_METHOD(Server::RequestCall) {
  if (!HasInstance(info.This())) {
    return Nan::ThrowTypeError("requestCall can only be called on a Server");
  }
  Server *server = ObjectWrap::Unwrap<Server>(info.This());
  NewCallOp *op = new NewCallOp();
  unique_ptr<OpVec> ops(new OpVec());
  ops->push_back(unique_ptr<Op>(op));
  grpc_call_error error = grpc_server_request_call(
      server->wrapped_server, &op->call, &op->details, &op->request_metadata,
      CompletionQueueAsyncWorker::GetQueue(),
      CompletionQueueAsyncWorker::GetQueue(),
      new struct tag(new Callback(info[0].As<Function>()), ops.release(),
                     shared_ptr<Resources>(nullptr)));
  if (error != GRPC_CALL_OK) {
    return Nan::ThrowError(nanErrorWithCode("requestCall failed", error));
  }
  CompletionQueueAsyncWorker::Next();
}

NAN_METHOD(Server::AddHttp2Port) {
  if (!HasInstance(info.This())) {
    return Nan::ThrowTypeError("addHttp2Port can only be called on a Server");
  }
  if (!info[0]->IsString()) {
    return Nan::ThrowTypeError(
        "addHttp2Port's first argument must be a String");
  }
  if (!ServerCredentials::HasInstance(info[1])) {
    return Nan::ThrowTypeError(
        "addHttp2Port's second argument must be ServerCredentials");
  }
  Server *server = ObjectWrap::Unwrap<Server>(info.This());
  ServerCredentials *creds_object = ObjectWrap::Unwrap<ServerCredentials>(
      Nan::To<Object>(info[1]).ToLocalChecked());
  grpc_server_credentials *creds = creds_object->GetWrappedServerCredentials();
  int port;
  if (creds == NULL) {
    port = grpc_server_add_insecure_http2_port(server->wrapped_server,
                                               *Utf8String(info[0]));
  } else {
    port = grpc_server_add_secure_http2_port(server->wrapped_server,
                                             *Utf8String(info[0]), creds);
  }
  info.GetReturnValue().Set(Nan::New<Number>(port));
}

NAN_METHOD(Server::Start) {
  Nan::HandleScope scope;
  if (!HasInstance(info.This())) {
    return Nan::ThrowTypeError("start can only be called on a Server");
  }
  Server *server = ObjectWrap::Unwrap<Server>(info.This());
  grpc_server_start(server->wrapped_server);
}

NAN_METHOD(Server::TryShutdown) {
  Nan::HandleScope scope;
  if (!HasInstance(info.This())) {
    return Nan::ThrowTypeError("tryShutdown can only be called on a Server");
  }
  Server *server = ObjectWrap::Unwrap<Server>(info.This());
  unique_ptr<OpVec> ops(new OpVec());
  grpc_server_shutdown_and_notify(
      server->wrapped_server, CompletionQueueAsyncWorker::GetQueue(),
      new struct tag(new Nan::Callback(info[0].As<Function>()), ops.release(),
                     shared_ptr<Resources>(nullptr)));
  CompletionQueueAsyncWorker::Next();
}

NAN_METHOD(Server::ForceShutdown) {
  Nan::HandleScope scope;
  if (!HasInstance(info.This())) {
    return Nan::ThrowTypeError("forceShutdown can only be called on a Server");
  }
  Server *server = ObjectWrap::Unwrap<Server>(info.This());
  server->ShutdownServer();
}

}  // namespace node
}  // namespace grpc
