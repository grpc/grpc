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
#include "completion_queue.h"
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
using v8::External;
using v8::Function;
using v8::FunctionTemplate;
using v8::Local;
using v8::Number;
using v8::Object;
using v8::String;
using v8::Value;

Nan::Callback *Server::constructor;
Persistent<FunctionTemplate> Server::fun_tpl;

static Callback *shutdown_callback;

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
  bool IsFinalOp() {
    return false;
  }

  grpc_call *call;
  grpc_call_details details;
  grpc_metadata_array request_metadata;

 protected:
  std::string GetTypeString() const { return "new_call"; }
};

class ServerShutdownOp : public Op {
 public:
  ServerShutdownOp(grpc_server *server): server(server) {
  }

  ~ServerShutdownOp() {
  }

  Local<Value> GetNodeValue() const {
    return Nan::New<External>(reinterpret_cast<void *>(server));
  }

  bool ParseOp(Local<Value> value, grpc_op *out,
               shared_ptr<Resources> resources) {
    return true;
  }
  bool IsFinalOp() {
    return false;
  }

  grpc_server *server;

 protected:
  std::string GetTypeString() const { return "shutdown"; }
};

NAN_METHOD(ServerShutdownCallback) {
  if (!info[0]->IsNull()) {
    return Nan::ThrowError("forceShutdown failed somehow");
  }
  MaybeLocal<Object> maybe_result = Nan::To<Object>(info[1]);
  Local<Object> result = maybe_result.ToLocalChecked();
  Local<Value> server_val = Nan::Get(
      result, Nan::New("shutdown").ToLocalChecked()).ToLocalChecked();
  Local<External> server_extern = server_val.As<External>();
  grpc_server *server = reinterpret_cast<grpc_server *>(server_extern->Value());
  grpc_server_destroy(server);
}

Server::Server(grpc_server *server) : wrapped_server(server) {
}

Server::~Server() {
  this->ShutdownServer();
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

  Local<FunctionTemplate>callback_tpl =
      Nan::New<FunctionTemplate>(ServerShutdownCallback);
  shutdown_callback = new Callback(
      Nan::GetFunction(callback_tpl).ToLocalChecked());
}

bool Server::HasInstance(Local<Value> val) {
  HandleScope scope;
  return Nan::New(fun_tpl)->HasInstance(val);
}

void Server::ShutdownServer() {
  if (this->wrapped_server != NULL) {
    ServerShutdownOp *op = new ServerShutdownOp(this->wrapped_server);
    unique_ptr<OpVec> ops(new OpVec());
    ops->push_back(unique_ptr<Op>(op));

    grpc_server_shutdown_and_notify(
        this->wrapped_server, GetCompletionQueue(),
        new struct tag(new Callback(**shutdown_callback), ops.release(),
                       shared_ptr<Resources>(nullptr), NULL));
    grpc_server_cancel_all_calls(this->wrapped_server);
    CompletionQueueNext();
    this->wrapped_server = NULL;
  }
}

NAN_METHOD(Server::New) {
  /* If this is not a constructor call, make a constructor call and return
     the result */
  if (!info.IsConstructCall()) {
    const int argc = 1;
    Local<Value> argv[argc] = {info[0]};
    MaybeLocal<Object> maybe_instance =
        Nan::NewInstance(constructor->GetFunction(), argc, argv);
    if (maybe_instance.IsEmpty()) {
      // There's probably a pending exception
      return;
    } else {
      info.GetReturnValue().Set(maybe_instance.ToLocalChecked());
      return;
    }
  }
  grpc_server *wrapped_server;
  grpc_completion_queue *queue = GetCompletionQueue();
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
      GetCompletionQueue(),
      GetCompletionQueue(),
      new struct tag(new Callback(info[0].As<Function>()), ops.release(),
                     shared_ptr<Resources>(nullptr), NULL));
  if (error != GRPC_CALL_OK) {
    return Nan::ThrowError(nanErrorWithCode("requestCall failed", error));
  }
  CompletionQueueNext();
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
      server->wrapped_server, GetCompletionQueue(),
      new struct tag(new Nan::Callback(info[0].As<Function>()), ops.release(),
                     shared_ptr<Resources>(nullptr), NULL));
  CompletionQueueNext();
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
