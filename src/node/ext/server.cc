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

#include <node.h>
#include <nan.h>

#include <vector>
#include "grpc/grpc.h"
#include "grpc/grpc_security.h"
#include "grpc/support/log.h"
#include "call.h"
#include "completion_queue_async_worker.h"
#include "server_credentials.h"
#include "timeval.h"

namespace grpc {
namespace node {

using std::unique_ptr;
using v8::Array;
using v8::Boolean;
using v8::Date;
using v8::Exception;
using v8::Function;
using v8::FunctionTemplate;
using v8::Handle;
using v8::HandleScope;
using v8::Local;
using v8::Number;
using v8::Object;
using v8::Persistent;
using v8::String;
using v8::Value;

NanCallback *Server::constructor;
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

  Handle<Value> GetNodeValue() const {
    NanEscapableScope();
    if (call == NULL) {
      return NanEscapeScope(NanNull());
    }
    Handle<Object> obj = NanNew<Object>();
    obj->Set(NanNew("call"), Call::WrapStruct(call));
    obj->Set(NanNew("method"), NanNew(details.method));
    obj->Set(NanNew("host"), NanNew(details.host));
    obj->Set(NanNew("deadline"),
             NanNew<Date>(TimespecToMilliseconds(details.deadline)));
    obj->Set(NanNew("metadata"), ParseMetadata(&request_metadata));
    return NanEscapeScope(obj);
  }

  bool ParseOp(Handle<Value> value, grpc_op *out,
               shared_ptr<Resources> resources) {
    return true;
  }

  grpc_call *call;
  grpc_call_details details;
  grpc_metadata_array request_metadata;

 protected:
  std::string GetTypeString() const {
    return "new call";
  }
};

Server::Server(grpc_server *server) : wrapped_server(server) {
  shutdown_queue = grpc_completion_queue_create();
  grpc_server_register_completion_queue(server, shutdown_queue);
}

Server::~Server() {
  this->ShutdownServer();
  grpc_completion_queue_shutdown(this->shutdown_queue);
  grpc_server_destroy(wrapped_server);
  grpc_completion_queue_destroy(this->shutdown_queue);
}

void Server::Init(Handle<Object> exports) {
  NanScope();
  Local<FunctionTemplate> tpl = NanNew<FunctionTemplate>(New);
  tpl->SetClassName(NanNew("Server"));
  tpl->InstanceTemplate()->SetInternalFieldCount(1);
  NanSetPrototypeTemplate(tpl, "requestCall",
                          NanNew<FunctionTemplate>(RequestCall)->GetFunction());

  NanSetPrototypeTemplate(
      tpl, "addHttp2Port",
      NanNew<FunctionTemplate>(AddHttp2Port)->GetFunction());

  NanSetPrototypeTemplate(
      tpl, "addSecureHttp2Port",
      NanNew<FunctionTemplate>(AddSecureHttp2Port)->GetFunction());

  NanSetPrototypeTemplate(tpl, "start",
                          NanNew<FunctionTemplate>(Start)->GetFunction());

  NanSetPrototypeTemplate(tpl, "shutdown",
                          NanNew<FunctionTemplate>(Shutdown)->GetFunction());

  NanAssignPersistent(fun_tpl, tpl);
  Handle<Function> ctr = tpl->GetFunction();
  constructor = new NanCallback(ctr);
  exports->Set(NanNew("Server"), ctr);
}

bool Server::HasInstance(Handle<Value> val) {
  return NanHasInstance(fun_tpl, val);
}

void Server::ShutdownServer() {
  if (this->wrapped_server != NULL) {
    grpc_server_shutdown_and_notify(this->wrapped_server,
                                    this->shutdown_queue,
                                    NULL);
    grpc_completion_queue_pluck(this->shutdown_queue, NULL,
                                gpr_inf_future(GPR_CLOCK_REALTIME));
    this->wrapped_server = NULL;
  }
}

NAN_METHOD(Server::New) {
  NanScope();

  /* If this is not a constructor call, make a constructor call and return
     the result */
  if (!args.IsConstructCall()) {
    const int argc = 1;
    Local<Value> argv[argc] = {args[0]};
    NanReturnValue(constructor->GetFunction()->NewInstance(argc, argv));
  }
  grpc_server *wrapped_server;
  grpc_completion_queue *queue = CompletionQueueAsyncWorker::GetQueue();
  if (args[0]->IsUndefined()) {
    wrapped_server = grpc_server_create(NULL);
  } else if (args[0]->IsObject()) {
    Handle<Object> args_hash(args[0]->ToObject());
    Handle<Array> keys(args_hash->GetOwnPropertyNames());
    grpc_channel_args channel_args;
    channel_args.num_args = keys->Length();
    channel_args.args = reinterpret_cast<grpc_arg *>(
        calloc(channel_args.num_args, sizeof(grpc_arg)));
    /* These are used to keep all strings until then end of the block, then
       destroy them */
    std::vector<NanUtf8String *> key_strings(keys->Length());
    std::vector<NanUtf8String *> value_strings(keys->Length());
    for (unsigned int i = 0; i < channel_args.num_args; i++) {
      Handle<String> current_key(keys->Get(i)->ToString());
      Handle<Value> current_value(args_hash->Get(current_key));
      key_strings[i] = new NanUtf8String(current_key);
      channel_args.args[i].key = **key_strings[i];
      if (current_value->IsInt32()) {
        channel_args.args[i].type = GRPC_ARG_INTEGER;
        channel_args.args[i].value.integer = current_value->Int32Value();
      } else if (current_value->IsString()) {
        channel_args.args[i].type = GRPC_ARG_STRING;
        value_strings[i] = new NanUtf8String(current_value);
        channel_args.args[i].value.string = **value_strings[i];
      } else {
        free(channel_args.args);
        return NanThrowTypeError("Arg values must be strings");
      }
    }
    wrapped_server = grpc_server_create(&channel_args);
    free(channel_args.args);
  } else {
    return NanThrowTypeError("Server expects an object");
  }
  grpc_server_register_completion_queue(wrapped_server, queue);
  Server *server = new Server(wrapped_server);
  server->Wrap(args.This());
  NanReturnValue(args.This());
}

NAN_METHOD(Server::RequestCall) {
  NanScope();
  if (!HasInstance(args.This())) {
    return NanThrowTypeError("requestCall can only be called on a Server");
  }
  Server *server = ObjectWrap::Unwrap<Server>(args.This());
  if (server->wrapped_server == NULL) {
    return NanThrowError("requestCall cannot be called on a shut down Server");
  }
  NewCallOp *op = new NewCallOp();
  unique_ptr<OpVec> ops(new OpVec());
  ops->push_back(unique_ptr<Op>(op));
  grpc_call_error error = grpc_server_request_call(
      server->wrapped_server, &op->call, &op->details, &op->request_metadata,
      CompletionQueueAsyncWorker::GetQueue(),
      CompletionQueueAsyncWorker::GetQueue(),
      new struct tag(new NanCallback(args[0].As<Function>()), ops.release(),
                     shared_ptr<Resources>(nullptr)));
  if (error != GRPC_CALL_OK) {
    return NanThrowError("requestCall failed", error);
  }
  CompletionQueueAsyncWorker::Next();
  NanReturnUndefined();
}

NAN_METHOD(Server::AddHttp2Port) {
  NanScope();
  if (!HasInstance(args.This())) {
    return NanThrowTypeError("addHttp2Port can only be called on a Server");
  }
  if (!args[0]->IsString()) {
    return NanThrowTypeError("addHttp2Port's argument must be a String");
  }
  Server *server = ObjectWrap::Unwrap<Server>(args.This());
  if (server->wrapped_server == NULL) {
    return NanThrowError("addHttp2Port cannot be called on a shut down Server");
  }
  NanReturnValue(NanNew<Number>(grpc_server_add_http2_port(
      server->wrapped_server, *NanUtf8String(args[0]))));
}

NAN_METHOD(Server::AddSecureHttp2Port) {
  NanScope();
  if (!HasInstance(args.This())) {
    return NanThrowTypeError(
        "addSecureHttp2Port can only be called on a Server");
  }
  if (!args[0]->IsString()) {
    return NanThrowTypeError(
        "addSecureHttp2Port's first argument must be a String");
  }
  if (!ServerCredentials::HasInstance(args[1])) {
    return NanThrowTypeError(
        "addSecureHttp2Port's second argument must be ServerCredentials");
  }
  Server *server = ObjectWrap::Unwrap<Server>(args.This());
  if (server->wrapped_server == NULL) {
    return NanThrowError(
        "addSecureHttp2Port cannot be called on a shut down Server");
  }
  ServerCredentials *creds = ObjectWrap::Unwrap<ServerCredentials>(
      args[1]->ToObject());
  NanReturnValue(NanNew<Number>(grpc_server_add_secure_http2_port(
      server->wrapped_server, *NanUtf8String(args[0]),
      creds->GetWrappedServerCredentials())));
}

NAN_METHOD(Server::Start) {
  NanScope();
  if (!HasInstance(args.This())) {
    return NanThrowTypeError("start can only be called on a Server");
  }
  Server *server = ObjectWrap::Unwrap<Server>(args.This());
  if (server->wrapped_server == NULL) {
    return NanThrowError("start cannot be called on a shut down Server");
  }
  grpc_server_start(server->wrapped_server);
  NanReturnUndefined();
}

NAN_METHOD(ShutdownCallback) {
  NanReturnUndefined();
}

NAN_METHOD(Server::Shutdown) {
  NanScope();
  if (!HasInstance(args.This())) {
    return NanThrowTypeError("shutdown can only be called on a Server");
  }
  Server *server = ObjectWrap::Unwrap<Server>(args.This());
  server->ShutdownServer();
  NanReturnUndefined();
}

}  // namespace node
}  // namespace grpc
