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
#include "credentials.h"
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

Nan::Callback *Credentials::constructor;
Persistent<FunctionTemplate> Credentials::fun_tpl;

Credentials::Credentials(grpc_credentials *credentials)
    : wrapped_credentials(credentials) {}

Credentials::~Credentials() {
  grpc_credentials_release(wrapped_credentials);
}

void Credentials::Init(Local<Object> exports) {
  HandleScope scope;
  Local<FunctionTemplate> tpl = Nan::New<FunctionTemplate>(New);
  tpl->SetClassName(Nan::New("Credentials").ToLocalChecked());
  tpl->InstanceTemplate()->SetInternalFieldCount(1);
  fun_tpl.Reset(tpl);
  Local<Function> ctr = Nan::GetFunction(tpl).ToLocalChecked();
  Nan::Set(ctr, Nan::New("createDefault").ToLocalChecked(),
           Nan::GetFunction(
               Nan::New<FunctionTemplate>(CreateDefault)).ToLocalChecked());
  Nan::Set(ctr, Nan::New("createSsl").ToLocalChecked(),
           Nan::GetFunction(
               Nan::New<FunctionTemplate>(CreateSsl)).ToLocalChecked());
  Nan::Set(ctr, Nan::New("createComposite").ToLocalChecked(),
           Nan::GetFunction(
               Nan::New<FunctionTemplate>(CreateComposite)).ToLocalChecked());
  Nan::Set(ctr, Nan::New("createGce").ToLocalChecked(),
           Nan::GetFunction(
               Nan::New<FunctionTemplate>(CreateGce)).ToLocalChecked());
  Nan::Set(ctr, Nan::New("createIam").ToLocalChecked(),
           Nan::GetFunction(
               Nan::New<FunctionTemplate>(CreateIam)).ToLocalChecked());
  Nan::Set(ctr, Nan::New("createInsecure").ToLocalChecked(),
           Nan::GetFunction(
               Nan::New<FunctionTemplate>(CreateInsecure)).ToLocalChecked());
  Nan::Set(exports, Nan::New("Credentials").ToLocalChecked(), ctr);
  constructor = new Nan::Callback(ctr);
}

bool Credentials::HasInstance(Local<Value> val) {
  HandleScope scope;
  return Nan::New(fun_tpl)->HasInstance(val);
}

Local<Value> Credentials::WrapStruct(grpc_credentials *credentials) {
  EscapableHandleScope scope;
  const int argc = 1;
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

grpc_credentials *Credentials::GetWrappedCredentials() {
  return wrapped_credentials;
}

NAN_METHOD(Credentials::New) {
  if (info.IsConstructCall()) {
    if (!info[0]->IsExternal()) {
      return Nan::ThrowTypeError(
          "Credentials can only be created with the provided functions");
    }
    Local<External> ext = info[0].As<External>();
    grpc_credentials *creds_value =
        reinterpret_cast<grpc_credentials *>(ext->Value());
    Credentials *credentials = new Credentials(creds_value);
    credentials->Wrap(info.This());
    info.GetReturnValue().Set(info.This());
    return;
  } else {
    const int argc = 1;
    Local<Value> argv[argc] = {info[0]};
    MaybeLocal<Object> maybe_instance = constructor->GetFunction()->NewInstance(
        argc, argv);
    if (maybe_instance.IsEmpty()) {
      // There's probably a pending exception
      return;
    } else {
      info.GetReturnValue().Set(maybe_instance.ToLocalChecked());
    }
  }
}

NAN_METHOD(Credentials::CreateDefault) {
  grpc_credentials *creds = grpc_google_default_credentials_create();
  if (creds == NULL) {
    info.GetReturnValue().SetNull();
  } else {
    info.GetReturnValue().Set(WrapStruct(creds));
  }
}

NAN_METHOD(Credentials::CreateSsl) {
  char *root_certs = NULL;
  grpc_ssl_pem_key_cert_pair key_cert_pair = {NULL, NULL};
  if (::node::Buffer::HasInstance(info[0])) {
    root_certs = ::node::Buffer::Data(info[0]);
  } else if (!(info[0]->IsNull() || info[0]->IsUndefined())) {
    return Nan::ThrowTypeError("createSsl's first argument must be a Buffer");
  }
  if (::node::Buffer::HasInstance(info[1])) {
    key_cert_pair.private_key = ::node::Buffer::Data(info[1]);
  } else if (!(info[1]->IsNull() || info[1]->IsUndefined())) {
    return Nan::ThrowTypeError(
        "createSSl's second argument must be a Buffer if provided");
  }
  if (::node::Buffer::HasInstance(info[2])) {
    key_cert_pair.cert_chain = ::node::Buffer::Data(info[2]);
  } else if (!(info[2]->IsNull() || info[2]->IsUndefined())) {
    return Nan::ThrowTypeError(
        "createSSl's third argument must be a Buffer if provided");
  }
  grpc_credentials *creds = grpc_ssl_credentials_create(
      root_certs, key_cert_pair.private_key == NULL ? NULL : &key_cert_pair,
      NULL);
  if (creds == NULL) {
    info.GetReturnValue().SetNull();
  } else {
    info.GetReturnValue().Set(WrapStruct(creds));
  }
}

NAN_METHOD(Credentials::CreateComposite) {
  if (!HasInstance(info[0])) {
    return Nan::ThrowTypeError(
        "createComposite's first argument must be a Credentials object");
  }
  if (!HasInstance(info[1])) {
    return Nan::ThrowTypeError(
        "createComposite's second argument must be a Credentials object");
  }
  Credentials *creds1 = ObjectWrap::Unwrap<Credentials>(
      Nan::To<Object>(info[0]).ToLocalChecked());
  Credentials *creds2 = ObjectWrap::Unwrap<Credentials>(
      Nan::To<Object>(info[1]).ToLocalChecked());
  grpc_credentials *creds = grpc_composite_credentials_create(
      creds1->wrapped_credentials, creds2->wrapped_credentials, NULL);
  if (creds == NULL) {
    info.GetReturnValue().SetNull();
  } else {
    info.GetReturnValue().Set(WrapStruct(creds));
  }
}

NAN_METHOD(Credentials::CreateGce) {
  Nan::HandleScope scope;
  grpc_credentials *creds = grpc_google_compute_engine_credentials_create(NULL);
  if (creds == NULL) {
    info.GetReturnValue().SetNull();
  } else {
    info.GetReturnValue().Set(WrapStruct(creds));
  }
}

NAN_METHOD(Credentials::CreateIam) {
  if (!info[0]->IsString()) {
    return Nan::ThrowTypeError("createIam's first argument must be a string");
  }
  if (!info[1]->IsString()) {
    return Nan::ThrowTypeError("createIam's second argument must be a string");
  }
  Utf8String auth_token(info[0]);
  Utf8String auth_selector(info[1]);
  grpc_credentials *creds =
      grpc_google_iam_credentials_create(*auth_token, *auth_selector, NULL);
  if (creds == NULL) {
    info.GetReturnValue().SetNull();
  } else {
    info.GetReturnValue().Set(WrapStruct(creds));
  }
}

NAN_METHOD(Credentials::CreateInsecure) {
  info.GetReturnValue().Set(WrapStruct(NULL));
}

NAN_METHOD(Credentials::CreateFromPlugin) {
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
  if (creds == NULL) {
    info.GetReturnValue().SetNull();
  } else {
    info.GetReturnValue().Set(WrapStruct(creds()));
  }
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
  grpc_status_code code = static_cast<grpc_status_code>(
      Nan::To<uint32_t>(info[0]).FromJust());
  char *details = *Nan::Utf8String(info[1]);
  grpc_metadata_array array;
  if (!CreateMetadataArray(Nan::To<Object>(info[2]).ToLocalChecked(),
                           &array, shared_ptr<Resources>(new Resources))){
    return Nan::ThrowError("Failed to parse metadata");
  }
  grpc_credentials_plugin_metadata_cb cb =
      reinterpret_cast<grpc_credentials_plugin_metadata_cb>(
          Nan::To<External>(
              Nan::Get(info.Callee, "cb").ToLocalChecked()
                            ).ToLocalChecked()->Value());
  void *user_data = Nan::To<External>(
      Nan::Get(info.Callee, "user_data").ToLocalChecked()
                                      ).ToLocalChecked()->Value();
  cb(user_data, array.metadata, array.count, code, details);
}

void plugin_get_metadata(void *state, const char *service_url,
                         grpc_credentials_plugin_metadata_cb cb,
                         void *user_data) {
  uv_async_t *async = new uv_async_t;
  uv_async_init(uv_default_loop(),
                async,
                PluginCallback);
}

}  // namespace node
}  // namespace grpc
