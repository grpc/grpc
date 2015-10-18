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

#include <node.h>

#include "grpc/grpc.h"
#include "grpc/grpc_security.h"
#include "grpc/support/log.h"
#include "call_credentials.h"
#include "call.h"

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

using v8::Exception;
using v8::External;
using v8::Function;
using v8::FunctionTemplate;
using v8::Integer;
using v8::Local;
using v8::Object;
using v8::ObjectTemplate;
using v8::Value;

Nan::Callback *CallCredentials::constructor;
Persistent<FunctionTemplate> CallCredentials::fun_tpl;

CallCredentials::CallCredentials(grpc_credentials *credentials)
    : wrapped_credentials(credentials) {}

CallCredentials::~CallCredentials() {
  grpc_credentials_release(wrapped_credentials);
}

void CallCredentials::Init(Local<Object> exports) {
  HandleScope scope;
  Local<FunctionTemplate> tpl = Nan::New<FunctionTemplate>(New);
  tpl->SetClassName(Nan::New("CallCredentials").ToLocalChecked());
  tpl->InstanceTemplate()->SetInternalFieldCount(1);
  Nan::SetPrototypeMethod(tpl, "compose", Compose);
  fun_tpl.Reset(tpl);
  Local<Function> ctr = Nan::GetFunction(tpl).ToLocalChecked();
  Nan::Set(ctr, Nan::New("createFromPlugin").ToLocalChecked(),
           Nan::GetFunction(
               Nan::New<FunctionTemplate>(CreateFromPlugin)).ToLocalChecked());
  Nan::Set(exports, Nan::New("CallCredentials").ToLocalChecked(), ctr);
  constructor = new Nan::Callback(ctr);
}

bool CallCredentials::HasInstance(Local<Value> val) {
  HandleScope scope;
  return Nan::New(fun_tpl)->HasInstance(val);
}

Local<Value> CallCredentials::WrapStruct(grpc_credentials *credentials) {
  EscapableHandleScope scope;
  const int argc = 1;
  if (credentials == NULL) {
    return scope.Escape(Nan::Null());
  }
  Local<Value> argv[argc] = {
    Nan::New<External>(reinterpret_cast<void *>(credentials))};
  MaybeLocal<Object> maybe_instance = Nan::NewInstance(
      constructor->GetFunction(), argc, argv);
  if (maybe_instance.IsEmpty()) {
    return scope.Escape(Nan::Null());
  } else {
    return scope.Escape(maybe_instance.ToLocalChecked());
  }
}

grpc_credentials *CallCredentials::GetWrappedCredentials() {
  return wrapped_credentials;
}

NAN_METHOD(CallCredentials::New) {
  if (info.IsConstructCall()) {
    if (!info[0]->IsExternal()) {
      return Nan::ThrowTypeError(
          "CallCredentials can only be created with the provided functions");
    }
    Local<External> ext = info[0].As<External>();
    grpc_credentials *creds_value =
        reinterpret_cast<grpc_credentials *>(ext->Value());
    CallCredentials *credentials = new CallCredentials(creds_value);
    credentials->Wrap(info.This());
    info.GetReturnValue().Set(info.This());
    return;
  } else {
    // This should never be called directly
    return Nan::ThrowTypeError(
        "CallCredentials can only be created with the provided functions");
  }
}

NAN_METHOD(CallCredentials::Compose) {
  if (!CallCredentials::HasInstance(info.This())) {
    return Nan::ThrowTypeError(
        "compose can only be called on CallCredentials objects");
  }
  if (!CallCredentials::HasInstance(info[0])) {
    return Nan::ThrowTypeError(
        "compose's first argument must be a CallCredentials object");
  }
  CallCredentials *self = ObjectWrap::Unwrap<CallCredentials>(info.This());
  CallCredentials *other = ObjectWrap::Unwrap<CallCredentials>(
      Nan::To<Object>(info[0]).ToLocalChecked());
  grpc_credentials *creds = grpc_composite_credentials_create(
      self->wrapped_credentials, other->wrapped_credentials, NULL);
  info.GetReturnValue().Set(WrapStruct(creds));
}



NAN_METHOD(CallCredentials::CreateFromPlugin) {
  if (!info[0]->IsFunction()) {
    return Nan::ThrowTypeError(
        "createFromPlugin's argument must be a function");
  }
  grpc_metadata_credentials_plugin plugin;
  plugin_state *state = new plugin_state;
  state->callback = new Nan::Callback(info[0].As<Function>());
  plugin.get_metadata = plugin_get_metadata;
  plugin.destroy = plugin_destroy_state;
  plugin.state = reinterpret_cast<void*>(state);
  grpc_credentials *creds = grpc_metadata_credentials_create_from_plugin(plugin,
                                                                         NULL);
  info.GetReturnValue().Set(WrapStruct(creds));
}

NAN_METHOD(PluginCallback) {
  // Arguments: status code, error details, metadata
  if (!info[0]->IsUint32()) {
    return Nan::ThrowTypeError(
        "The callback's first argument must be a status code");
  }
  if (!info[1]->IsString()) {
    return Nan::ThrowTypeError(
        "The callback's second argument must be a string");
  }
  if (!info[2]->IsObject()) {
    return Nan::ThrowTypeError(
        "The callback's third argument must be an object");
  }
  shared_ptr<Resources> resources(new Resources);
  grpc_status_code code = static_cast<grpc_status_code>(
      Nan::To<uint32_t>(info[0]).FromJust());
  char *details = *Utf8String(info[1]);
  grpc_metadata_array array;
  if (!CreateMetadataArray(Nan::To<Object>(info[2]).ToLocalChecked(),
                           &array, resources)){
    return Nan::ThrowError("Failed to parse metadata");
  }
  grpc_credentials_plugin_metadata_cb cb =
      reinterpret_cast<grpc_credentials_plugin_metadata_cb>(
          Nan::Get(info.Callee(),
                   Nan::New("cb").ToLocalChecked()
                   ).ToLocalChecked().As<External>()->Value());
  void *user_data =
      Nan::Get(info.Callee(),
               Nan::New("user_data").ToLocalChecked()
               ).ToLocalChecked().As<External>()->Value();
  cb(user_data, array.metadata, array.count, code, details);
}

NAUV_WORK_CB(SendPluginCallback) {
  Nan::HandleScope scope;
  plugin_callback_data *data = reinterpret_cast<plugin_callback_data*>(
      async->data);
  // Attach cb and user_data to plugin_callback so that it can access them later
  v8::Local<v8::Function> plugin_callback = Nan::GetFunction(
      Nan::New<v8::FunctionTemplate>(PluginCallback)).ToLocalChecked();
  Nan::Set(plugin_callback, Nan::New("cb").ToLocalChecked(),
           Nan::New<v8::External>(reinterpret_cast<void*>(data->cb)));
  Nan::Set(plugin_callback, Nan::New("user_data").ToLocalChecked(),
           Nan::New<v8::External>(data->user_data));
  const int argc = 2;
  v8::Local<v8::Value> argv[argc] = {
    Nan::New(data->service_url).ToLocalChecked(),
    plugin_callback
  };
  Nan::Callback *callback = data->state->callback;
  callback->Call(argc, argv);
  delete data;
  uv_unref((uv_handle_t *)async);
  uv_close((uv_handle_t *)async, (uv_close_cb)free);
}

void plugin_get_metadata(void *state, const char *service_url,
                         grpc_credentials_plugin_metadata_cb cb,
                         void *user_data) {
  uv_async_t *async = static_cast<uv_async_t*>(malloc(sizeof(uv_async_t)));
  uv_async_init(uv_default_loop(),
                async,
                SendPluginCallback);
  plugin_callback_data *data = new plugin_callback_data;
  data->state = reinterpret_cast<plugin_state*>(state);
  data->service_url = service_url;
  data->cb = cb;
  data->user_data = user_data;
  async->data = data;
  /* libuv says that it will coalesce calls to uv_async_send. If there is ever a
   * problem with a callback not getting called, that is probably the reason */
  uv_async_send(async);
}

void plugin_destroy_state(void *ptr) {
  plugin_state *state = reinterpret_cast<plugin_state *>(ptr);
  delete state->callback;
}

}  // namespace node
}  // namespace grpc
