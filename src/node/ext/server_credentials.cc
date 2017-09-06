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

#include "grpc/grpc.h"
#include "grpc/grpc_security.h"
#include "grpc/support/log.h"
#include "server_credentials.h"

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
using v8::External;
using v8::Function;
using v8::FunctionTemplate;
using v8::Integer;
using v8::Local;
using v8::Object;
using v8::ObjectTemplate;
using v8::String;
using v8::Value;

Nan::Callback *ServerCredentials::constructor;
Persistent<FunctionTemplate> ServerCredentials::fun_tpl;

ServerCredentials::ServerCredentials(grpc_server_credentials *credentials)
    : wrapped_credentials(credentials) {}

ServerCredentials::~ServerCredentials() {
  grpc_server_credentials_release(wrapped_credentials);
}

void ServerCredentials::Init(Local<Object> exports) {
  Nan::HandleScope scope;
  Local<FunctionTemplate> tpl = Nan::New<FunctionTemplate>(New);
  tpl->SetClassName(Nan::New("ServerCredentials").ToLocalChecked());
  tpl->InstanceTemplate()->SetInternalFieldCount(1);
  Local<Function> ctr = tpl->GetFunction();
  Nan::Set(
      ctr, Nan::New("createSsl").ToLocalChecked(),
      Nan::GetFunction(Nan::New<FunctionTemplate>(CreateSsl)).ToLocalChecked());
  Nan::Set(ctr, Nan::New("createInsecure").ToLocalChecked(),
           Nan::GetFunction(Nan::New<FunctionTemplate>(CreateInsecure))
               .ToLocalChecked());
  fun_tpl.Reset(tpl);
  constructor = new Nan::Callback(ctr);
  Nan::Set(exports, Nan::New("ServerCredentials").ToLocalChecked(), ctr);
}

bool ServerCredentials::HasInstance(Local<Value> val) {
  Nan::HandleScope scope;
  return Nan::New(fun_tpl)->HasInstance(val);
}

Local<Value> ServerCredentials::WrapStruct(
    grpc_server_credentials *credentials) {
  Nan::EscapableHandleScope scope;
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

grpc_server_credentials *ServerCredentials::GetWrappedServerCredentials() {
  return wrapped_credentials;
}

NAN_METHOD(ServerCredentials::New) {
  if (info.IsConstructCall()) {
    if (!info[0]->IsExternal()) {
      return Nan::ThrowTypeError(
          "ServerCredentials can only be created with the provided functions");
    }
    Local<External> ext = info[0].As<External>();
    grpc_server_credentials *creds_value =
        reinterpret_cast<grpc_server_credentials *>(ext->Value());
    ServerCredentials *credentials = new ServerCredentials(creds_value);
    credentials->Wrap(info.This());
    info.GetReturnValue().Set(info.This());
  } else {
    // This should never be called directly
    return Nan::ThrowTypeError(
        "ServerCredentials can only be created with the provided functions");
  }
}

NAN_METHOD(ServerCredentials::CreateSsl) {
  Nan::HandleScope scope;
  char *root_certs = NULL;
  if (::node::Buffer::HasInstance(info[0])) {
    root_certs = ::node::Buffer::Data(info[0]);
  } else if (!(info[0]->IsNull() || info[0]->IsUndefined())) {
    return Nan::ThrowTypeError(
        "createSSl's first argument must be a Buffer if provided");
  }
  if (!info[1]->IsArray()) {
    return Nan::ThrowTypeError(
        "createSsl's second argument must be a list of objects");
  }

  // Default to not requesting the client certificate
  grpc_ssl_client_certificate_request_type client_certificate_request =
      GRPC_SSL_DONT_REQUEST_CLIENT_CERTIFICATE;
  if (info[2]->IsBoolean()) {
    client_certificate_request =
        Nan::To<bool>(info[2]).FromJust()
            ? GRPC_SSL_REQUEST_AND_REQUIRE_CLIENT_CERTIFICATE_AND_VERIFY
            : GRPC_SSL_DONT_REQUEST_CLIENT_CERTIFICATE;
  } else if (!(info[2]->IsUndefined() || info[2]->IsNull())) {
    return Nan::ThrowTypeError(
        "createSsl's third argument must be a boolean if provided");
  }
  Local<Array> pair_list = Local<Array>::Cast(info[1]);
  uint32_t key_cert_pair_count = pair_list->Length();
  grpc_ssl_pem_key_cert_pair *key_cert_pairs =
      new grpc_ssl_pem_key_cert_pair[key_cert_pair_count];

  Local<String> key_key = Nan::New("private_key").ToLocalChecked();
  Local<String> cert_key = Nan::New("cert_chain").ToLocalChecked();

  for (uint32_t i = 0; i < key_cert_pair_count; i++) {
    Local<Value> pair_val = Nan::Get(pair_list, i).ToLocalChecked();
    if (!pair_val->IsObject()) {
      delete[] key_cert_pairs;
      return Nan::ThrowTypeError("Key/cert pairs must be objects");
    }
    Local<Object> pair_obj = Nan::To<Object>(pair_val).ToLocalChecked();
    Local<Value> maybe_key = Nan::Get(pair_obj, key_key).ToLocalChecked();
    Local<Value> maybe_cert = Nan::Get(pair_obj, cert_key).ToLocalChecked();
    if (!::node::Buffer::HasInstance(maybe_key)) {
      delete[] key_cert_pairs;
      return Nan::ThrowTypeError("private_key must be a Buffer");
    }
    if (!::node::Buffer::HasInstance(maybe_cert)) {
      delete[] key_cert_pairs;
      return Nan::ThrowTypeError("cert_chain must be a Buffer");
    }
    key_cert_pairs[i].private_key = ::node::Buffer::Data(maybe_key);
    key_cert_pairs[i].cert_chain = ::node::Buffer::Data(maybe_cert);
  }
  grpc_server_credentials *creds = grpc_ssl_server_credentials_create_ex(
      root_certs, key_cert_pairs, key_cert_pair_count,
      client_certificate_request, NULL);
  delete[] key_cert_pairs;
  if (creds == NULL) {
    info.GetReturnValue().SetNull();
  } else {
    info.GetReturnValue().Set(WrapStruct(creds));
  }
}

NAN_METHOD(ServerCredentials::CreateInsecure) {
  info.GetReturnValue().Set(WrapStruct(NULL));
}

}  // namespace node
}  // namespace grpc
