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

#include <node.h>

#include "call.h"
#include "call_credentials.h"
#include "channel_credentials.h"
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

Nan::Callback *ChannelCredentials::constructor;
Persistent<FunctionTemplate> ChannelCredentials::fun_tpl;

ChannelCredentials::ChannelCredentials(grpc_channel_credentials *credentials)
    : wrapped_credentials(credentials) {}

ChannelCredentials::~ChannelCredentials() {
  grpc_channel_credentials_release(wrapped_credentials);
}

void ChannelCredentials::Init(Local<Object> exports) {
  HandleScope scope;
  Local<FunctionTemplate> tpl = Nan::New<FunctionTemplate>(New);
  tpl->SetClassName(Nan::New("ChannelCredentials").ToLocalChecked());
  tpl->InstanceTemplate()->SetInternalFieldCount(1);
  Nan::SetPrototypeMethod(tpl, "compose", Compose);
  fun_tpl.Reset(tpl);
  Local<Function> ctr = Nan::GetFunction(tpl).ToLocalChecked();
  Nan::Set(
      ctr, Nan::New("createSsl").ToLocalChecked(),
      Nan::GetFunction(Nan::New<FunctionTemplate>(CreateSsl)).ToLocalChecked());
  Nan::Set(ctr, Nan::New("createInsecure").ToLocalChecked(),
           Nan::GetFunction(Nan::New<FunctionTemplate>(CreateInsecure))
               .ToLocalChecked());
  Nan::Set(exports, Nan::New("ChannelCredentials").ToLocalChecked(), ctr);
  constructor = new Nan::Callback(ctr);
}

bool ChannelCredentials::HasInstance(Local<Value> val) {
  HandleScope scope;
  return Nan::New(fun_tpl)->HasInstance(val);
}

Local<Value> ChannelCredentials::WrapStruct(
    grpc_channel_credentials *credentials) {
  EscapableHandleScope scope;
  const int argc = 1;
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

grpc_channel_credentials *ChannelCredentials::GetWrappedCredentials() {
  return wrapped_credentials;
}

NAN_METHOD(ChannelCredentials::New) {
  if (info.IsConstructCall()) {
    if (!info[0]->IsExternal()) {
      return Nan::ThrowTypeError(
          "ChannelCredentials can only be created with the provided functions");
    }
    Local<External> ext = info[0].As<External>();
    grpc_channel_credentials *creds_value =
        reinterpret_cast<grpc_channel_credentials *>(ext->Value());
    ChannelCredentials *credentials = new ChannelCredentials(creds_value);
    credentials->Wrap(info.This());
    info.GetReturnValue().Set(info.This());
    return;
  } else {
    // This should never be called directly
    return Nan::ThrowTypeError(
        "ChannelCredentials can only be created with the provided functions");
  }
}

NAN_METHOD(ChannelCredentials::CreateSsl) {
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
  if ((key_cert_pair.private_key == NULL) !=
      (key_cert_pair.cert_chain == NULL)) {
    return Nan::ThrowError(
        "createSsl's second and third arguments must be"
        " provided or omitted together");
  }
  grpc_channel_credentials *creds = grpc_ssl_credentials_create(
      root_certs, key_cert_pair.private_key == NULL ? NULL : &key_cert_pair,
      NULL);
  if (creds == NULL) {
    info.GetReturnValue().SetNull();
  } else {
    info.GetReturnValue().Set(WrapStruct(creds));
  }
}

NAN_METHOD(ChannelCredentials::Compose) {
  if (!ChannelCredentials::HasInstance(info.This())) {
    return Nan::ThrowTypeError(
        "compose can only be called on ChannelCredentials objects");
  }
  if (!CallCredentials::HasInstance(info[0])) {
    return Nan::ThrowTypeError(
        "compose's first argument must be a CallCredentials object");
  }
  ChannelCredentials *self =
      ObjectWrap::Unwrap<ChannelCredentials>(info.This());
  if (self->wrapped_credentials == NULL) {
    return Nan::ThrowTypeError("Cannot compose insecure credential");
  }
  CallCredentials *other = ObjectWrap::Unwrap<CallCredentials>(
      Nan::To<Object>(info[0]).ToLocalChecked());
  grpc_channel_credentials *creds = grpc_composite_channel_credentials_create(
      self->wrapped_credentials, other->GetWrappedCredentials(), NULL);
  if (creds == NULL) {
    info.GetReturnValue().SetNull();
  } else {
    info.GetReturnValue().Set(WrapStruct(creds));
  }
}

NAN_METHOD(ChannelCredentials::CreateInsecure) {
  info.GetReturnValue().Set(WrapStruct(NULL));
}

}  // namespace node
}  // namespace grpc
