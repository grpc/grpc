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

#include <vector>

#include "grpc/support/log.h"

#include <node.h>
#include <nan.h>
#include "grpc/grpc.h"
#include "grpc/grpc_security.h"
#include "call.h"
#include "channel.h"
#include "completion_queue_async_worker.h"
#include "credentials.h"
#include "timeval.h"

namespace grpc {
namespace node {

using v8::Array;
using v8::Exception;
using v8::Function;
using v8::FunctionTemplate;
using v8::Handle;
using v8::HandleScope;
using v8::Integer;
using v8::Local;
using v8::Number;
using v8::Object;
using v8::Persistent;
using v8::String;
using v8::Value;

NanCallback *Channel::constructor;
Persistent<FunctionTemplate> Channel::fun_tpl;

Channel::Channel(grpc_channel *channel) : wrapped_channel(channel) {}

Channel::~Channel() {
  if (wrapped_channel != NULL) {
    grpc_channel_destroy(wrapped_channel);
  }
}

void Channel::Init(Handle<Object> exports) {
  NanScope();
  Local<FunctionTemplate> tpl = NanNew<FunctionTemplate>(New);
  tpl->SetClassName(NanNew("Channel"));
  tpl->InstanceTemplate()->SetInternalFieldCount(1);
  NanSetPrototypeTemplate(tpl, "close",
                          NanNew<FunctionTemplate>(Close)->GetFunction());
  NanSetPrototypeTemplate(tpl, "getTarget",
                          NanNew<FunctionTemplate>(GetTarget)->GetFunction());
  NanSetPrototypeTemplate(
      tpl, "getConnectivityState",
      NanNew<FunctionTemplate>(GetConnectivityState)->GetFunction());
  NanSetPrototypeTemplate(
      tpl, "watchConnectivityState",
      NanNew<FunctionTemplate>(WatchConnectivityState)->GetFunction());
  NanAssignPersistent(fun_tpl, tpl);
  Handle<Function> ctr = tpl->GetFunction();
  constructor = new NanCallback(ctr);
  exports->Set(NanNew("Channel"), ctr);
}

bool Channel::HasInstance(Handle<Value> val) {
  NanScope();
  return NanHasInstance(fun_tpl, val);
}

grpc_channel *Channel::GetWrappedChannel() { return this->wrapped_channel; }

NAN_METHOD(Channel::New) {
  NanScope();

  if (args.IsConstructCall()) {
    if (!args[0]->IsString()) {
      return NanThrowTypeError(
          "Channel expects a string, a credential and an object");
    }
    grpc_channel *wrapped_channel;
    // Owned by the Channel object
    NanUtf8String host(args[0]);
    grpc_credentials *creds;
    if (!Credentials::HasInstance(args[1])) {
      return NanThrowTypeError(
          "Channel's second argument must be a credential");
    }
    Credentials *creds_object = ObjectWrap::Unwrap<Credentials>(
        args[1]->ToObject());
    creds = creds_object->GetWrappedCredentials();
    grpc_channel_args *channel_args_ptr;
    if (args[2]->IsUndefined()) {
      channel_args_ptr = NULL;
      wrapped_channel = grpc_insecure_channel_create(*host, NULL, NULL);
    } else if (args[2]->IsObject()) {
      Handle<Object> args_hash(args[2]->ToObject()->Clone());
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
      channel_args_ptr = &channel_args;
    } else {
      return NanThrowTypeError("Channel expects a string and an object");
    }
    if (creds == NULL) {
      wrapped_channel = grpc_insecure_channel_create(*host, channel_args_ptr,
                                                     NULL);
    } else {
      wrapped_channel =
          grpc_secure_channel_create(creds, *host, channel_args_ptr, NULL);
    }
    if (channel_args_ptr != NULL) {
      free(channel_args_ptr->args);
    }
    Channel *channel = new Channel(wrapped_channel);
    channel->Wrap(args.This());
    NanReturnValue(args.This());
  } else {
    const int argc = 3;
    Local<Value> argv[argc] = {args[0], args[1], args[2]};
    NanReturnValue(constructor->GetFunction()->NewInstance(argc, argv));
  }
}

NAN_METHOD(Channel::Close) {
  NanScope();
  if (!HasInstance(args.This())) {
    return NanThrowTypeError("close can only be called on Channel objects");
  }
  Channel *channel = ObjectWrap::Unwrap<Channel>(args.This());
  if (channel->wrapped_channel != NULL) {
    grpc_channel_destroy(channel->wrapped_channel);
    channel->wrapped_channel = NULL;
  }
  NanReturnUndefined();
}

NAN_METHOD(Channel::GetTarget) {
  NanScope();
  if (!HasInstance(args.This())) {
    return NanThrowTypeError("getTarget can only be called on Channel objects");
  }
  Channel *channel = ObjectWrap::Unwrap<Channel>(args.This());
  NanReturnValue(NanNew(grpc_channel_get_target(channel->wrapped_channel)));
}

NAN_METHOD(Channel::GetConnectivityState) {
  NanScope();
  if (!HasInstance(args.This())) {
    return NanThrowTypeError(
        "getConnectivityState can only be called on Channel objects");
  }
  Channel *channel = ObjectWrap::Unwrap<Channel>(args.This());
  int try_to_connect = (int)args[0]->Equals(NanTrue());
  NanReturnValue(grpc_channel_check_connectivity_state(channel->wrapped_channel,
                                                       try_to_connect));
}

NAN_METHOD(Channel::WatchConnectivityState) {
  NanScope();
  if (!HasInstance(args.This())) {
    return NanThrowTypeError(
        "watchConnectivityState can only be called on Channel objects");
  }
  if (!args[0]->IsUint32()) {
    return NanThrowTypeError(
        "watchConnectivityState's first argument must be a channel state");
  }
  if (!(args[1]->IsNumber() || args[1]->IsDate())) {
    return NanThrowTypeError(
        "watchConnectivityState's second argument must be a date or a number");
  }
  if (!args[2]->IsFunction()) {
    return NanThrowTypeError(
        "watchConnectivityState's third argument must be a callback");
  }
  grpc_connectivity_state last_state =
      static_cast<grpc_connectivity_state>(args[0]->Uint32Value());
  double deadline = args[1]->NumberValue();
  Handle<Function> callback_func = args[2].As<Function>();
  NanCallback *callback = new NanCallback(callback_func);
  Channel *channel = ObjectWrap::Unwrap<Channel>(args.This());
  unique_ptr<OpVec> ops(new OpVec());
  grpc_channel_watch_connectivity_state(
      channel->wrapped_channel, last_state, MillisecondsToTimespec(deadline),
      CompletionQueueAsyncWorker::GetQueue(),
      new struct tag(callback,
                     ops.release(),
                     shared_ptr<Resources>(nullptr)));
  CompletionQueueAsyncWorker::Next();
  NanReturnUndefined();
}

}  // namespace node
}  // namespace grpc
