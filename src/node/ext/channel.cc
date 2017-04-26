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
#include "completion_queue.h"
#include "channel_credentials.h"
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

using v8::Array;
using v8::Exception;
using v8::Function;
using v8::FunctionTemplate;
using v8::Integer;
using v8::Local;
using v8::Number;
using v8::Object;
using v8::String;
using v8::Value;

Callback *Channel::constructor;
Persistent<FunctionTemplate> Channel::fun_tpl;

bool ParseChannelArgs(Local<Value> args_val,
                      grpc_channel_args **channel_args_ptr) {
  if (args_val->IsUndefined() || args_val->IsNull()) {
    *channel_args_ptr = NULL;
    return true;
  }
  if (!args_val->IsObject()) {
    *channel_args_ptr = NULL;
    return false;
  }
  grpc_channel_args *channel_args = reinterpret_cast<grpc_channel_args*>(
      malloc(sizeof(grpc_channel_args)));
  *channel_args_ptr = channel_args;
  Local<Object> args_hash = Nan::To<Object>(args_val).ToLocalChecked();
  Local<Array> keys = Nan::GetOwnPropertyNames(args_hash).ToLocalChecked();
  channel_args->num_args = keys->Length();
  channel_args->args = reinterpret_cast<grpc_arg *>(
      calloc(channel_args->num_args, sizeof(grpc_arg)));
  for (unsigned int i = 0; i < channel_args->num_args; i++) {
    Local<Value> key = Nan::Get(keys, i).ToLocalChecked();
    Utf8String key_str(key);
    if (*key_str == NULL) {
      // Key string onversion failed
      return false;
    }
    Local<Value> value = Nan::Get(args_hash, key).ToLocalChecked();
    if (value->IsInt32()) {
      channel_args->args[i].type = GRPC_ARG_INTEGER;
      channel_args->args[i].value.integer = Nan::To<int32_t>(value).FromJust();
    } else if (value->IsString()) {
      Utf8String val_str(value);
      channel_args->args[i].type = GRPC_ARG_STRING;
      channel_args->args[i].value.string = reinterpret_cast<char*>(
          calloc(val_str.length() + 1,sizeof(char)));
      memcpy(channel_args->args[i].value.string,
             *val_str, val_str.length() + 1);
    } else {
      // The value does not match either of the accepted types
      return false;
    }
    channel_args->args[i].key = reinterpret_cast<char*>(
        calloc(key_str.length() + 1, sizeof(char)));
    memcpy(channel_args->args[i].key, *key_str, key_str.length() + 1);
  }
  return true;
}

void DeallocateChannelArgs(grpc_channel_args *channel_args) {
  if (channel_args == NULL) {
    return;
  }
  for (size_t i = 0; i < channel_args->num_args; i++) {
    if (channel_args->args[i].key == NULL) {
      /* NULL key implies that this argument and all subsequent arguments failed
       * to parse */
      break;
    }
    free(channel_args->args[i].key);
    if (channel_args->args[i].type == GRPC_ARG_STRING) {
      free(channel_args->args[i].value.string);
    }
  }
  free(channel_args->args);
  free(channel_args);
}

Channel::Channel(grpc_channel *channel) : wrapped_channel(channel) {}

Channel::~Channel() {
  gpr_log(GPR_DEBUG, "Destroying channel");
  if (wrapped_channel != NULL) {
    grpc_channel_destroy(wrapped_channel);
  }
}

void Channel::Init(Local<Object> exports) {
  Nan::HandleScope scope;
  Local<FunctionTemplate> tpl = Nan::New<FunctionTemplate>(New);
  tpl->SetClassName(Nan::New("Channel").ToLocalChecked());
  tpl->InstanceTemplate()->SetInternalFieldCount(1);
  Nan::SetPrototypeMethod(tpl, "close", Close);
  Nan::SetPrototypeMethod(tpl, "getTarget", GetTarget);
  Nan::SetPrototypeMethod(tpl, "getConnectivityState", GetConnectivityState);
  Nan::SetPrototypeMethod(tpl, "watchConnectivityState",
                          WatchConnectivityState);
  fun_tpl.Reset(tpl);
  Local<Function> ctr = Nan::GetFunction(tpl).ToLocalChecked();
  Nan::Set(exports, Nan::New("Channel").ToLocalChecked(), ctr);
  constructor = new Callback(ctr);
}

bool Channel::HasInstance(Local<Value> val) {
  HandleScope scope;
  return Nan::New(fun_tpl)->HasInstance(val);
}

grpc_channel *Channel::GetWrappedChannel() { return this->wrapped_channel; }

NAN_METHOD(Channel::New) {
  if (info.IsConstructCall()) {
    if (!info[0]->IsString()) {
      return Nan::ThrowTypeError(
          "Channel expects a string, a credential and an object");
    }
    grpc_channel *wrapped_channel;
    // Owned by the Channel object
    Utf8String host(info[0]);
    grpc_channel_credentials *creds;
    if (!ChannelCredentials::HasInstance(info[1])) {
      return Nan::ThrowTypeError(
          "Channel's second argument must be a ChannelCredentials");
    }
    ChannelCredentials *creds_object = ObjectWrap::Unwrap<ChannelCredentials>(
        Nan::To<Object>(info[1]).ToLocalChecked());
    creds = creds_object->GetWrappedCredentials();
    grpc_channel_args *channel_args_ptr = NULL;
    if (!ParseChannelArgs(info[2], &channel_args_ptr)) {
      DeallocateChannelArgs(channel_args_ptr);
      return Nan::ThrowTypeError("Channel options must be an object with "
                                 "string keys and integer or string values");
    }
    if (creds == NULL) {
      wrapped_channel = grpc_insecure_channel_create(*host, channel_args_ptr,
                                                     NULL);
    } else {
      wrapped_channel =
          grpc_secure_channel_create(creds, *host, channel_args_ptr, NULL);
    }
    DeallocateChannelArgs(channel_args_ptr);
    Channel *channel = new Channel(wrapped_channel);
    channel->Wrap(info.This());
    info.GetReturnValue().Set(info.This());
    return;
  } else {
    const int argc = 3;
    Local<Value> argv[argc] = {info[0], info[1], info[2]};
    MaybeLocal<Object> maybe_instance = Nan::NewInstance(
        constructor->GetFunction(), argc, argv);
    if (maybe_instance.IsEmpty()) {
      // There's probably a pending exception
      return;
    } else {
      info.GetReturnValue().Set(maybe_instance.ToLocalChecked());
    }
  }
}

NAN_METHOD(Channel::Close) {
  if (!HasInstance(info.This())) {
    return Nan::ThrowTypeError("close can only be called on Channel objects");
  }
  Channel *channel = ObjectWrap::Unwrap<Channel>(info.This());
  if (channel->wrapped_channel != NULL) {
    grpc_channel_destroy(channel->wrapped_channel);
    channel->wrapped_channel = NULL;
  }
}

NAN_METHOD(Channel::GetTarget) {
  if (!HasInstance(info.This())) {
    return Nan::ThrowTypeError("getTarget can only be called on Channel objects");
  }
  Channel *channel = ObjectWrap::Unwrap<Channel>(info.This());
  info.GetReturnValue().Set(Nan::New(
      grpc_channel_get_target(channel->wrapped_channel)).ToLocalChecked());
}

NAN_METHOD(Channel::GetConnectivityState) {
  if (!HasInstance(info.This())) {
    return Nan::ThrowTypeError(
        "getConnectivityState can only be called on Channel objects");
  }
  Channel *channel = ObjectWrap::Unwrap<Channel>(info.This());
  int try_to_connect = (int)info[0]->Equals(Nan::True());
  info.GetReturnValue().Set(
      grpc_channel_check_connectivity_state(channel->wrapped_channel,
                                            try_to_connect));
}

NAN_METHOD(Channel::WatchConnectivityState) {
  if (!HasInstance(info.This())) {
    return Nan::ThrowTypeError(
        "watchConnectivityState can only be called on Channel objects");
  }
  if (!info[0]->IsUint32()) {
    return Nan::ThrowTypeError(
        "watchConnectivityState's first argument must be a channel state");
  }
  if (!(info[1]->IsNumber() || info[1]->IsDate())) {
    return Nan::ThrowTypeError(
        "watchConnectivityState's second argument must be a date or a number");
  }
  if (!info[2]->IsFunction()) {
    return Nan::ThrowTypeError(
        "watchConnectivityState's third argument must be a callback");
  }
  grpc_connectivity_state last_state =
      static_cast<grpc_connectivity_state>(
          Nan::To<uint32_t>(info[0]).FromJust());
  double deadline = Nan::To<double>(info[1]).FromJust();
  Local<Function> callback_func = info[2].As<Function>();
  Nan::Callback *callback = new Callback(callback_func);
  Channel *channel = ObjectWrap::Unwrap<Channel>(info.This());
  unique_ptr<OpVec> ops(new OpVec());
  grpc_channel_watch_connectivity_state(
      channel->wrapped_channel, last_state, MillisecondsToTimespec(deadline),
      GetCompletionQueue(),
      new struct tag(callback,
                     ops.release(), NULL, Nan::Null()));
  CompletionQueueNext();
}

}  // namespace node
}  // namespace grpc
