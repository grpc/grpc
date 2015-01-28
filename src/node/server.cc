/*
 *
 * Copyright 2014, Google Inc.
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

#include "server.h"

#include <node.h>
#include <nan.h>

#include <malloc.h>

#include <vector>
#include "grpc/grpc.h"
#include "grpc/grpc_security.h"
#include "call.h"
#include "completion_queue_async_worker.h"
#include "tag.h"
#include "server_credentials.h"

namespace grpc {
namespace node {

using v8::Arguments;
using v8::Array;
using v8::Boolean;
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

Persistent<Function> Server::constructor;
Persistent<FunctionTemplate> Server::fun_tpl;

Server::Server(grpc_server *server) : wrapped_server(server) {}

Server::~Server() { grpc_server_destroy(wrapped_server); }

void Server::Init(Handle<Object> exports) {
  NanScope();
  Local<FunctionTemplate> tpl = FunctionTemplate::New(New);
  tpl->SetClassName(String::NewSymbol("Server"));
  tpl->InstanceTemplate()->SetInternalFieldCount(1);
  NanSetPrototypeTemplate(tpl, "requestCall",
                          FunctionTemplate::New(RequestCall)->GetFunction());

  NanSetPrototypeTemplate(tpl, "addHttp2Port",
                          FunctionTemplate::New(AddHttp2Port)->GetFunction());

  NanSetPrototypeTemplate(
      tpl, "addSecureHttp2Port",
      FunctionTemplate::New(AddSecureHttp2Port)->GetFunction());

  NanSetPrototypeTemplate(tpl, "start",
                          FunctionTemplate::New(Start)->GetFunction());

  NanSetPrototypeTemplate(tpl, "shutdown",
                          FunctionTemplate::New(Shutdown)->GetFunction());

  NanAssignPersistent(fun_tpl, tpl);
  NanAssignPersistent(constructor, tpl->GetFunction());
  exports->Set(String::NewSymbol("Server"), constructor);
}

bool Server::HasInstance(Handle<Value> val) {
  return NanHasInstance(fun_tpl, val);
}

NAN_METHOD(Server::New) {
  NanScope();

  /* If this is not a constructor call, make a constructor call and return
     the result */
  if (!args.IsConstructCall()) {
    const int argc = 1;
    Local<Value> argv[argc] = {args[0]};
    NanReturnValue(constructor->NewInstance(argc, argv));
  }
  grpc_server *wrapped_server;
  grpc_completion_queue *queue = CompletionQueueAsyncWorker::GetQueue();
  if (args[0]->IsUndefined()) {
    wrapped_server = grpc_server_create(queue, NULL);
  } else if (args[0]->IsObject()) {
    grpc_server_credentials *creds = NULL;
    Handle<Object> args_hash(args[0]->ToObject()->Clone());
    if (args_hash->HasOwnProperty(NanNew("credentials"))) {
      Handle<Value> creds_value = args_hash->Get(NanNew("credentials"));
      if (!ServerCredentials::HasInstance(creds_value)) {
        return NanThrowTypeError(
            "credentials arg must be a ServerCredentials object");
      }
      ServerCredentials *creds_object =
          ObjectWrap::Unwrap<ServerCredentials>(creds_value->ToObject());
      creds = creds_object->GetWrappedServerCredentials();
      args_hash->Delete(NanNew("credentials"));
    }
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
    if (creds == NULL) {
      wrapped_server = grpc_server_create(queue, &channel_args);
    } else {
      wrapped_server = grpc_secure_server_create(creds, queue, &channel_args);
    }
    free(channel_args.args);
  } else {
    return NanThrowTypeError("Server expects an object");
  }
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
  grpc_call_error error = grpc_server_request_call(
      server->wrapped_server, CreateTag(args[0], NanNull()));
  if (error == GRPC_CALL_OK) {
    CompletionQueueAsyncWorker::Next();
  } else {
    return NanThrowError("requestCall failed", error);
  }
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
    return NanThrowTypeError("addSecureHttp2Port's argument must be a String");
  }
  Server *server = ObjectWrap::Unwrap<Server>(args.This());
  NanReturnValue(NanNew<Number>(grpc_server_add_secure_http2_port(
      server->wrapped_server, *NanUtf8String(args[0]))));
}

NAN_METHOD(Server::Start) {
  NanScope();
  if (!HasInstance(args.This())) {
    return NanThrowTypeError("start can only be called on a Server");
  }
  Server *server = ObjectWrap::Unwrap<Server>(args.This());
  grpc_server_start(server->wrapped_server);
  NanReturnUndefined();
}

NAN_METHOD(Server::Shutdown) {
  NanScope();
  if (!HasInstance(args.This())) {
    return NanThrowTypeError("shutdown can only be called on a Server");
  }
  Server *server = ObjectWrap::Unwrap<Server>(args.This());
  grpc_server_shutdown(server->wrapped_server);
  NanReturnUndefined();
}

}  // namespace node
}  // namespace grpc
