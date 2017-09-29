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

#include <nan.h>
#include <node.h>
#include <uv.h>

#include <queue>

#include "call.h"
#include "call_credentials.h"
#include "grpc/grpc.h"
#include "grpc/grpc_security.h"
#include "grpc/support/log.h"

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

static Callback *plugin_callback;

CallCredentials::CallCredentials(grpc_call_credentials *credentials)
    : wrapped_credentials(credentials) {}

CallCredentials::~CallCredentials() {
  grpc_call_credentials_release(wrapped_credentials);
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
           Nan::GetFunction(Nan::New<FunctionTemplate>(CreateFromPlugin))
               .ToLocalChecked());
  Nan::Set(exports, Nan::New("CallCredentials").ToLocalChecked(), ctr);
  constructor = new Nan::Callback(ctr);

  Local<FunctionTemplate> callback_tpl =
      Nan::New<FunctionTemplate>(PluginCallback);
  plugin_callback =
      new Callback(Nan::GetFunction(callback_tpl).ToLocalChecked());
}

bool CallCredentials::HasInstance(Local<Value> val) {
  HandleScope scope;
  return Nan::New(fun_tpl)->HasInstance(val);
}

Local<Value> CallCredentials::WrapStruct(grpc_call_credentials *credentials) {
  EscapableHandleScope scope;
  const int argc = 1;
  if (credentials == NULL) {
    return scope.Escape(Nan::Null());
  }
  Local<Value> argv[argc] = {
      Nan::New<External>(reinterpret_cast<void *>(credentials))};
  MaybeLocal<Object> maybe_instance =
      Nan::NewInstance(constructor->GetFunction(), argc, argv);
  if (maybe_instance.IsEmpty()) {
    return scope.Escape(Nan::Null());
  } else {
    return scope.Escape(maybe_instance.ToLocalChecked());
  }
}

grpc_call_credentials *CallCredentials::GetWrappedCredentials() {
  return wrapped_credentials;
}

NAN_METHOD(CallCredentials::New) {
  if (info.IsConstructCall()) {
    if (!info[0]->IsExternal()) {
      return Nan::ThrowTypeError(
          "CallCredentials can only be created with the provided functions");
    }
    Local<External> ext = info[0].As<External>();
    grpc_call_credentials *creds_value =
        reinterpret_cast<grpc_call_credentials *>(ext->Value());
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
  grpc_call_credentials *creds = grpc_composite_call_credentials_create(
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
  state->pending_callbacks = new std::queue<plugin_callback_data *>();
  uv_mutex_init(&state->plugin_mutex);
  uv_async_init(uv_default_loop(), &state->plugin_async, SendPluginCallback);
  uv_unref((uv_handle_t *)&state->plugin_async);

  state->plugin_async.data = state;

  plugin.get_metadata = plugin_get_metadata;
  plugin.destroy = plugin_destroy_state;
  plugin.state = reinterpret_cast<void *>(state);
  plugin.type = "";
  grpc_call_credentials *creds =
      grpc_metadata_credentials_create_from_plugin(plugin, NULL);
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
  if (!info[3]->IsObject()) {
    return Nan::ThrowTypeError(
        "The callback's fourth argument must be an object");
  }
  grpc_status_code code =
      static_cast<grpc_status_code>(Nan::To<uint32_t>(info[0]).FromJust());
  Utf8String details_utf8_str(info[1]);
  char *details = *details_utf8_str;
  grpc_metadata_array array;
  grpc_metadata_array_init(&array);
  Local<Object> callback_data = Nan::To<Object>(info[3]).ToLocalChecked();
  if (!CreateMetadataArray(Nan::To<Object>(info[2]).ToLocalChecked(), &array)) {
    return Nan::ThrowError("Failed to parse metadata");
  }
  grpc_credentials_plugin_metadata_cb cb =
      reinterpret_cast<grpc_credentials_plugin_metadata_cb>(
          Nan::Get(callback_data, Nan::New("cb").ToLocalChecked())
              .ToLocalChecked()
              .As<External>()
              ->Value());
  void *user_data =
      Nan::Get(callback_data, Nan::New("user_data").ToLocalChecked())
          .ToLocalChecked()
          .As<External>()
          ->Value();
  cb(user_data, array.metadata, array.count, code, details);
  DestroyMetadataArray(&array);
}

NAUV_WORK_CB(SendPluginCallback) {
  Nan::HandleScope scope;
  plugin_state *state = reinterpret_cast<plugin_state *>(async->data);
  std::queue<plugin_callback_data *> callbacks;
  uv_mutex_lock(&state->plugin_mutex);
  state->pending_callbacks->swap(callbacks);
  uv_mutex_unlock(&state->plugin_mutex);
  while (!callbacks.empty()) {
    plugin_callback_data *data = callbacks.front();
    callbacks.pop();
    Local<Object> callback_data = Nan::New<Object>();
    Nan::Set(callback_data, Nan::New("cb").ToLocalChecked(),
             Nan::New<v8::External>(reinterpret_cast<void *>(data->cb)));
    Nan::Set(callback_data, Nan::New("user_data").ToLocalChecked(),
             Nan::New<v8::External>(data->user_data));
    const int argc = 3;
    v8::Local<v8::Value> argv[argc] = {
        Nan::New(data->service_url).ToLocalChecked(), callback_data,
        // Get Local<Function> from Nan::Callback*
        **plugin_callback};
    Nan::Callback *callback = state->callback;
    callback->Call(argc, argv);
    delete data;
  }
}

int plugin_get_metadata(
    void *state, grpc_auth_metadata_context context,
    grpc_credentials_plugin_metadata_cb cb, void *user_data,
    grpc_metadata creds_md[GRPC_METADATA_CREDENTIALS_PLUGIN_SYNC_MAX],
    size_t *num_creds_md, grpc_status_code *status,
    const char **error_details) {
  plugin_state *p_state = reinterpret_cast<plugin_state *>(state);
  plugin_callback_data *data = new plugin_callback_data;
  data->service_url = context.service_url;
  data->cb = cb;
  data->user_data = user_data;

  uv_mutex_lock(&p_state->plugin_mutex);
  p_state->pending_callbacks->push(data);
  uv_mutex_unlock(&p_state->plugin_mutex);

  uv_async_send(&p_state->plugin_async);
  return 0;  // Async processing.
}

void plugin_uv_close_cb(uv_handle_t *handle) {
  uv_async_t *async = reinterpret_cast<uv_async_t *>(handle);
  plugin_state *state = reinterpret_cast<plugin_state *>(async->data);
  uv_mutex_destroy(&state->plugin_mutex);
  delete state->pending_callbacks;
  delete state->callback;
  delete state;
}

void plugin_destroy_state(void *ptr) {
  plugin_state *state = reinterpret_cast<plugin_state *>(ptr);
  uv_close((uv_handle_t *)&state->plugin_async, plugin_uv_close_cb);
}

}  // namespace node
}  // namespace grpc
